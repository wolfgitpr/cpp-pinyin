#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_pinyin::v2
{
    class Registry;

    /// Dialect-agnostic query facade: every dialect's operations, parameterized by a dialect description
    /// instead of per-class (the `Mandarin` / `Cantonese` classes are no longer public). The dictionary
    /// root is a constructor argument with no setter (FR-4.5 of docs/REQUIREMENTS.md), so objects with
    /// different roots or dialects coexist in one process without interfering.
    ///
    /// THREADS: after construction completes, concurrent READ-only calls (`query` / `readings` /
    /// `toSimplified` / `isPolyphonic`) are safe on a shared instance; `loadUserDict` /
    /// `loadTraditionalDict` / `setDisambiguationCallback` write what readers touch and must not overlap a
    /// read from another thread -- a data race that corrupts the heap.
    ///
    /// FAILURE: `open` answers with an `Outcome` carrying the kind of failure; the description
    /// constructors cannot return anything, so a failed construction is reportable only through `open`, and
    /// the public face has no `isReady()` / `lastError()` (those belong to the implementation).
    ///
    /// Every accessor stays safe on an engine that is not ready: `query`, `readings`, `toSimplified` and
    /// the `load*` entries answer `Code::NotReady` rather than dereferencing anything; only `isPolyphonic`
    /// (false) and `dialect()` (an empty id) have an empty value to return.
    class CPP_PINYIN2_EXPORT Engine {
    public:
        /// Resolves `dialectId` through `registry` -- the CALLER's registry; there is no global one -- and
        /// loads `dictRoot / subdir`, `subdir` being the dialect's `dictSubDir` (its `id` when empty). This is
        /// the entry point that reports WHY it could not build an engine (`Error`'s two fields, one channel):
        /// an id the registry does not know is an error and is NEVER resolved to another dialect -- the outcome
        /// holds no value, `error().code` is `Code::NotReady`, `error().detail` is `"unknown dialect: " + dialectId`;
        /// a dictionary that would not load is `Code::Load` with the files and the directory named in `detail`.
        [[nodiscard]] static Outcome<Engine> open(const Registry &registry, const std::filesystem::path &dictRoot,
                                                  std::string_view dialectId);

        /// Uses the description as given, without consulting the registry -- the path to take when
        /// a caller wants a dialect that is not registered, or wants to be immune to later
        /// registrations under the same id.
        Engine(const Dialect &dialect, const std::filesystem::path &dictDir);
        Engine(const Dialect &dialect, const std::filesystem::path &dictDir, std::filesystem::path subdir);

        ~Engine();

        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;
        Engine(Engine &&) noexcept;
        Engine &operator=(Engine &&) noexcept;


        /// The description this engine was built from. Empty-id description when construction
        /// failed or the object was moved from.
        [[nodiscard]] const Dialect &dialect() const;

        /// The rows for `text`, or an error instead of them when this engine cannot answer: an
        /// The rows for `text`, or an error when this engine cannot answer: a not-ready engine (an
        /// unresolvable description, a moved-from object) answers `Code::NotReady` -- never an empty
        /// result, which reads like a successful query that found nothing (docs/CONTRACT.md section 2).
        /// Every other failure of a running engine stays in the value (`Segment::failure_kind`).
        ///
        /// TWO overloads, differing in the unit of input: a string is queried as text (converted and
        /// segmented), a vector as a list of characters already split, so every element must be exactly one
        /// character. `query({"x"})` is therefore ambiguous and g++ rejects it -- deliberately: "ni hao" is
        /// ONE string but TWO invalid characters, so a braced list cannot pick a reading for you. Say which
        /// you mean: `query("x")` for text, `query(std::vector<std::string>{"x"})` for a character list.
        [[nodiscard]] Outcome<Result> query(std::string_view text, const EngineConfig &cfg = {}) const;
        [[nodiscard]] Outcome<Result> query(const std::vector<std::string> &chars, const EngineConfig &cfg = {}) const;
        [[nodiscard]] Outcome<std::vector<std::string>> readings(std::string_view hanzi,
                                                                 const EngineConfig &cfg = {}) const;

        /// Traditional -> simplified, one code point at a time. Fails with `Code::NotReady` when the
        /// engine is moved-from or was built from a description that resolved to nothing -- the same
        /// state the C face reports as `CPPP2_ERR_NOT_READY`, which this face could not say before
        /// (2026-10-19: it answered an empty string, indistinguishable from "the input was empty").
        [[nodiscard]] Outcome<std::string> toSimplified(std::string_view hanzi) const;
        /// Whether `hanzi`'s **first character** starts a phrase in the corpus (occurs across phrases with
        /// more than one reading) -- the AUTHORITY for the predicate; `docs/API_zh.md` / `docs/API_en.md`
        /// point here rather than restate it. Test: `phrases.builtinStart || phrases.userStart` (EITHER layer
        /// counts; checking only the built-in layer was a gate defect). Only the first character is examined
        /// (the rest of `hanzi` is ignored); a moved-from engine answers `false`. It says "starts a phrase",
        /// not "has several readings of its own" -- a single-reading character opening a phrase is `true` (C-13).
        [[nodiscard]] bool isPolyphonic(std::string_view hanzi) const;

        /// Load a user dictionary over the built-in one. Success carries nothing; a failure carries
        /// `Code::NotReady` (moved-from / unusable engine) or `Code::Load` (the path was read but the
        /// table did not load), with the library's own detail -- which the C face cannot deliver
        /// (`cppp2_load_user_dict` has no `err` out-parameter).
        [[nodiscard]] Outcome<void> loadUserDict(const std::filesystem::path &path, bool clear = true);
        /// Same contract as `loadUserDict`, for the traditional/simplified table.
        [[nodiscard]] Outcome<void> loadTraditionalDict(const std::filesystem::path &path, bool clear = true);
        /// Install the callback described on `DisambiguationCallback` (core/Types.h): it is asked
        /// for a character with several readings, and is handed the raw text plus the character's
        /// UTF-8 byte offset in it, so a caller can look at the surrounding text and answer.
        /// Passing an empty function removes it. MUTATES engine state: see the class comment above.
        ///
        /// THREADS (contract, and the reason it is written here rather than only in docs/CONTRACT.md):
        /// the callback runs on the CALLING thread, in place, and invocations from several threads are NOT
        /// serialised -- two concurrent queries on one shared engine can be inside it at the same time. A
        /// callback shared across threads must therefore be thread-safe itself and must not capture mutable
        /// state that the queries both touch. Nothing here queues, copies or defers the call.
        void setDisambiguationCallback(DisambiguationCallback callback);

    private:
        /// Shared by `open` and the two description constructors. `dialect` is null when the id did
        /// not resolve.
        Engine(std::string_view dialectId, std::shared_ptr<const Dialect> dialect, const std::filesystem::path &dictDir,
               std::filesystem::path subdir);

        /// Whether there is a usable engine underneath. False for a moved-from object AND for one
        /// built from a description that resolved to nothing useful -- the same test the public
        /// two-place face used to carry, kept private because the query paths need it.
        [[nodiscard]] bool usable() const noexcept;

        /// The reason `usable()` is false: the inner engine message, or ours when there is no
        /// inner engine at all. Returned rather than exposed.
        [[nodiscard]] const std::string &reason() const;
        class Impl;
        std::unique_ptr<Impl> impl_;
        /// Why construction failed. Empty whenever `impl_` is set.
        std::string m_error;
        /// How to classify that failure when `open` turns it into an `Outcome`. `Load` unless the
        /// dictionary's manifest asks for a newer library (docs/CONTRACT.md section 2), which is the whole
        /// point of the separate code: "upgrade the library" is actionable, "the data is broken" is not.
        ///
        /// SCOPE: produced by BOTH construction paths, exposed by only one -- `open` returns
        /// `Outcome<Engine>`, while the description constructors cannot return anything, so a host using
        /// them learns only `usable() == false` plus `reason()`. That is a property of the constructor
        /// shape, not a missing wire: a public `failureCode()` would expose it and is deliberately NOT in
        /// the frozen surface (`tests/abi/expected_public_members.txt`).
        Code m_errorCode = Code::Load;
    };

    /// Where this build installed the dictionary tables -- the C++ face of `cppp2_dict_dir_candidate`,
    /// with ONE implementation behind both spellings (a C++ host should not have to include the C header).
    ///
    /// Comes from the loaded module's own location plus the install-time relative path: never an
    /// environment variable (the shells own that) and never a guess at the host executable's directory
    /// (decisions D1/D2, docs/DECISIONS.md section 4.1). EMPTY when there is no usable anchor -- a static
    /// build has none, because "this module" would resolve to the host executable, which D1 forbids.
    /// Cached after the first call. Returns a COPY; the C spelling hands out a pointer into that cache
    /// instead, because `const char *` is all that ABI can carry.
    [[nodiscard]] CPP_PINYIN2_EXPORT std::string dictDirCandidate();

}  // namespace cpp_pinyin::v2
