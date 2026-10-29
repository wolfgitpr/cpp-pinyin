#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/Syllable.h>

namespace cpp_pinyin::v2
{
    // Style enums — strongly typed per dialect
    // Single authority for the style numbering: `ManTone::Style` / `CanTone::Style` alias these two
    // enums, and the C ABI's `int32_t style` carries exactly these numbers.
    // NOTE: this library's own historical numbering, NOT pypinyin's Style values (pypinyin has
    // INITIALS=3 / FIRST_LETTER=4 / BOPOMOFO=10).
    // Reference: https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/constants.py
    enum class MandarinStyle {
        // Plain style, no tone marks. Example: 中国 -> ``zhong guo``
        NORMAL = 0,
        // Standard tone style, the mark on the final's first vowel (the default). Example: 中国 -> ``zhōng guó``
        TONE = 1,
        // Tone style 2: the tone number [1-4] goes after the final's vowel. Example: 中国 -> ``zho1ng guo2``
        TONE2 = 2,
        // First-letter style. Example: 中国 -> ``z g``
        FIRST_LETTER = 3,
        // Bopomofo style. Example: 中国 -> ``ㄓㄨㄥ ㄍㄨㄛˊ``
        BOPOMOFO = 4,
        // The initial. zh/ch/sh match longest; by 《汉语拼音方案》 y/w/yu are NOT initials, so a
        // zero-initial syllable yields an empty string ("阿" -> "", "衣" -> "", "知" -> "zh").
        // Aligned with pypinyin's Style.INITIALS(strict=True).
        INITIALS = 5,
        // The final, tone removed, in its canonical form: yi->i, ya->ia, you->iou, yu->v, yue->ve,
        // jue->ve, gui->uei, lun->uen, zhi->i; ü is written as v unless vToU=true. Aligned with
        // pypinyin's Style.FINALS.
        FINALS = 6,
        // The final plus a trailing tone number: ong1 / uen4. Whether a neutral tone gets a 5 is decided
        // by markNeutralAs5.
        FINALS_TONE3 = 7,
        // Tone style 3: the tone number [1-4] goes after the whole syllable. Example: 中国 -> ``zhong1 guo2``
        TONE3 = 8
    };

    enum class CantoneseStyle {
        // Plain style, no tone marks. Example: 中国 -> ``zung gwok``
        NORMAL = 0,
        // First-letter style. Example: 中國 -> ``z g``
        FIRST_LETTER = 3,
        // Tone style 3: the tone number after the whole syllable. Example: 中国 -> ``zung1 gwok3``
        TONE3 = 8
    };

    // Style identity (docs/CONTRACT.md section 5)
    // A dialect's styles live in ONE shared number domain (0 .. `ToneConverter::MAX_STYLE`), each
    // dialect occupying a SPARSE part of it: Mandarin fills all nine, Cantonese declares 0, 3 and 8 (see
    // `CantoneseStyle`) and has nothing at 1, 2 or 4..7. The first shape proposed was a dense
    // `vector<string>`, which cannot express that: every entry therefore carries the number it belongs
    // to.
    struct CPP_PINYIN2_EXPORT StyleDecl {
        /// Canonical, lower-case, hyphens between words: "tone2", "first-letter", "finals-tone3".
        std::string name;

        /// The number this name is an alias for: the value `EngineConfig::style` carries. Must be
        /// inside `[0, ToneConverter::MAX_STYLE)`, because it indexes `ToneConverter::m_converts`.
        int alias = 0;

        /// The dialect's own default -- what a caller gets when it does not ask for a style.
        bool isDefault = false;
    };

    /// Looks `name` up in a dialect's style table (`Dialect::styles`). Returns nullptr when that
    /// dialect does not declare the name: an unknown name is NOT "pass the reading through", it is
    /// "no such style here", and the caller decides what to do with that answer.
    [[nodiscard]] inline const StyleDecl *findStyle(const std::vector<StyleDecl> &styles,
                                                    std::string_view name) noexcept {
        for (const StyleDecl &decl : styles)
            if (decl.name == name)
                return &decl;
        return nullptr;
    }

    /// The dialect's own default style: the alias of the ONE entry whose `isDefault` is set, or -1 when
    /// the table marks none (or is empty). This is the SINGLE authority for "what an empty config means
    /// here" -- `cppp2_style_list` publishes the same flag, and until 2026-10-18 a separate
    /// `Dialect::defaultStyle` field said it a second time, so a plugin could declare a default that the
    /// renderer did not use. `resolveStyle` below turns the -1 into the 0 fallback, so -1 never escapes
    /// as a style. A table carrying two `isDefault` entries answers with the first;
    /// tests/cpp/test_style_table.cpp refuses that shape for the built-ins.
    [[nodiscard]] inline int defaultStyleOf(const std::vector<StyleDecl> &styles) noexcept {
        for (const StyleDecl &decl : styles)
            if (decl.isDefault)
                return decl.alias;
        return -1;
    }

    /// A style as a caller writes it: a number (`Dialect::styles[].alias`), a `MandarinStyle` /
    /// `CantoneseStyle` member, or `Style{}` -- the sentinel `-1`, "whatever this dialect defaults
    /// to" (`resolveStyle` below turns it into a number exactly once).
    ///
    /// It exists so the ONE merged config can be dialect-agnostic without the numbering losing its
    /// spellings: `cfg.style = MandarinStyle::TONE3;` still compiles and still means 8. The
    /// conversions are implicit on purpose -- spelled out at every call site, the config merge would
    /// have become a rewrite of every caller.
    struct CPP_PINYIN2_EXPORT Style {
        /// The number, or -1 for "the dialect's own default".
        int alias = -1;

        Style() = default;
        // NOLINTNEXTLINE(google-explicit-constructor): the whole point is that the spellings survive.
        Style(int value) noexcept : alias(value) {}
        Style(MandarinStyle value) noexcept : alias(static_cast<int>(value)) {}
        Style(CantoneseStyle value) noexcept : alias(static_cast<int>(value)) {}

        /// So `cfg.style` can still be handed to anything that takes the raw number.
        operator int() const noexcept { return alias; }
    };

    /// The ONE place where "no style was asked for" becomes a number: the dialect's own default, or 0
    /// when it declares no styles at all -- 0 is the numbering every dialect shared before styles had
    /// names, so a custom dialect that declares nothing keeps behaving exactly as it did.
    [[nodiscard]] inline int resolveStyle(const Style &asked, int dialectDefault) noexcept {
        if (asked.alias >= 0)
            return asked.alias;
        return dialectDefault >= 0 ? dialectDefault : 0;
    }

    // Error handling mode
    enum class ErrorHandling { Default = 0, Drop = 1 };

    // Status, error and the value-or-error result of an operation
    // ONE error channel: an operation that can fail returns an `Outcome`, and an `Outcome` without
    // a value carries the `Code` that says which kind of failure it was. `Code` is the SINGLE value
    // domain for failures: `Outcome::error().code` carries it and so does `Segment::failure_kind` (S7).
    // The numbers are frozen: new codes may only be APPENDED, because a binding compiled against this
    // header must keep reading the same failures after a library upgrade. The old `Unsupported` value
    // is deliberately gone (docs/CONTRACT.md section 2, the `Unsupported` clause): it had no producer
    // and no consumer.
    enum class Code {
        // No failure.
        Ok = 0,
        // The caller passed something the library cannot use (a bad argument, a malformed syllable).
        InvalidArgument = 1,
        // This object cannot answer: a dialect id that did not resolve, or a handle that was moved
        // from. It is a state of the object, not of the call.
        NotReady = 2,
        // The dictionary files could not be loaded (missing, unreadable, or refused by the loader).
        Load = 3,
        // Anything the library did not anticipate -- including an exception thrown by a caller's
        // code (a disambiguation callback, a dialect creator) and translated at the boundary
        // (docs/CONTRACT.md section 2; the C face reports it as CPPP2_ERR_INTERNAL).
        Internal = 4,
        // The dictionary directory was found and read, but it declares a `min_library` newer than this
        // library. Separate from `Load` on purpose: a caller can act on "your data is for a newer
        // release" (upgrade the library), while `Load` only says the data is broken. A MISSING
        // `min_library` means "no requirement" (hand-built directories stay usable), and the
        // table-format marker stays `Load`, exactly as R13 defined it.
        // Appended 2026-10-18 by the min_library handshake (docs/DECISIONS.md section 4.1).
        DictIncompatible = 5
    };

    /// The text of `code`, for every value of `Code` and nothing else.
    ///
    /// A static string: no allocation, no ownership, safe to call from a signal-free context. It is
    /// the single authority for the wording -- a binding prints this instead of inventing a reason
    /// (docs/CONTRACT.md section 2, "文案单一真相源").
    [[nodiscard]] CPP_PINYIN2_EXPORT const char *codeText(Code code) noexcept;

    /// A failure: its kind, plus the human-readable detail of this particular occurrence.
    ///
    /// `detail` is for logs and error messages; `code` is what a program branches on. A detail may
    /// be empty (a failure the library can only classify); `code` is `Code::Ok` only in a default
    /// constructed object that no operation produced.
    struct CPP_PINYIN2_EXPORT Error {
        Code code = Code::Ok;
        std::string detail;
    };

    /// What `value_or_throw()` throws -- the error itself, so a `catch` can still branch on `code`.
    ///
    /// It derives from `std::runtime_error` so a caller that only wants "something went wrong" can
    /// catch that, and its `what()` is the error's `detail`.
    class CPP_PINYIN2_EXPORT ErrorException : public std::runtime_error {
    public:
        explicit ErrorException(Error error);

        /// The failure this exception was built from (also its `what()`).
        [[nodiscard]] const Error &error() const noexcept;

    private:
        Error m_error;
    };

    /// The result of an operation that either produced a `T` or failed: one of the two, never both.
    ///
    /// Read it with `has_value()` / `operator bool` before `value()` / `error()`. Neither accessor
    /// invents a `Code`: `value()` on a failure throws `ErrorException` (the same type `value_or_throw()`
    /// throws), while `error()` on a **success** throws `std::bad_variant_access` on purpose -- "the
    /// caller asked the wrong question" is a programming error, and a `Code` for it would break the frozen
    /// 0..5 domain. `ErrorException` is the only exception a fallible call throws out of a failure
    /// (docs/CONTRACT.md section 2, "Exceptions"). Move-only `T` (i.e. `Engine`) makes the `Outcome`
    /// move-only too: `value()` on an rvalue moves the value out instead of copying it.
    template <class T>
    class Outcome {
        // `Outcome<Error>` would collide with itself: `Outcome(T)` and `Outcome(Error)` become the
        // same signature, and which one won would depend on instantiation order. No instantiation
        // exists today -- this keeps it that way instead of leaving a trap (2026-10-19 audit, F47).
        static_assert(!std::is_same<T, Error>::value,
                      "Outcome<Error> is ambiguous by construction; wrap it or use Outcome<void>");

    public:
        Outcome(T value) : m_value(std::move(value)) {}
        Outcome(Error error) : m_value(std::move(error)) {}

        /// true = a value, false = an error.
        [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(m_value); }

        /// Same as `has_value()`, spelled for an `if (auto r = ...)`.
        [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

        /// The value. Throws `ErrorException` on a failure -- see `value_or_throw()` for the spelling
        /// that says so at the call site.
        [[nodiscard]] const T &value() const & { return asValue(); }
        [[nodiscard]] T &&value() && {
            if (!has_value())
                throw ErrorException(std::get<Error>(m_value));
            return std::get<T>(std::move(m_value));
        }
        [[nodiscard]] T &value() & { return asValue(); }

        [[nodiscard]] const Error &error() const & { return std::get<Error>(m_value); }
        [[nodiscard]] Error &error() & { return std::get<Error>(m_value); }

        /// The value, or the failure as an `ErrorException`. Same throw as `value()`; the name is what
        /// makes "this call may throw" visible where it happens.
        [[nodiscard]] const T &value_or_throw() const & { return asValue(); }
        [[nodiscard]] T &value_or_throw() & { return asValue(); }
        [[nodiscard]] T &&value_or_throw() && {
            if (!has_value())
                throw ErrorException(std::get<Error>(m_value));
            return std::get<T>(std::move(m_value));
        }

    private:
        T &asValue() {
            if (!has_value())
                throw ErrorException(std::get<Error>(m_value));
            return std::get<T>(m_value);
        }

        const T &asValue() const {
            if (!has_value())
                throw ErrorException(std::get<Error>(m_value));
            return std::get<T>(m_value);
        }

        std::variant<T, Error> m_value;
    };

    /// `Outcome<void>`: the operation either succeeded or failed, and carries no value.
    ///
    /// An explicit SPECIALIZATION, not `Outcome<std::monostate>`: the call site reads `Outcome<void>` (the
    /// same shape as `std::expected<void, E>`), and no public NAME is added, so the frozen name list
    /// (`tests/abi/expected_public_names.v2.txt`) does not move for a change that only refines a return
    /// type.
    ///
    /// Deliberately NO `value()`: a `void value()` that threw on failure would give one spelling two
    /// behaviours. Success has nothing to read, so `value_or_throw()` (a no-op on success, an
    /// `ErrorException` on failure) is the only way to consume it -- which makes `if (!r) return
    /// r.error();` the natural spelling at a call site.
    template <>
    class Outcome<void> {
    public:
        /// Success. A default-constructed `Outcome<void>` is what "nothing to report" looks like.
        Outcome() noexcept = default;
        Outcome(Error error) : m_error(std::move(error)), m_ok(false) {}

        [[nodiscard]] bool has_value() const noexcept { return m_ok; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ok; }

        /// The failure. `Outcome<void>` has no alternative to get wrong, so unlike the primary
        /// template this returns a default `Error` (`Code::Ok`) on success instead of throwing
        /// (docs/CONTRACT.md section 2, "Exceptions").
        [[nodiscard]] const Error &error() const & { return m_error; }
        [[nodiscard]] Error &error() & { return m_error; }

        /// No-op on success; throws `ErrorException` on failure.
        void value_or_throw() const {
            if (!m_ok)
                throw ErrorException(m_error);
        }

    private:
        Error m_error{};
        bool m_ok = true;
    };

    // Result row (S7: the 2.0 face's shape)
    // One input span and what the engine read for it: the shape authority (docs/CONTRACT.md §3) minus the
    // C ABI's `struct_size`/`candidate_count`; `character`/`reading`/`failed` merely renamed.
    // RULED (S8): `candidates` stays an array of STRINGS: seven readings have an (initial, fin)
    // projection that is not invertible (`hm`, `ǹ`, `ḿ`, `yo`, `yō`, `ò`, `ō`;
    // tests/cpp/test_syllable_producer.cpp), so moving that column would move the frozen digest and
    // the C ABI bytes at once - that projection, NOT the frozen corpus, is why (measured 2026-10-09).
    struct CPP_PINYIN2_EXPORT Segment {
        // The input fragment this row corresponds to (UTF-8). A run of ASCII letters/digits may be more
        // than one character ("hello", "abc1").
        std::string text;
        // The reading selected for this fragment in context, STRUCTURED (data, not a rendering). Filled
        // only when `includeReading = true` (S8); with it off the field stays default-constructed EXCEPT
        // for `tone`, which the Mandarin sandhi pass writes unconditionally (core/ToneSandhi.h) --
        // "default-constructed" is not the same claim as "never written",
        // which means "not asked for", not "no reading here" -- the reading is in `rendered` anyway, and
        // `failure_kind`, never this field, is what tells a failed row apart.
        Syllable reading;
        // Every reading of this fragment itself: word.txt row order, deduplicated on the CONVERTED
        // string keeping the first, each converted with the same style. Empty with
        // `includeCandidates=false`, the FAILED rows excepted (always {text}). See
        // docs/REQUIREMENTS.md section 1 K9.
        std::vector<std::string> candidates;
        // The failure classification: `Code::Ok` means this fragment has a reading. Its value range is
        // `Code` (this header), so its text comes from `codeText()` too -- a binding layer must not write
        // its own reason strings. A failed row carries `InvalidArgument` (the "library cannot use it"
        // code), and `Code::Ok` is reserved for "this row succeeded", exactly the old `failed == false`.
        //
        // RULED (S8): a TEMPORARY use of `InvalidArgument`, not a new value domain -- `Code` is frozen at
        // 0..5 with new codes only APPENDED, so "not a Hanzi" vs "a Hanzi the dictionary does not know"
        // stays one code until a consumer asks for finer.
        Code failure_kind = Code::Ok;
        // ★ The one rendered string of this row: what the dialect's converter produced for the selected
        // reading, byte for byte what the old `reading` field carried. A failed row holds the input text
        // unchanged (`rendered == text`).
        std::string rendered;
    };

    // Result container
    /// The rows of one query, in input order. `value_type` is `Segment` (one row), not a `Syllable`.
    /// Out-of-range behaviour is `std::vector`'s: `operator[]` does not check, `at()` throws
    /// `std::out_of_range`.
    class CPP_PINYIN2_EXPORT Result {
    public:
        using value_type = Segment;
        using const_iterator = std::vector<Segment>::const_iterator;

        Result() = default;
        explicit Result(std::vector<Segment> syllables);

        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
        const Segment &operator[](size_t i) const noexcept;
        const Segment &at(size_t i) const;
        // Mutable access, for post-processing such as `applyMandarinToneSandhi` rewriting `rendered`.
        Segment &operator[](size_t i) noexcept;
        Segment &at(size_t i);

        [[nodiscard]] const_iterator begin() const noexcept;
        [[nodiscard]] const_iterator end() const noexcept;
        [[nodiscard]] const_iterator cbegin() const noexcept;
        [[nodiscard]] const_iterator cend() const noexcept;

        [[nodiscard]] std::string join(std::string_view delimiter = " ") const;
        [[nodiscard]] std::vector<std::string> toVector() const;

    private:
        std::vector<Segment> m_syllables;
    };

    // Per-dialect configuration
    // ONE config for every dialect (C4/N27, 2026-10-06): the three per-dialect structs disagreed about
    // the default style (TONE / TONE3 / 0). The default is `Style{}`, the sentinel -1 ("whatever this
    // dialect defaults to"), resolved in one place (`resolveStyle` above); a dialect declaring no styles
    // falls back to 0. `includeReading` is the one switch NOT about rendering: the structured
    // `Segment::reading` (S8), default to false, never masked by a capability flag (every dialect has
    // readings); paying per row for it is what put the S7 query path outside its performance gate.
    struct CPP_PINYIN2_EXPORT EngineConfig {
        /// What was asked for: a `Dialect::styles[].alias`, a `MandarinStyle`/`CantoneseStyle` member,
        /// or `Style{}` for "the dialect's own default".
        Style style;
        ErrorHandling onError = ErrorHandling::Default;
        bool includeCandidates = true;
        // false (the default): `reading` stays default-constructed -- see the struct comment above.
        bool includeReading = false;
        /// Masked by `Dialect::supportsVToU`: ignored, not silently reinterpreted, where unsupported.
        bool vToU = false;
        /// Same, for `Dialect::supportsNeutralTone5`.
        bool markNeutralAs5 = false;
    };

    /// Called for a character the dictionary gave several readings and the phrase window did not settle:
    /// return the reading to use, or "" to keep the engine's own choice. Arguments: the caller's `text`,
    /// the character's UTF-8 BYTE offset in it (deliberately NOT an index into the hanzi sequence, which
    /// diverges from the second character on), the `hanzi`, and the `candidates` in this dialect's order;
    /// the views live only for the call. Installed with `Engine::setDisambiguationCallback`. A TYPE OF
    /// ITS OWN, not a `std::function` alias: an alias naming only standard-library types is the SAME type
    /// in every generation, so the other generation's callback would compile and surface as behaviour.
    class DisambiguationCallback {
    public:
        /// What the engine stores and calls. Spelled once so the four parameter types cannot drift.
        using Body = std::function<std::string(std::string_view text, size_t position, std::string_view hanzi,
                                               const std::vector<std::string> &candidates)>;

        DisambiguationCallback() = default;

        /// Any callable that can serve `Body` binds here. The constraint keeps this constructor from
        /// out-competing the copy and move constructors: without it, a `DisambiguationCallback&` would
        /// bind to this template and fail inside `std::function`.
        template <class Callable,
                  class = std::enable_if_t<!std::is_same_v<std::decay_t<Callable>, DisambiguationCallback> &&
                                           std::is_invocable_r_v<std::string, Callable &, std::string_view, size_t,
                                                                 std::string_view, const std::vector<std::string> &>>>
        DisambiguationCallback(Callable &&body) : m_body(std::forward<Callable>(body)) {}

        /// False for a default-constructed callback: the engine then does no conversion at all.
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_body); }

        std::string operator()(std::string_view text, size_t position, std::string_view hanzi,
                               const std::vector<std::string> &candidates) const {
            return m_body(text, position, hanzi, candidates);
        }

    private:
        Body m_body;
    };
}  // namespace cpp_pinyin::v2
