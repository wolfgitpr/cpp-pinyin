#include <cpp-pinyin2/core/ToneSandhi.h>
#include "c_api_internal.h"

using namespace cppp_detail;

namespace
{
    // The C surface cannot build a `cpp_pinyin::v2::Registry` (nor hand one in -- that parameter is the
    // reason this fallback exists), so the id -> dialect lookups below go through ONE registry owned
    // by this translation unit: a function-local `static const`, i.e. constructed on first use under
    // C++11's thread-safe magic statics, immutable afterwards, and therefore not a mutable global.
    // It carries the built-ins, which is exactly what a C-only host is promised.
    const cpp_pinyin::v2::Registry &builtinRegistry() {
        static const cpp_pinyin::v2::Registry registry;
        return registry;
    }

    // `cppp2_registry` is an incomplete type in C and a `cpp_pinyin::v2::Registry` in fact: the handle a
    // C++ host hands in is the address of its own registry (see c_api.h). NULL means the built-ins.
    const cpp_pinyin::v2::Registry &registryOf(const cppp2_registry *registry) {
        return registry == nullptr ? builtinRegistry() : *reinterpret_cast<const cpp_pinyin::v2::Registry *>(registry);
    }
}  // namespace

// The C enumerators and the C++ `Code` are ONE value domain, and this is where that is enforced
// rather than promised: if either side is renumbered, this file stops compiling. Together with
// cppp2_code_text() calling `codeText` instead of carrying a table, code and wording have exactly one
// authority each (the 2.0 face's shape authority -- now docs/CONTRACT.md; the plan that decided it was
// retired in the 2026-10-09 documentation cut, see docs/HISTORY.md).
static_assert(CPPP2_OK == static_cast<int32_t>(cpp_pinyin::v2::Code::Ok), "CPPP2_OK must be Code::Ok");
static_assert(CPPP2_ERR_INVALID_ARG == static_cast<int32_t>(cpp_pinyin::v2::Code::InvalidArgument),
              "CPPP2_ERR_INVALID_ARG must be Code::InvalidArgument");
static_assert(CPPP2_ERR_NOT_READY == static_cast<int32_t>(cpp_pinyin::v2::Code::NotReady),
              "CPPP2_ERR_NOT_READY must be Code::NotReady");
static_assert(CPPP2_ERR_LOAD == static_cast<int32_t>(cpp_pinyin::v2::Code::Load), "CPPP2_ERR_LOAD must be Code::Load");
static_assert(CPPP2_ERR_INTERNAL == static_cast<int32_t>(cpp_pinyin::v2::Code::Internal),
              "CPPP2_ERR_INTERNAL must be Code::Internal");
static_assert(CPPP2_ERR_DICT_INCOMPATIBLE == static_cast<int32_t>(cpp_pinyin::v2::Code::DictIncompatible),
              "CPPP2_ERR_DICT_INCOMPATIBLE must be Code::DictIncompatible");

// `cppp2_syllable`'s two value domains, pinned the same way and for the same reason: a C shell reads
// `tone_kind` / `source` as plain integers, so a renumbering on the C++ side would otherwise be a
// silently wrong answer instead of a build failure.
static_assert(CPPP2_TONE_KIND_UNSPECIFIED == static_cast<int32_t>(cpp_pinyin::v2::ToneKind::unspecified),
              "CPPP2_TONE_KIND_UNSPECIFIED must be ToneKind::unspecified");
static_assert(CPPP2_TONE_KIND_BASE == static_cast<int32_t>(cpp_pinyin::v2::ToneKind::base),
              "CPPP2_TONE_KIND_BASE must be ToneKind::base");
static_assert(CPPP2_TONE_KIND_SANDHI == static_cast<int32_t>(cpp_pinyin::v2::ToneKind::sandhi),
              "CPPP2_TONE_KIND_SANDHI must be ToneKind::sandhi");
static_assert(CPPP2_SOURCE_DICTIONARY == static_cast<int32_t>(cpp_pinyin::v2::ReadingSource::dictionary),
              "CPPP2_SOURCE_DICTIONARY must be ReadingSource::dictionary");
static_assert(CPPP2_SOURCE_PHRASE == static_cast<int32_t>(cpp_pinyin::v2::ReadingSource::phrase),
              "CPPP2_SOURCE_PHRASE must be ReadingSource::phrase");
static_assert(CPPP2_SOURCE_USER_DICT == static_cast<int32_t>(cpp_pinyin::v2::ReadingSource::user_dict),
              "CPPP2_SOURCE_USER_DICT must be ReadingSource::user_dict");
static_assert(CPPP2_SOURCE_PLUGIN == static_cast<int32_t>(cpp_pinyin::v2::ReadingSource::plugin),
              "CPPP2_SOURCE_PLUGIN must be ReadingSource::plugin");

// The query-opts layout the comment beside `reserved_0` (c_api.h) describes, pinned as ADJACENCY rather
// than absolute byte numbers: those differ between a 32-bit build (size_t is 4) and LP64, which is how
// that comment came to quote the wrong offsets twice (2026-10-20, audit round 3 H07). What must stay
// true is the claim the field makes: `include_reading` sits exactly where the next append was supposed
// to go, and `reserved_1` is the one field after it.
static_assert(offsetof(cppp2_query_opts, include_reading) == offsetof(cppp2_query_opts, reserved_0) + sizeof(int32_t),
              "include_reading must be appended immediately after reserved_0");
static_assert(offsetof(cppp2_query_opts, reserved_1) == offsetof(cppp2_query_opts, include_reading) + sizeof(int32_t),
              "reserved_1 must directly follow include_reading");
static_assert(offsetof(cppp2_query_opts, reserved_0) > offsetof(cppp2_query_opts, mark_neutral_as_5),
              "reserved_0 must come after the switches it appends to");

// Engine lifetime, dialect enumeration, query options.
extern "C" {
const char *cppp2_version(void) { return CPP_PINYIN2_VERSION_STRING; }

const char *cppp2_code_text(int32_t code) {
    // A call, not a copy: the wording lives in one place (cpp_pinyin::v2::codeText, src/Outcome.cpp),
    // and an out-of-domain value is answered by that function too ("unknown code") instead of
    // falling off the end of a switch here.
    return cpp_pinyin::v2::codeText(static_cast<cpp_pinyin::v2::Code>(code));
}

void cppp2_string_free(char *s) { delete[] s; }

void cppp2_string_list_free(char **list, size_t count) { free_list(list, count); }

cppp2_engine *cppp2_open(const cppp2_registry *registry, const char *dialect_id, const char *dict_dir,
                         cppp2_error *err) {
    if (err != nullptr) {
        err->code = CPPP2_OK;
        err->detail = nullptr;
    }

    // Every failure path returns nullptr, so the lambda only fills the channel. `detail` is the
    // occurrence's message and belongs to the caller (cppp2_string_free); a failed allocation leaves
    // it NULL rather than returning a message we do not own.
    //
    // WHY `const char *` AND NOT `const std::string &` (2026-10-20, audit round 2 §12.6 C6). The literal
    // messages below are longer than any small-string buffer (the `dict_dir` one is 55 bytes), so
    // passing them by value would allocate a `std::string` BEFORE entering this lambda -- and two of the
    // calls happen inside `catch` handlers, where `std::bad_alloc` cannot propagate: it would call
    // `std::terminate` and break the one promise `c_api.h:5` makes ("no exception crosses the boundary").
    // Taking a pointer keeps the whole failure path allocation-free in its own construction; `dup` is the
    // only place that allocates, and it already handles its own failure.
    auto fail = [err](int32_t code, const char *detail) -> cppp2_engine *
    {
        if (err != nullptr) {
            err->code = code;
            err->detail = dup(detail);
        }
        return nullptr;
    };

    if (dialect_id == nullptr || dialect_id[0] == '\0')
        return fail(CPPP2_ERR_INVALID_ARG, "dialect_id is required");
    if (dict_dir == nullptr || dict_dir[0] == '\0')
        return fail(CPPP2_ERR_INVALID_ARG, "dict_dir is required (this library has no default path)");

    try {
        const cpp_pinyin::v2::Registry &reg = registryOf(registry);
        cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
            cpp_pinyin::v2::Engine::open(reg, std::filesystem::u8path(dict_dir), dialect_id);
        if (!opened.has_value()) {
            // Both the MESSAGE and the CODE are the engine's own since S11a: `Engine::open` is the
            // single producer of the wording AND of the classification (an id the registry does not
            // know is NotReady, a dictionary that would not load is Load) -- the C face forwards
            // `Error` instead of re-asking the registry for the same judgement. `c_api.h`'s promise
            // ("NotReady names the id", "a dictionary that would not load is LOAD") is what this
            // forwarding keeps true, and the static_asserts above keep the two value domains one.
            const cpp_pinyin::v2::Error &error = opened.error();
            // `error.detail` is a std::string: its `c_str()` is valid for the duration of this call, and
            // `dup` copies into C memory before anything can invalidate it.
            return fail(static_cast<int32_t>(error.code), error.detail.c_str());
        }
        auto *handle = new (std::nothrow) cppp2_engine{std::move(opened).value()};
        if (handle == nullptr)
            return fail(CPPP2_ERR_INTERNAL, "out of memory");
        return handle;
    }
    catch (const std::exception &e) {
        // `e.what()` is a pointer into the exception object, which outlives this handler.
        return fail(CPPP2_ERR_INTERNAL, e.what());
    }
    catch (...) {
        return fail(CPPP2_ERR_INTERNAL, "unknown exception caught at the C boundary");
    }
}

void cppp2_destroy(cppp2_engine *engine) { delete engine; }

int32_t cppp2_dialect_ids(const cppp2_registry *registry, char ***out_ids, size_t *out_count) {
    if (out_ids == nullptr || out_count == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_ids = nullptr;
    *out_count = 0;
    try {
        const std::vector<std::string> ids = registryOf(registry).ids();
        char **list = dup_list(ids, out_count);
        if (list == nullptr && !ids.empty())
            return CPPP2_ERR_INTERNAL;
        *out_ids = list;
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

int32_t cppp2_style_list(const cppp2_engine *engine, cppp2_style **out_styles, size_t *out_count) {
    if (out_styles == nullptr || out_count == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    *out_styles = nullptr;
    *out_count = 0;
    if (engine == nullptr || engine->engine.dialect().id.empty())
        return CPPP2_ERR_NOT_READY;
    try {
        // The dialect's own table, verbatim: the C face must not keep a second copy of it.
        const auto &declared = engine->engine.dialect().styles;
        cppp2_style *rows = nullptr;
        if (!declared.empty()) {
            // Value-initialised so a rollback can free every slot safely, and `nothrow` so the failure
            // is this function's decision rather than a throw two frames away.
            rows = new (std::nothrow) cppp2_style[declared.size()]();
            if (rows == nullptr)
                return CPPP2_ERR_INTERNAL;
            for (size_t i = 0; i < declared.size(); ++i) {
                rows[i].name = dup(declared[i].name);
                // A row whose name could not be copied is not a row this API may return (the field is
                // "the dialect's own name for this style"), so the whole list is rolled back instead of
                // handed over with a hole in it -- 2026-10-20, the residual of audit H43 after the
                // result-construction sites turned out to be checked already.
                if (rows[i].name == nullptr) {
                    cppp2_style_list_free(rows, declared.size());
                    return CPPP2_ERR_INTERNAL;
                }
                rows[i].alias = declared[i].alias;
                rows[i].is_default = declared[i].isDefault ? 1 : 0;
            }
        }
        *out_styles = rows;
        *out_count = declared.size();
        return CPPP2_OK;
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

void cppp2_style_list_free(cppp2_style *styles, size_t count) {
    if (styles == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        delete[] styles[i].name;
    delete[] styles;
}

// --------------------------------------------------------------------------- queries ---

void cppp2_query_opts_init(cppp2_query_opts *opts) {
    if (opts == nullptr)
        return;
    // A caller-owned struct may be *smaller* than this header's declaration (a shell built against an
    // older c_api.h). Writing past what the caller owns is heap corruption in the caller's process,
    // not an error we could report, so the fields are gated on the size the caller declared -- the
    // same rule the read path uses.
    //
    // `struct_size` is read before it can be known to be meaningful, so only a *plausible* prefix is
    // honoured: at least a size_t (the struct starts with one) and smaller than this declaration.
    // Anything else -- 0 from a fresh struct, a stack garbage word, or a size from a newer header --
    // means "initialise everything I know", which is exactly what this function did before the gate
    // existed (tests/c_api_smoke.c pins both halves of that behaviour).
    const size_t declared = opts->struct_size;
    const bool prefix = (declared >= sizeof(size_t)) && (declared < sizeof(cppp2_query_opts));
    if (!prefix)
        opts->struct_size = sizeof(cppp2_query_opts);
    if (CPPP_HAS_FIELD(opts, style))
        // -1 is the sentinel "whatever this dialect defaults to" (core/Types.h `resolveStyle`), NOT
        // "style 0". Zero used to be the default here and it meant NORMAL, so a C caller and the
        // Mandarin facade asked for different renderings of the same sentence with an empty config --
        // the four-layer disagreement C4/N27 removed. The sentinel is now the ONE default, C++/C/
        // Python/C# alike, and a dialect that declares no styles still falls back to 0.
        opts->style = -1;
    if (CPPP_HAS_FIELD(opts, on_error))
        opts->on_error = 0;
    if (CPPP_HAS_FIELD(opts, include_candidates))
        opts->include_candidates = 1;
    if (CPPP_HAS_FIELD(opts, v_to_u))
        opts->v_to_u = 0;
    if (CPPP_HAS_FIELD(opts, mark_neutral_as_5))
        opts->mark_neutral_as_5 = 0;
    // The appended field (S9a-2). Default 0 = "do not build the structure", which is the S8 default on
    // the C++ side and what the C surface returned before this field existed.
    if (CPPP_HAS_FIELD(opts, include_reading))
        opts->include_reading = 0;
}

// -------------------------------------------------------------- tone sandhi (post-processing) ---

// The bridge exists because the rules live in ONE place (core/ToneSandhi.h) and are deliberately not
// re-implemented here: writing them a second time in C is how two surfaces start disagreeing about what
// 不是 sounds like. Only the fields the rules read are carried across, and only the field they write
// (`rendered`) is carried back.
// `CPPP_HAS_FIELD` above cannot be reused here: its offsetof names `cppp2_query_opts`, so asking it
// about this struct would be a lie the compiler happens to accept. Same rule, this type, no sharing --
// including the `struct_size == 0` normalisation (c_api.h documents 0 as "exactly this header's struct").
#define CPPP_SANDHI_DECLARED_SIZE(o) ((o)->struct_size == 0 ? sizeof(cppp2_tone_sandhi_opts) : (o)->struct_size)
#define CPPP_SANDHI_HAS_FIELD(o, member)                                                                               \
    (CPPP_SANDHI_DECLARED_SIZE(o) >= offsetof(cppp2_tone_sandhi_opts, member) + sizeof((o)->member))

int32_t cppp2_apply_tone_sandhi(const cppp2_engine *engine, cppp2_result *result, const cppp2_tone_sandhi_opts *opts,
                                size_t *out_changed) {
    if (out_changed != nullptr)
        *out_changed = 0;
    if (result == nullptr)
        return CPPP2_ERR_INVALID_ARG;
    if (!usable(engine))
        return CPPP2_ERR_NOT_READY;
    // The pass is a DIALECT decision, asked here and nowhere else (`Dialect::toneSandhi`, core/ToneSandhi.h).
    // A dialect that declares none answers INVALID_ARG and keeps its rows untouched -- which is the whole
    // reason this entry now takes an engine: until 2026-10-18 it took none, and a Cantonese result would have
    // been rewritten with 不/一 rules that Cantonese does not have (jyutping is letters-plus-a-digit too, so
    // nothing in the data could have told the rules to stop). Any enumerator that is not Mandarin also lands
    // here: a future pass gets its own symbol rather than being guessed at by this one.
    if (engine->engine.dialect().toneSandhi != cpp_pinyin::v2::ToneSandhiPass::Mandarin)
        return CPPP2_ERR_INVALID_ARG;
    try {
        // `Result` wraps its vector (core/Types.h): build the vector, then hand it over.
        std::vector<cpp_pinyin::v2::Segment> built;
        built.reserve(result->count);
        for (size_t i = 0; i < result->count; ++i) {
            const cppp2_segment &row = result->segments[i];
            cpp_pinyin::v2::Segment segment;
            segment.text = (row.text != nullptr) ? row.text : "";
            segment.rendered = (row.rendered != nullptr) ? row.rendered : "";
            // Only "did this row fail" travels: the rules never look at which failure it was, and
            // inventing a finer code here would be a second classification of a failure the C face
            // already classified once when it built the row.
            segment.failure_kind =
                (row.failure_kind == CPPP2_OK) ? cpp_pinyin::v2::Code::Ok : cpp_pinyin::v2::Code::InvalidArgument;
            built.emplace_back(std::move(segment));
        }
        cpp_pinyin::v2::Result rows(std::move(built));

        cpp_pinyin::v2::ToneSandhiOptions options;  // the C++ defaults ARE what `opts == NULL` documents
        if (opts != nullptr) {
            if (CPPP_SANDHI_HAS_FIELD(opts, third_tone))
                options.thirdTone = (opts->third_tone != 0);
            if (CPPP_SANDHI_HAS_FIELD(opts, bu))
                options.bu = (opts->bu != 0);
            if (CPPP_SANDHI_HAS_FIELD(opts, yi))
                options.yi = (opts->yi != 0);
        }

        const size_t changed = cpp_pinyin::v2::applyMandarinToneSandhi(rows, options);

        for (size_t i = 0; i < result->count; ++i) {
            const std::string &now = rows[i].rendered;
            const char *was = result->segments[i].rendered;
            if (was != nullptr && now == was)
                continue;
            // The result OWNS its strings in one block (2026-10-20, audit H10), so a replacement is written into
            // that block's per-segment override slot instead of being `new`ed and the old pointer `delete[]`d:
            // the old pointer is a `c_str()` VIEW into the owner, so deleting it was heap corruption waiting for
            // the first caller to run the sandhi pass (`CppPinyinTests` aborted with 0xc0000374 when it did).
            cppp_detail::ResultOwner *owner = cppp_detail::ownerOf(result);
            owner->renderedOverrides[i] = now;
            result->segments[i].rendered = owner->renderedOverrides[i].c_str();
        }
        if (out_changed != nullptr)
            *out_changed = changed;
        return CPPP2_OK;  // a status, not the count: `out_changed` carries the count
    }
    catch (...) {
        return CPPP2_ERR_INTERNAL;
    }
}

#undef CPPP_SANDHI_HAS_FIELD

#undef CPPP_HAS_FIELD

}  // extern "C"
