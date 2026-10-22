#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cpp-pinyin2/core/U16Str.h>
#include "BuiltinDialects.h"

#include "Capabilities.h"
#include "EngineImpl.h"
#include "Utf8Conv.h"
#include "dict/DictManifest.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cpp_pinyin::v2
{
    namespace
    {
        /// The subdirectory a dialect's tables live in: its explicit choice, or its id.
        std::string effectiveSubdir(const Dialect &dialect) {
            return dialect.dictSubDir.empty() ? dialect.id : dialect.dictSubDir;
        }

        /// Applies the dialect's optional post-pass. Kept free-standing because it needs only
        /// public types, and used by both `Result`-returning query overloads.
        Result withPostProcess(const Dialect &dialect, std::vector<Segment> syllables) {
            Result result(std::move(syllables));
            if (dialect.postProcess)
                dialect.postProcess(result);
            return result;
        }
    }  // namespace

    // Registry

    struct Registry::Impl {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, DialectCreator> creators;
    };

    Registry::Registry() : impl_(std::make_unique<Impl>()) {
        // Built-ins are registered HERE, in the constructor of every registry -- not as static
        // objects and not behind a lazy singleton: whoever needs a registry declares one, and the
        // object they get already knows the built-ins. That also keeps initialization thread-safe
        // for a function-local registry (C++11 magic statics) and keeps the cost off programs that
        // never resolve a dialect by id.
        // Registered through add(), the same entry point every third-party dialect uses: one table, one
        // insertion path, no special case. (Before 2026-10-04 this wrote impl_->creators directly, so the
        // "the built-ins use the public registration entry too" claim in the playbook was simply false.)
        // The return value is deliberately not checked here: a freshly constructed registry has an empty
        // map by construction and both ids are non-empty literals.
        add("mandarin", [] { return std::make_shared<const Dialect>(detail::makeMandarinDialect()); });
        add("cantonese", [] { return std::make_shared<const Dialect>(detail::makeCantoneseDialect()); });
    }

    Registry::~Registry() = default;

    bool Registry::add(std::string id, DialectCreator creator, bool replace) {
        if (id.empty() || !creator)
            return false;

        const std::unique_lock lock(impl_->mutex);
        const auto existing = impl_->creators.find(id);
        if (existing != impl_->creators.end() && !replace)
            return false;
        impl_->creators.insert_or_assign(std::move(id), std::move(creator));
        return true;
    }

    std::shared_ptr<const Dialect> Registry::resolve(std::string_view id) const {
        DialectCreator creator;
        {
            const std::shared_lock lock(impl_->mutex);
            const auto it = impl_->creators.find(std::string(id));
            if (it == impl_->creators.end())
                return nullptr;
            creator = it->second;
        }
        // Called with the lock released: a creator is allowed to register another dialect.
        return creator();
    }

    std::vector<std::string> Registry::ids() const {
        const std::shared_lock lock(impl_->mutex);
        std::vector<std::string> out;
        out.reserve(impl_->creators.size());
        for (const auto &entry : impl_->creators)
            out.push_back(entry.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    // Engine

    class Engine::Impl {
    public:
        Impl(Dialect description, std::filesystem::path dictDir, const std::filesystem::path &subdir)
            // `dialect` is declared first, so it is fully constructed before `engine` reads its
            // converter. The converter is copied into the engine -- which slices it to the
            // ToneConverter base, exactly as intended (all behaviour is in the function pointers).
            : dialect(std::move(description)), engine(dictDir / subdir, dialect.tone) {}

        Dialect dialect;
        EngineImpl engine;
    };

    // R13 (docs/STABILITY.md section 4.5) + D4 (docs/DECISIONS.md section 4.1, the min_library handshake): the
    // dictionary manifest is checked BEFORE any table is read. The check itself lives in the dict layer
    // (`dict/DictManifest.{h,cpp}`) because it is a property of the DATA, not of this facade; what stays
    // here are the two numbers that are this ENGINE's own answers:
    //   * the data-format generation this library accepts -- the one definition `tools/check_bindings.py`
    //     parses and compares against every shipped `format.txt`;
    //   * the library's own version, from the build. Empty when a non-CMake build did not define it, in
    //     which case the `min_library` dimension is skipped rather than guessed.
    constexpr int kSupportedDictFormat = 2;

    constexpr std::string_view kLibraryVersion =
#ifdef CPP_PINYIN2_VERSION_STRING
        CPP_PINYIN2_VERSION_STRING;
#else
        "";
#endif

    Engine::Engine(const std::string_view dialectId, std::shared_ptr<const Dialect> dialect,
                   const std::filesystem::path &dictDir, std::filesystem::path subdir) {
        if (!dialect) {
            m_error = "unknown dialect: " + std::string(dialectId);
            return;
        }
        if (subdir.empty())
            subdir = effectiveSubdir(*dialect);
        if (detail::ManifestCheck check =
                detail::checkDictManifest(dictDir, subdir, kSupportedDictFormat, kLibraryVersion);
            !check.detail.empty()) {
            m_errorCode = check.code;
            m_error = std::move(check.detail);
            return;
        }
        impl_ = std::make_unique<Impl>(*dialect, dictDir, subdir);
    }

    Outcome<Engine> Engine::open(const Registry &registry, const std::filesystem::path &dictRoot,
                                 std::string_view dialectId) {
        // The classification lives HERE, not at the C boundary (S11a). This layer is the one that
        // knows whether the id resolved, so it is the one that says which kind of failure it is --
        // and the C face only forwards `Error::code` / `Error::detail` instead of re-asking the
        // registry (which would be a second, drifting authority).
        //
        // `Registry::resolve` calls the dialect's creator, i.e. the caller's own code. An exception
        // out of it is a failure this library did not anticipate and does not swallow: it propagates
        // to the caller unchanged, and the C boundary turns it into `Code::Internal`.
        std::shared_ptr<const Dialect> resolved = registry.resolve(dialectId);
        if (!resolved)
            return Error{Code::NotReady, "unknown dialect: " + std::string(dialectId)};

        Engine engine(dialectId, std::move(resolved), dictRoot, {});
        if (!engine.usable())
            return Error{engine.m_errorCode, engine.reason()};
        // No std::move here on purpose: `engine` is a local, and the return statement is one of the
        // copy-initialization contexts in which it is already treated as an rvalue, so the move into
        // `Outcome<Engine>` happens anyway. Spelling it out was redundant (-Wredundant-move).
        return engine;
    }

    Engine::Engine(const Dialect &dialect, const std::filesystem::path &dictDir) :
        Engine(dialect.id, std::make_shared<const Dialect>(dialect), dictDir, {}) {}

    Engine::Engine(const Dialect &dialect, const std::filesystem::path &dictDir, std::filesystem::path subdir) :
        Engine(dialect.id, std::make_shared<const Dialect>(dialect), dictDir, std::move(subdir)) {}

    Engine::~Engine() = default;
    Engine::Engine(Engine &&) noexcept = default;
    Engine &Engine::operator=(Engine &&) noexcept = default;

    bool Engine::usable() const noexcept { return impl_ && impl_->engine.isReady(); }

    const std::string &Engine::reason() const { return impl_ ? impl_->engine.lastError() : m_error; }

    const Dialect &Engine::dialect() const {
        static const Dialect kEmpty;
        return impl_ ? impl_->dialect : kEmpty;
    }

    Outcome<Result> Engine::query(std::string_view text, const EngineConfig &cfg) const {
        // One check covers both states with no usable engine underneath: the dictionary never
        // loaded, and the object was moved from (or built from an id that resolved to nothing).
        // Both now answer `Code::NotReady` naming the reason, instead of an empty result that a
        // caller cannot tell from a successful query that matched nothing (S11a).
        if (!usable())
            return Error{Code::NotReady, reason()};
        // The sentinel becomes a number in ONE place (core/Types.h `resolveStyle`): `EngineConfig{}`
        // asks for whatever this dialect defaults to, so an empty config means the same thing here, in
        // the C ABI and in both shells (C4/N27). A dialect that declares no styles falls back to 0 --
        // exactly the numbering it had before its styles had names.
        const int style = resolveStyle(cfg.style, defaultStyleOf(impl_->dialect.styles));
        const auto caps = detail::effectiveCaps(impl_->dialect, cfg);
        return withPostProcess(impl_->dialect,
                               impl_->engine.query(text, style, cfg.onError, cfg.includeCandidates, cfg.includeReading,
                                                   caps.vToU, caps.neutralTone5));
    }

    Outcome<Result> Engine::query(const std::vector<std::string> &chars, const EngineConfig &cfg) const {
        if (!usable())
            return Error{Code::NotReady, reason()};
        // N16: one element is one character, and that is a precondition, not a hint. The elements go
        // straight into the u16 conversion (EngineImpl::query) and then into the segmentation, where a
        // two-character element would be treated as one syllable -- silently. Refusing here keeps the
        // mistake at the boundary, where the caller can still see which element it was.
        for (std::size_t i = 0; i < chars.size(); ++i) {
            if (toU16(chars[i]).size() != 1)
                return Error{Code::InvalidArgument,
                             "query(vector<string>): element " + std::to_string(i) + " is not exactly one character"};
        }
        const int style = resolveStyle(cfg.style, defaultStyleOf(impl_->dialect.styles));
        const auto caps = detail::effectiveCaps(impl_->dialect, cfg);
        return withPostProcess(impl_->dialect,
                               impl_->engine.query(chars, style, cfg.onError, cfg.includeCandidates, cfg.includeReading,
                                                   caps.vToU, caps.neutralTone5));
    }
    Outcome<std::vector<std::string>> Engine::readings(std::string_view hanzi, const EngineConfig &cfg) const {
        // `usable()` rather than `impl_`: a moved-from facade and an engine built from a description
        // that resolved to nothing are the same failure to the caller, and both now name their
        // reason through `reason()` (2026-10-19, F19b: this path used to hardcode "engine is not
        // usable" while `query` used the stored message -- one state, two sentences).
        if (!usable())
            return Error{Code::NotReady, reason()};
        const int style = resolveStyle(cfg.style, defaultStyleOf(impl_->dialect.styles));
        const auto caps = detail::effectiveCaps(impl_->dialect, cfg);
        return impl_->engine.readings(hanzi, style, caps.vToU, caps.neutralTone5);
    }

    Outcome<std::string> Engine::toSimplified(std::string_view hanzi) const {
        if (!impl_)
            return Error{Code::NotReady, reason()};  // moved-from facade: there is nobody to ask
        // A ready engine answers "" for empty input, which stays a legal empty answer; an impl that
        // exists but is not ready answers its own `Code::NotReady` (EngineImpl::toSimplified).
        return impl_->engine.toSimplified(hanzi);
    }

    bool Engine::isPolyphonic(std::string_view hanzi) const {
        return impl_ ? impl_->engine.isPolyphonic(hanzi) : false;
    }

    Outcome<void> Engine::loadUserDict(const std::filesystem::path &path, bool clear) {
        if (!usable())
            return Error{Code::NotReady, reason()};
        if (!impl_->engine.loadUserDict(path, clear))
            return Error{Code::Load, impl_->engine.lastError()};
        return {};
    }

    Outcome<void> Engine::loadTraditionalDict(const std::filesystem::path &path, bool clear) {
        if (!usable())
            return Error{Code::NotReady, reason()};
        if (!impl_->engine.loadTraditionalDict(path, clear))
            return Error{Code::Load, impl_->engine.lastError()};
        return {};
    }

    void Engine::setDisambiguationCallback(DisambiguationCallback callback) {
        if (impl_)
            impl_->engine.setDisambiguationCallback(std::move(callback));
    }
}  // namespace cpp_pinyin::v2
