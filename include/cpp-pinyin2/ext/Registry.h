#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/ext/Dialect.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_pinyin::v2
{
    /// Builds one dialect on demand. A creator is called every time a dialect is resolved, and
    /// returns a fresh immutable description, so a resolved dialect can never be mutated by a
    /// later registration and an `Engine` built from it never changes under the caller's feet.
    using DialectCreator = std::function<std::shared_ptr<const Dialect>()>;

    /// A READ-ONLY table of dialect id -> creator, **held by the caller**.
    ///
    /// Read-only in the sense that matters (FR-4.5 of docs/REQUIREMENTS.md): it stores **creators only** -- never a
    /// dictionary path, never an `Engine`, never a cached table. Resolving an id produces a description;
    /// the root is still the caller's, supplied at `Engine` construction, so there is no global default
    /// dictionary and two instances may use two roots.
    ///
    /// **There is no process-level registry, and no `builtins()` factory.** The constructor registers the
    /// two built-ins (mandarin, cantonese) inside *that object*, so a declared registry is usable at once
    /// and two registries stay independent: an id is reachable only through the exact registry the caller
    /// handed to `Engine::open`.
    ///
    ///     cpp_pinyin::v2::Registry registry;
    ///     auto engine = cpp_pinyin::v2::Engine::open(registry, dictRoot, "mandarin");
    ///
    /// Thread safety: `add` takes a unique lock, `resolve`/`ids` a shared one, and only while an engine is
    /// being constructed -- the query path takes no lock at all. A creator is invoked after the lock is
    /// released, so it may call back into the registry.
    class CPP_PINYIN2_EXPORT Registry {
    public:
        /// Registers the two built-in dialects. The registry is empty of anything else, and it owns
        /// no dictionary: the root arrives later, as an argument to `Engine::open`.
        Registry();

        ~Registry();

        /// Registers `creator` under `id`. Returns false when the id is already taken and
        /// `replace` is false, or when `id`/`creator` is empty -- so a caller that did not mean to
        /// overwrite a built-in dialect learns about it instead of quietly winning.
        bool add(std::string id, DialectCreator creator, bool replace = false);

        /// Returns nullptr when `id` is unknown. Callers must not treat that as "fall back to
        /// Mandarin": an unknown dialect is an error, not a request for a default. `Engine::open`
        /// is the path that turns it into one, with a message that names the id.
        [[nodiscard]] std::shared_ptr<const Dialect> resolve(std::string_view id) const;

        /// Registered ids in **ascending byte order** (`std::sort` over `std::string`, not
        /// registration order), for diagnostics. The order is part of the contract, not an
        /// implementation detail: `cppp2_dialect_ids` hands C callers the same list in the same order.
        [[nodiscard]] std::vector<std::string> ids() const;

        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;
        Registry(Registry &&) = delete;
        Registry &operator=(Registry &&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace cpp_pinyin::v2
