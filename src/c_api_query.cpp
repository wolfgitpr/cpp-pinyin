#include "Utf8Conv.h"
#include "c_api_internal.h"

#include <algorithm>

#include "Capabilities.h"
#include "toneUtil/ReadingCodec.h"

#include <cpp-pinyin2/core/U16Str.h>

using namespace cppp_detail;

// Query, result ownership, dictionary loaders, single-syllable rendering.
extern "C" {
int32_t cppp2_query(const cppp2_engine *engine, const char *text, const cppp2_query_opts *opts,
                    cppp2_result **out_result) {
    if (out_result == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_result = nullptr;
    if (engine == nullptr || text == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        // The options are decoded ONCE: the structure switch below has to be the one the engine was
        // actually asked for. Inferring it from the built rows could not tell "not requested"
        // from "this row has no structure to describe", and those two are different answers.
        const cpp_pinyin::v2::EngineConfig cfg = config_of(opts);
        // `Engine::query` classifies its own failure since S11a, and the C face forwards the code
        // instead of inventing one: the readiness check above is the fast path, this is the same
        // answer for the same state (the two can only disagree if a future query failure gets a
        // different code, and then this line is what carries it).
        const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Result> outcome =
            engine->engine.query(std::string_view(text), cfg);
        if (!outcome.has_value())
            return static_cast<int32_t>(outcome.error().code);
        cppp2_result *out = make_result(outcome.value(), cfg.includeReading);
        if (out == nullptr)
            return CPPP2_ERR_INTERNAL;
        *out_result = out;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

// The one pair for a result. Every string the result owns is released here -- per segment: `text`,
// `rendered`, the syllable array and each syllable's two strings, the candidate array and each
// candidate -- so a caller frees the whole conversion with ONE call and never has to walk it.
void cppp2_result_free(cppp2_result *result) {
    if (result == nullptr)
        return;
    // ONE deletion releases the strings, the segment array and both nested arrays: they are members of the
    // owner block the public pointer addresses (2026-10-20, audit H10). Nothing is freed one pointer at a
    // time any more, so no `const_cast` is needed to express ownership -- `ResultOwner` says it in its shape.
    auto *owner = reinterpret_cast<cppp_detail::ResultOwner *>(result);
    delete owner;
}

// One structured syllable -> the string its own dialect renders it as. The whole point is that the
// STYLE stays in the library (I7): a shell that needs this string asks here instead of re-implementing
// tone marks, jyutping digits or bopomofo.
int32_t cppp2_render_syllable(const cppp2_engine *engine, const cppp2_syllable *syllable, const cppp2_query_opts *opts,
                              char **out_text) {
    if (out_text == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_text = nullptr;
    if (engine == nullptr || syllable == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        if (syllable->initial == nullptr || syllable->fin == nullptr)
            return CPPP2_ERR_INVALID_ARG;
        // The single-syllable contract, checked rather than assumed (plan A2): one syllable never
        // carries a separator, so a structure that does is a multi-syllable string in the wrong field.
        if (hasSeparator(syllable->initial) || hasSeparator(syllable->fin))
            return CPPP2_ERR_INVALID_ARG;

        const cpp_pinyin::v2::EngineConfig cfg = config_of(opts);
        // The dialect is what makes the bytes right: the notation says how its tables spell a tone,
        // and its own converter is the one that renders a style. Neither is recoverable from the
        // structure (which deliberately carries no dialect), which is why this entry takes the handle.
        const cpp_pinyin::v2::Dialect &dialect = engine->engine.dialect();
        const cpp_pinyin::v2::detail::ToneNotation notation = cpp_pinyin::v2::detail::notationOf(dialect.tone);

        cpp_pinyin::v2::Syllable data;
        data.initial = syllable->initial;
        data.fin = syllable->fin;
        if (syllable->spelling != nullptr)
            data.spelling = syllable->spelling;
        data.tone = syllable->tone;
        data.tone_kind = static_cast<cpp_pinyin::v2::ToneKind>(syllable->tone_kind);
        data.source = static_cast<cpp_pinyin::v2::ReadingSource>(syllable->source);

        // The spelling is the row's IDENTITY: with it the surface needs no rebuilding, and the
        // projection's blind spot (readings `surfaceOf` cannot invert) stops being an error here. When
        // the caller has only the two halves, the old path stays: rebuild, and a surface that cannot be
        // rebuilt is genuinely a bad argument, because nothing could be rendered from it.
        const std::u16string spelling16 = cpp_pinyin::v2::toU16(data.spelling);
        const std::u16string surface =
            spelling16.empty() ? cpp_pinyin::v2::detail::surfaceOf(data, notation) : spelling16;
        if (surface.empty())
            return CPPP2_ERR_INVALID_ARG;

        // v_to_u / mark_neutral_as_5 are masked by the dialect's capability flags, exactly as the
        // query path masks them: a switch a dialect does not support is ignored, never reinterpreted,
        // and this entry must not be the one place that disagrees. The mask itself is
        // `detail::effectiveCaps` -- the ONE definition -- not a fifth hand-written copy of the same
        // expression (2026-10-20, audit round 3 H50: the copy here is why `Capabilities.h`'s "written
        // out four times" claim was false). The style goes through `resolveStyle` for the same reason:
        // this entry renders ONE syllable by hand instead of going through `Engine::query`, and it must
        // not be the place where an empty `cppp2_query_opts` starts meaning something else.
        const cpp_pinyin::v2::detail::EffectiveCaps caps = cpp_pinyin::v2::detail::effectiveCaps(dialect, cfg);
        const std::u16string rendered = dialect.tone.convert(
            surface, cpp_pinyin::v2::resolveStyle(cfg.style, cpp_pinyin::v2::defaultStyleOf(dialect.styles)), caps.vToU,
            caps.neutralTone5);
        // An EMPTY `rendered` is a legal answer and is returned as one: INITIALS of a zero-initial
        // syllable IS "", and reporting that as CPPP2_ERR_INVALID_ARG told the caller its argument was
        // malformed when the library had simply answered. The two empties -- "nothing can be rendered"
        // and "the answer is empty" -- are now told apart by the surface test above.

        char *out = dup(cpp_pinyin::v2::toUtf8(rendered));
        if (out == nullptr)
            return CPPP2_ERR_INTERNAL;
        *out_text = out;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

int32_t cppp2_readings(const cppp2_engine *engine, const char *hanzi, const cppp2_query_opts *opts,
                       char ***out_readings, size_t *out_count) {
    if (out_readings == nullptr || out_count == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_readings = nullptr;
    *out_count = 0;
    if (engine == nullptr || hanzi == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        // The SAME failure forwarding `cppp2_query` does, one entry point over: `Engine::readings`
        // returns an `Outcome` (decision N17, landed in batch B4 -- docs/CONTRACT.md section 2).
        // Neither failure producer is reachable through a handle `cppp2_open` publishes -- the reader
        // guard above covers the moved-from facade, and an engine that never became ready is not
        // published at all (c_api.h says the same of NOT_READY) -- so this is classification, not a bug
        // report: letting `value()` throw a `bad_variant_access` would have the catch below report
        // INTERNAL, a code that names the wrong failure should a producer ever appear.
        const cpp_pinyin::v2::Outcome<std::vector<std::string>> outcome =
            engine->engine.readings(std::string_view(hanzi), config_of(opts));
        if (!outcome.has_value())
            return static_cast<int32_t>(outcome.error().code);
        const std::vector<std::string> &items = outcome.value();
        // An empty input is the one answer with nothing in it, and it is published as NULL with count 0
        // (the shape `cppp2_style_list` uses for "this dialect declares no styles") rather than as an
        // allocated empty array: the caller then has ONE way to test "nothing here" and never has to
        // tell an empty array apart from no array at all.
        char **list = nullptr;
        if (!items.empty())
            list = dup_list(items, out_count);
        if (list == nullptr && !items.empty())
            return CPPP2_ERR_INTERNAL;
        *out_readings = list;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

int32_t cppp2_to_simplified(const cppp2_engine *engine, const char *text, char **out_text) {
    if (out_text == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_text = nullptr;
    if (engine == nullptr || text == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        const auto converted = engine->engine.toSimplified(std::string_view(text));
        if (!converted.has_value())
            return static_cast<int32_t>(converted.error().code);
        char *out = dup(converted.value());
        if (out == nullptr)
            return CPPP2_ERR_INTERNAL;
        *out_text = out;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

int32_t cppp2_tone_convert(const cppp2_engine *engine, const char *reading, int32_t style, char **out_text) {
    if (out_text == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_text = nullptr;
    if (engine == nullptr || reading == nullptr || reading[0] == '\0')
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        // The dialect decides both halves: how a reading is spelled coming in (`normalizeReading`) and what each
        // style looks like going out (`convert`). A separator means the caller passed more than one reading, and
        // this entry is documented as one -- splitting it here would silently turn one bad argument into N good
        // ones, which is the query path's job, not this one's.
        if (hasSeparator(reading))
            return CPPP2_ERR_INVALID_ARG;
        const cpp_pinyin::v2::Dialect &dialect = engine->engine.dialect();
        // The ACCEPTED input form is tone3: letters (the u-diaeresis spelling included) and one trailing tone digit
        // -- the form user dictionaries are written in and `normalizeReading` is documented to take. Everything else
        // is refused HERE rather than handed to the converter, which does not reject it: measured 2026-10-21,
        // "ni" came back "nii" and "nǐ3" came back "n3i". Checked on the RAW input, before normalization turns it
        // into the tone-marked form the engine stores.
        const std::u16string input = cpp_pinyin::v2::toU16(reading);
        // ONE trailing tone DIGIT -- not "0 to 5". The narrow range was Mandarin's, and Cantonese writes tones
        // 1-6, so every `jyut6`-style reading was refused here (measured 2026-10-22: 19,483 digit-bearing lines
        // in cantonese/word.txt, and the tone-6 ones all came back INVALID_ARG). Whether the digit means
        // anything is the dialect's question; this check only refuses a shape no stored reading has.
        if (input.size() < 2 || input.back() < u'0' || input.back() > u'9')
            return CPPP2_ERR_INVALID_ARG;
        for (std::size_t i = 0; i + 1 < input.size(); ++i) {
            const char16_t unit = input[i];
            const bool letter =
                (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || unit == 0x00FC || unit == 0x00DC;
            if (!letter)
                return CPPP2_ERR_INVALID_ARG;
        }
        const std::u16string internal = dialect.tone.normalizeReading(input);
        if (internal.empty())
            return CPPP2_ERR_INVALID_ARG;
        // -1 is the sentinel "whatever this dialect defaults to", and it must become a number HERE too: the
        // sentinel used to reach `convert` unresolved and then fail the `declared` test below, so the default
        // argument of the whole feature -- what `cppp2_query_opts_init` writes and what both shells pass when
        // the caller names no style -- returned the caller's own string with CPPP2_OK. Measured 2026-10-22:
        // `cppp2_tone_convert(h, "ni3", -1, &text)` answered "ni3". `resolveStyle` is the ONE place the header
        // (`c_api.h:198-199`) says turns the sentinel into a number, and every other entry already calls it.
        const int styleIndex = cpp_pinyin::v2::resolveStyle(style, cpp_pinyin::v2::defaultStyleOf(dialect.styles));
        // vToU and neutralTone5 are OFF on purpose (see the header): this entry converts STYLES. A style the
        // dialect does not fill comes back unchanged, which `convert` guarantees -- so no error code is invented
        // for it here.
        const std::u16string rendered = dialect.tone.convert(internal, styleIndex, false, false);
        // "Unchanged" must mean the CALLER's string, so a style this dialect does not DECLARE returns the input as
        // written -- not `convert`'s pass-through, which hands back its own input (the normalized, tone-marked
        // reading) and would turn "ni3" into "nǐ" while reporting success. Asked of the dialect's style table, the
        // same authority `cppp2_style_list` publishes, rather than guessed from `rendered == internal`: that guess
        // is wrong for exactly the interesting case, where the target style IS the stored form (TONE of "nǐ").
        const bool declared =
            std::any_of(dialect.styles.begin(), dialect.styles.end(),
                        [styleIndex](const cpp_pinyin::v2::StyleDecl &decl) { return decl.alias == styleIndex; });
        const std::u16string answer = declared ? rendered : input;
        char *out = dup(cpp_pinyin::v2::toUtf8(answer));
        if (out == nullptr)
            return CPPP2_ERR_INTERNAL;
        *out_text = out;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

int32_t cppp2_is_polyphonic(const cppp2_engine *engine, const char *hanzi, int32_t *out_is_polyphonic) {
    if (out_is_polyphonic == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_is_polyphonic = 0;
    if (engine == nullptr || hanzi == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    try {
        if (!usable(engine))
            return CPPP2_ERR_NOT_READY;
        *out_is_polyphonic = engine->engine.isPolyphonic(std::string_view(hanzi)) ? 1 : 0;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

// ---------------------------------------------------------------------- dictionaries ---

namespace
{
    /// Which table a load entry names. Both loads are the same shape -- validate, load, classify -- and keeping
    /// ONE body means the two spellings of each (with and without the error channel) cannot drift apart about the
    /// CODE, which is what a host branches on (2026-10-20, audit H04).
    enum class LoadedTable { user, traditional };

    int32_t loadThrough(cppp2_engine *engine, const char *path, int32_t clear_existing, LoadedTable which,
                        cppp2_error *err) {
        if (err != nullptr) {
            err->code = CPPP2_OK;
            err->detail = nullptr;
        }
        // Fills the channel when there is one; the historical spelling passes NULL and only gets the code.
        const auto report = [err](const cpp_pinyin::v2::Error &error)
        {
            if (err != nullptr) {
                err->code = static_cast<int32_t>(error.code);
                err->detail = dup(error.detail);
            }
            return static_cast<int32_t>(error.code);
        };
        if (engine == nullptr || path == nullptr)
            return report(cpp_pinyin::v2::Error{cpp_pinyin::v2::Code::InvalidArgument, "engine and path are required"});
        try {
            if (!usable(engine))
                return report(cpp_pinyin::v2::Error{cpp_pinyin::v2::Code::NotReady,
                                                    "the handle is not usable (a published handle never is)"});
            // NOT a ternary on the Outcome: `Outcome<void>` converts in a boolean context, so
            // `... ? CPPP2_OK : CPPP2_ERR_LOAD` still COMPILES and silently reports every failure as
            // LOAD -- the one distinction this signature family exists for. Read the code instead.
            const auto loaded = which == LoadedTable::user
                ? engine->engine.loadUserDict(std::filesystem::u8path(path), clear_existing != 0)
                : engine->engine.loadTraditionalDict(std::filesystem::u8path(path), clear_existing != 0);
            if (!loaded.has_value())
                return report(loaded.error());
            return CPPP2_OK;
        }
        catch (...) {
            return report(cpp_pinyin::v2::Error{cpp_pinyin::v2::Code::Internal, {}});
        }
    }
}  // namespace

int32_t cppp2_load_user_dict(cppp2_engine *engine, const char *path, int32_t clear_existing) {
    return loadThrough(engine, path, clear_existing, LoadedTable::user, nullptr);
}

int32_t cppp2_load_traditional_dict(cppp2_engine *engine, const char *path, int32_t clear_existing) {
    return loadThrough(engine, path, clear_existing, LoadedTable::traditional, nullptr);
}

int32_t cppp2_load_user_dict_ex(cppp2_engine *engine, const char *path, int32_t clear_existing, cppp2_error *err) {
    return loadThrough(engine, path, clear_existing, LoadedTable::user, err);
}

int32_t cppp2_load_traditional_dict_ex(cppp2_engine *engine, const char *path, int32_t clear_existing,
                                       cppp2_error *err) {
    return loadThrough(engine, path, clear_existing, LoadedTable::traditional, err);
}

}  // extern "C"
