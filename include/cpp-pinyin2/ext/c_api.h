/*
 * cpp-pinyin2 C ABI -- a flat `extern "C"` surface over `cpp_pinyin::v2::Engine` for callers that cannot use C++
 * headers: C, and the C#/Python FFI shells (closes ISS-005; `docs/DECISIONS.md` §2.1 6.1). Contract:
 * opaque paired handle; UTF-8 in and out; every string this library hands out is freed BY THIS LIBRARY -- your own
 * CRT's free() is heap corruption on Windows, not a leak; no exception crosses the boundary; `int32_t`, never C++
 * `bool`; versioned structs carry `struct_size` and grow by APPEND after the WHOLE old `sizeof`; `dict_dir` is always
 * an argument (no implicit path, no global state). ABI: unreleased, so no compatibility promise yet; already frozen is
 * the SYMBOL SET (`tests/abi/expected_c_api_symbols.txt`, gated by `tests/abi/check_exports.cmake`: a function added
 * here is added to that list in the same commit). `cppp2_version()` reports PROJECT_VERSION, NOT an ABI version --
 * compatibility is answered by the symbol list (written contract: doc 6.8.5.1). THREADS: stated once on
 * `cpp_pinyin::v2::Engine` (include/cpp-pinyin2/core/Engine.h) -- after `cppp2_open` any number of threads may call the
 * read functions on one handle, while `cppp2_load_user_dict` / `cppp2_load_traditional_dict` must NOT overlap a read.
 *
 * WHAT IS DELIBERATELY NOT HERE: the disambiguation callback.
 * `Engine::setDisambiguationCallback` takes a `std::function`, and a C caller cannot construct one -- a C-visible
 * version would be a function-pointer-plus- user-data pair with its own lifetime rules, i.e. a new contract rather than
 * a spelling of the existing one. The capability table in docs/CONTRACT.md lists callbacks as C++-only for that reason.
 * If a host needs it, the shape to add is an APPEND (a `_cb` setter plus a callback struct under the struct_size rule),
 * not a change to these symbols.
 */

// Why a macro guard here and `#pragma once` in the other 14 public headers:
// this is the one public header compiled by C as well as C++. `#pragma once` is an unstandardised
// extension, so a strict C compiler is entitled to ignore it -- the include-guard macro is the portable
// spelling, and this file is the one a C translation unit includes. It is also why the surrounding block
// is `#ifdef __cplusplus` rather than the C++-only spellings used elsewhere in include/.
#ifndef CPP_PINYIN2_C_API_H
#define CPP_PINYIN2_C_API_H

#include <stddef.h>
#include <stdint.h>

#include <cpp-pinyin2/core/PinyinGlobal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- status codes --- */

/*
 * Status codes -- the C face of `cpp_pinyin::v2::Code` and ONE value domain with it (these are that enum's
 * numbers; the implementation pins the pairs together at compile time). Deliberately no `Unsupported`: it
 * had no producer and no consumer -- "a dialect does not support this" is the contract instead (the input
 * comes back unchanged). APPEND only, so a shell keeps reading the same failures after an upgrade; the text
 * of a code is `cppp2_code_text`; switch on CPPP2_OK, never on "a code I have not seen".
 */
enum {
    CPPP2_OK = 0,
    /* A required argument is NULL (out-parameters, `out_count` included, count as required). EMPTY is a
     * different matter: an empty `text` is a legal empty result, not an error -- only `cppp2_open`'s
     * dialect_id/dict_dir and `cppp2_tone_convert`'s reading treat an empty string as invalid, because
     * there is no handle or no reading to work from. Two further producers the caller can decide itself:
     * cppp2_query for a span it cannot read (a cppp2_segment.failure_kind), cppp2_render_syllable for a
     * structure that is not one syllable.
     *
     * NULL HANDLES ARE *NOT* UNIFORM: `cppp2_style_list` answers CPPP2_ERR_NOT_READY for a NULL handle
     * (it asks the handle for its dialect's table, which a NULL handle cannot supply), while the query
     * entries answer INVALID_ARG. Historical shape, documented here rather than silently unified. */
    CPPP2_ERR_INVALID_ARG = 1,
    /* The handle exists but is not usable, or (from cppp2_open) the id is not in the registry that was
     * passed -- an unknown id is never resolved to another dialect, and `err->detail` names it.
     *
     * cppp2_open never publishes a not-ready handle, so this code is not observable through a handle it
     * returned -- which is why there is no readiness probe in this surface (see cppp2_destroy). */
    CPPP2_ERR_NOT_READY = 2,
    /* The dictionary path was fine but loading failed (missing/unreadable file). Returned by
     * cppp2_open when the id resolved, and by the two loaders. */
    CPPP2_ERR_LOAD = 3,
    /* An exception was caught at the boundary, or an allocation failed while marshalling. There is
     * no message channel on the read functions, so the status IS the report. */
    CPPP2_ERR_INTERNAL = 4,
    /* The dictionary directory was read and is intact, but it declares a `min_library` newer than this
     * library (`data/dialects/<dialect>/dialect.json`). Appended 2026-10-18; distinguishable from
     * CPPP2_ERR_LOAD on purpose -- a host can act on this one by upgrading the library, and a MISSING
     * `min_library` means "no requirement" rather than a failure. */
    CPPP2_ERR_DICT_INCOMPATIBLE = 5
};

/* The text of a status code from this header (including a `cppp2_error::code` a call wrote back): a static
 * string, never NULL, never freed by the caller, and valid even after every handle is destroyed. The
 * wording has one authority, the library's `cpp_pinyin::v2::codeText`; a value outside the domain above is
 * reported as unknown rather than aborting. */
CPP_PINYIN2_EXPORT const char *cppp2_code_text(int32_t code);

/* The error channel of the entry points: `code` is what a program branches on (numerically a
 * `cpp_pinyin::v2::Code`) and its text is `cppp2_code_text(code)`; `detail` is THIS occurrence's message and
 * belongs to the CALLER -- free it with `cppp2_string_free` -- and is NULL both on success and when the
 * library can only classify the failure. A function that takes a `cppp2_error *` writes BOTH fields before
 * returning, so nothing needs pre-initialising; passing NULL is allowed when only the status matters. */
typedef struct cppp2_error {
    int32_t code;
    int32_t reserved_0; /* S9c: claims the compiler's 4-byte hole (LP64 offsets 4..7) so a future field
                         * can be appended WITHOUT changing sizeof; see the GROWTH RULE section above. */
    char *detail;
} cppp2_error;

/* An opaque handle to a dialect registry: only a C++ host can produce one (it passes the address of its own
 * `cpp_pinyin::v2::Registry`), which is what lets a C-facing caller reach a dialect that host registered.
 * Every entry point that takes one accepts NULL, meaning "the library's own registry": the two built-ins
 * (mandarin, cantonese) and nothing else -- unlike a NULL `cppp2_engine *` / `out_*`, which is
 * CPPP2_ERR_INVALID_ARG. */
typedef struct cppp2_registry cppp2_registry;

/* --------------------------------------------------------------- handle & info --- */

/* Opaque. The definition lives in src/c_api_internal.h (an internal header, not one of the installed
 * public ones) and is deliberately not visible here. */
typedef struct cppp2_engine cppp2_engine;

/* Library version string (static, never freed). */
CPP_PINYIN2_EXPORT const char *cppp2_version(void);

/* The dictionary root THIS BUILD installed to, resolved against the library's own module directory
 * (`<module dir>/../<datadir>/cpp-pinyin2/dict`), or NULL when this build has no such anchor.
 *
 * Static string, never freed -- the same ownership rule as cppp2_version.
 *
 * Why it exists: a host that links this library directly (no CMake package, no .pc file -- a Qt app with the
 * DLL, a Unity/UE plugin, a shell whose language has no CMake story) otherwise has to hard-code a path that
 * is wrong the moment the application is relocated. D2 of section 4.1 in
 * docs/DECISIONS.md section 4.1 records the decision.
 *
 * What it is NOT:
 *   * not a default. `cppp2_open` still requires `dict_dir`; nothing here makes the library load anything;
 *   * not an environment override. Reading `CPP_PINYIN2_DICT_DIR` is the shell's job, by design (the library
 *     has no implicit path);
 *   * not a check. The directory may be absent or hold different tables -- call cppp2_open and let it name
 *     what is wrong, or `cppp2_dict_dir_candidate` would be a second, drifting validator;
 *   * NULL in a STATIC build: there the library is code inside the host, so "this module" would be the host
 *     executable -- exactly the guess the locality rule forbids.
 *
 * Windows note: the path comes back UTF-8, like every other path this surface takes. */
CPP_PINYIN2_EXPORT const char *cppp2_dict_dir_candidate(void);

typedef struct cppp2_style {
    const char *name; /* the dialect's own name for this style: "tone2", "first-letter", ... */
    int32_t alias; /* the number `cppp2_query_opts::style` carries for it */
    int32_t is_default; /* 1 for the dialect's own default, else 0: no C++ bool in the ABI */
} cppp2_style;

/*
 * Fills `*out_styles` / `*out_count` with the engine's OWN style table -- the rows the dialect
 * itself declares (ext/Dialect.h `styles`), so a shell never keeps a second, disagreeing copy.
 * `*out_count` is 0 with `*out_styles` NULL when the dialect declares no styles at all: that is a
 * success, not a failure. Free with `cppp2_style_list_free`.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_style_list(const cppp2_engine *engine, cppp2_style **out_styles, size_t *out_count);

/* Frees the (possibly NULL) array returned by `cppp2_style_list`, names included. */
CPP_PINYIN2_EXPORT void cppp2_style_list_free(cppp2_style *styles, size_t count);
/* Frees a string this library returned. NULL is a no-op. */
CPP_PINYIN2_EXPORT void cppp2_string_free(char *s);

/* Frees the (possibly NULL) array returned by `cppp2_dialect_ids` / `cppp2_readings`. */
CPP_PINYIN2_EXPORT void cppp2_string_list_free(char **list, size_t count);

/* Opens an engine for `dialect_id` -- an id `registry` knows, or a built-in when it is NULL -- loading
 * `dict_dir` plus the dialect's own subdirectory. Both arguments are required: there is no implicit path.
 * NULL on failure, `err` (optional) then saying why: CPPP2_ERR_INVALID_ARG (missing argument),
 * CPPP2_ERR_NOT_READY (an id the registry does not know: NEVER resolved to another dialect, `err->detail`
 * names it), CPPP2_ERR_LOAD (id resolved, dictionary did not load; the message names the files it looked
 * for), CPPP2_ERR_INTERNAL (exception caught at the boundary). On success `err->code` is CPPP2_OK and
 * `err->detail` is NULL. The registry is read only while opening -- registering later changes nothing. */
CPP_PINYIN2_EXPORT cppp2_engine *cppp2_open(const cppp2_registry *registry, const char *dialect_id,
                                            const char *dict_dir, cppp2_error *err);

/* Destroys an engine. NULL is a no-op. Every string it produced is either owned by the caller
 * (`cppp2_string_free` / `cppp2_string_list_free` / `cppp2_result_free`) or invalid after this call:
 * nothing outlives the handle. */
CPP_PINYIN2_EXPORT void cppp2_destroy(cppp2_engine *engine);

/*
 * A handle returned by `cppp2_open` is ALWAYS usable. A dialect the registry does not know and a
 * dictionary that will not load are OPEN failures -- reported through `err`, with no handle published
 * -- not a not-ready engine. That is why there is no `cppp_is_ready` and no `cppp_last_error` here
 * (both were removed before the first release): a per-call failure comes back as that call's status
 * code plus `err`, so a handle-scoped error channel would be a second truth about the same event.
 */

/*
 * The dialect ids `registry` knows, so a shell does not have to hardcode them -- in ascending byte
 * order (the registry's own contract), and the two built-ins when `registry` is NULL.
 * Returns CPPP2_OK with `*out_ids` (an array of `*out_count` strings; NULL with `*out_count` 0 when
 * there is nothing to report) and `*out_count`, or an error status with `*out_ids = NULL` /
 * `*out_count = 0`. Free with `cppp2_string_list_free`.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_dialect_ids(const cppp2_registry *registry, char ***out_ids, size_t *out_count);

/* -------------------------------------------------------------------- queries --- */

/*
 * Per-query options -- CALLER-OWNED, shared by `cppp2_query` and `cppp2_render_syllable` (both see the same style and
 * switches). `struct_size` declares how much of the struct the caller owns: only fields inside it are written or read,
 * so an older, smaller copy is never written past its own memory; 0 means "exactly this header's struct"; pass NULL for
 * the defaults. THE GROWTH RULE: a new option is APPENDED after the WHOLE old `sizeof`, NOT "after the last member" --
 * confusing the two once put an option inside an older declaration's TRAILING PADDING, where a caller's uninitialised
 * bytes were read as that option (S9c); a `reserved_*` member claims such padding first, and fields are never inserted,
 * reordered, removed or resized (measurement: plugin guide 6.2.1). `style` is the dialect's own numbering (0 = NORMAL,
 * the only value every dialect shares); -1 is the sentinel "whatever this dialect defaults to" -- what
 * `cppp2_query_opts_init` writes, turned into a number in exactly one place (`cpp_pinyin::v2::resolveStyle`); a style
 * the dialect does not support yields the input unchanged. A NUMBER and no name on purpose: resolving a name is the
 * library's job -- a C host spelling one out would be free to disagree.
 */
typedef struct cppp2_query_opts {
    size_t struct_size;
    int32_t style;
    int32_t on_error; /* 0 = Default (mark failed syllables), 1 = Drop */
    int32_t include_candidates; /* 0/1 -- candidates are built only when asked for (M4) */
    int32_t v_to_u; /* 0/1, honoured only where the dialect supports it */
    int32_t mark_neutral_as_5; /* 0/1, honoured only where the dialect supports it */
    /* RESERVED: the APPEND POINT (S9c). The switches above and this field are the prefix an older caller
     * declares -- `struct_size` covering exactly these bytes (32 on LP64, 24 on a 32-bit build) -- so a
     * field appended HERE lands in bytes that caller never declared. That is what happened:
     * `include_reading` was appended at this position, and `reserved_1` claims what is left of the
     * declaration's trailing padding. Nothing after this field is padding any more, and this field
     * itself is never read, never written, never reused.
     * (This note has been wrong twice, both times by quoting offsets that were not its own: it first
     * repeated `reserved_1`'s 36..39, then called 32..39 padding after `include_reading` had taken
     * 32..35. The layout asserts in `src/c_api_lifecycle.cpp` now pin the adjacency this note describes
     * on both 32- and 64-bit layouts.) */
    int32_t reserved_0;
    /* 0/1 -- the structured reading, the same switch as `EngineConfig::includeReading` (S8). 0 (the DEFAULT,
     * and what a shorter older struct gets) means "do not build the structure": syllables come back NULL
     * with count 0, which is "not requested", NOT "no reading" (`rendered` carries the reading either way).
     * 1: every non-failed span carries exactly one syllable, a failed span still none. A separate switch
     * from `include_candidates` because the two are independent costs. */
    int32_t include_reading;
    /* RESERVED: claims the trailing padding of THIS declaration (fields end at 36, `sizeof` is 40, so
     * 36..39 is padding that a caller's `struct_size = 40` already covers). Claimed NOW, while 2.0.0 is
     * unreleased and there is therefore nothing to be compatible with: the next option then appends at
     * 40 instead of being placed inside bytes older callers have already declared -- the S9c replay this
     * header's own note below warns about. Never read, never written, never reused. */
    int32_t reserved_1;
} cppp2_query_opts;

CPP_PINYIN2_EXPORT void cppp2_query_opts_init(cppp2_query_opts *opts);

/* WHICH OPTIONS EACH ENTRY HONOURS.
 *
 * The question a binding author actually asks is "does `cppp2_readings` honour `style`, or do I have to do
 * something else?" -- and this header never answered it, so the only way to find out was to read the .cpp.
 * The answer has a structural reason rather than a per-entry list:
 *
 *   All THREE entries that take `const cppp2_query_opts *` -- `cppp2_query`, `cppp2_render_syllable` and
 *   `cppp2_readings` -- honour EVERY field, because exactly one function turns the C struct into an
 *   `EngineConfig`: `config_of` in src/c_api_internal.h. There is no second reader, so the three cannot
 *   drift apart one field at a time.
 *
 * | field                | C++ member                        | default | all three entries read it? |
 * |----------------------|-----------------------------------|---------|----------------------------|
 * | `style`              | `EngineConfig::style`             | -1      | yes |
 * | `on_error`           | `EngineConfig::onError`           | 0       | yes |
 * | `include_candidates` | `EngineConfig::includeCandidates` | 1       | yes |
 * | `v_to_u`             | `EngineConfig::vToU`              | 0       | yes |
 * | `mark_neutral_as_5`  | `EngineConfig::markNeutralAs5`    | 0       | yes |
 * | `include_reading`    | `EngineConfig::includeReading`    | 0       | yes |
 *
 * "Honours every field" is exact only for a caller that declares the whole struct: `config_of` reads a field
 * ONLY when the caller's `struct_size` covers it (`CPPP_HAS_FIELD`, the same handshake the growth rule
 * above describes). A caller built against 2.0.0's first declaration therefore gets the default for
 * `include_reading`, which is what it has always got.
 *
 * The tone-sandhi entry takes a DIFFERENT struct: `cppp2_apply_tone_sandhi` reads
 * `cppp2_tone_sandhi_opts`, whose booleans are its own. A dialect that declares no sandhi pass is refused
 * there with CPPP2_ERR_INVALID_ARG (`Dialect::toneSandhi`, src/c_api_lifecycle.cpp), rather than silently
 * returning the input unchanged -- "this dialect has no pass" and "the pass changed nothing" must not look
 * the same to a caller. That refusal is why the entry takes an engine handle at all.
 *
 * WHAT IT WRITES: each affected span's `rendered`, and nothing else -- `cppp2_syllable::tone` and
 * `spelling` keep the values the query produced. The C++ face does more: `applyMandarinToneSandhi`
 * mirrors the new tone into `Syllable::reading.tone` as well, with the comment "so the two descriptions
 * of one row cannot disagree" (src/ToneSandhi.cpp). The C entry has no such sync, so after a sandhi pass
 * the two faces disagree about the structured tone while agreeing about the text. Stated rather than
 * fixed: syncing it would move bytes, and whether the structure or the rendered string is the subject of
 * the rewrite is a ruling this entry does not get to make on its own.
 *
 * WHAT IT ACCEPTS, said because the type does not say it: `result` must be a result THIS LIBRARY produced
 * -- the pointer `cppp2_query` wrote through `out_result` -- and it must not have been passed to
 * `cppp2_result_free` yet. The entry recovers the owning block from the pointer's address (one
 * `reinterpret_cast`, pinned by a load-bearing `static_assert` in `src/c_api_internal.h`) in order to write
 * the replacement into that block instead of freeing a string the caller may still hold. A `cppp2_result`
 * the caller assembled itself therefore has no owner block behind it and is NOT a supported input; the
 * refusals below cover the one malformed shape a careless caller produces by accident (`count != 0` with a
 * null `segments`, which answers CPPP2_ERR_INVALID_ARG rather than dereferencing it), not every misuse.
 */

/* ------------------------------------------------------- structured readings --- */

/*
 * The two C++ value domains `cppp2_syllable` transcribes: the status codes and the tone/source pair. The numbers ARE
 * those enums' numbers and the implementation pins each pair at compile time (`src/c_api_lifecycle.cpp` static_assert
 * blocks -- `Code`, then `ToneKind`/`ReadingSource`), so a renumbering fails the build; APPEND only. `tone_kind`
 * describes the TABLE ENTRY read, never the rendering: every non-empty reading of a shipped table is `base`
 * (`src/toneUtil/ReadingCodec.cpp`: only the EMPTY reading stays `unspecified`), and `sandhi` is reserved for a reading
 * a tone-change rule produced -- nothing in this repository produces it yet. Output bytes never depend on it. `source`:
 * `dictionary` (word.txt) and `phrase` (`phrases_dict.txt` + `user_dict.txt`, merged into one layer before anything can
 * tell them apart) have producers today; the other two are not implemented (`include/cpp-pinyin2/core/Syllable.h`
 * states this for the C++ face).
 */
enum { CPPP2_TONE_KIND_UNSPECIFIED = 0, CPPP2_TONE_KIND_BASE = 1, CPPP2_TONE_KIND_SANDHI = 2 };
enum { CPPP2_SOURCE_DICTIONARY = 0, CPPP2_SOURCE_PHRASE = 1, CPPP2_SOURCE_USER_DICT = 2, CPPP2_SOURCE_PLUGIN = 3 };

/*
 * A STRUCTURED reading -- data, never a rendering: the parts of ONE syllable, in the dialect's own spelling system (the
 * C face of `cpp_pinyin::v2::Syllable`). NO `struct_size` and no growth rule: the 2.0 face makes NO cross-version
 * compatibility promise at all (no consumers to keep compatible), so a wider element arrives as another field of THIS
 * struct and a consumer rebuilds against the version it links. `tone` is a dialect-internal tone category, NOT
 * comparable across dialects (Mandarin 0..5, Cantonese 1..6): never compare, order or average values from two dialects.
 * It comes from the table's own notation, is never parsed back out of a rendered string, and is filled for EVERY style
 * -- including one whose string carries no tone, because a style decides the string, not the data (authority:
 * `include/cpp-pinyin2/core/Syllable.h`). `initial` is "" for a zero-initial syllable; `fin` is the canonical final (ü
 * as `v`, `uei`/`iou`/`uen` spelled out, `zhi` -> `i`). The pair is a PROJECTION and does not always rebuild what the
 * table stores -- `spelling` is that spelling verbatim and is what to render again (`cppp2_render_syllable` accepts
 * it). ZERO-INITIALIZE before filling it in: every field is read as a value, so stack garbage (`spelling` above all) is
 * read as a string pointer rather than as "absent".
 */
typedef struct cppp2_syllable {
    const char *initial;
    const char *fin;
    const char *spelling; /* the table's own reading bytes: this row's identity; initial/fin come from it */
    int32_t tone;
    int32_t tone_kind; /* CPPP2_TONE_KIND_* above */
    int32_t source; /* CPPP2_SOURCE_* above */
    int32_t reserved_0; /* S9c: claims the trailing hole (LP64 offsets 36..39); append here, not at the
                         * end, so an older consumer's array stride stays correct. */
} cppp2_syllable;

/*
 * ONE input span and what the engine read for it -- the C face of `cpp_pinyin::v2::Segment`, element type of a
 * `cppp2_result`. `rendered` is the ONE rendered string per span, byte for byte what a query has always returned, and
 * equals `text` when the span failed; there is deliberately no rendered field on `cppp2_syllable` -- a second copy of
 * the same string in the same row would be a second truth (shape authority: docs/CONTRACT.md
 * section 3). `failure_kind` is a status code
 * (text: `cppp2_code_text`): CPPP2_OK means "this span has a reading", and a span the library cannot read (not Hanzi,
 * not in the dictionary) is CPPP2_ERR_INVALID_ARG. A span's failure is CLASSIFIED, not explained -- there is no detail
 * field; `failure_kind`, never "is the structure filled", is the failure test. `syllables` is EMPTY (NULL / 0) in two
 * cases the other fields tell apart: it was not requested (`include_reading` == 0, the default -- "not requested", NOT
 * "no reading"), or the span failed (a failed span deliberately does not enter the array). With `include_reading` == 1
 * every non-failed span carries EXACTLY ONE syllable (one span is one syllable), including a span the engine could not
 * fill (a caller-supplied disambiguation callback wrote the reading), which is default-constructed and describes
 * nothing. `candidates` is optional like the structure: strings in the dialect's style, NULL/0 when
 * `include_candidates` is 0, `{text}` for a failed span. STRINGS and not structures on purpose: a handful of readings
 * have a stored spelling their (initial, fin) projection cannot rebuild (`include/cpp-pinyin2/core/Types.h` names the
 * seven), so this column can move later by handing that spelling to a renderer -- which `cppp2_render_syllable`
 * provides.
 */
typedef struct cppp2_segment {
    const char *text; /* the input span, UTF-8 (a run of ASCII letters may be several bytes) */
    const char *rendered; /* the ONE rendered string of this span; equals `text` when it failed */
    int32_t failure_kind; /* CPPP2_OK, or a CPPP2_ERR_* value: see above */
    int32_t reserved_0; /* S9c: claims the hole between `failure_kind` and this pointer (LP64 offsets
                         * 20..23); append here so an older consumer's stride stays correct. */
    const cppp2_syllable *syllables; /* `syllable_count` entries; NULL when not requested / failed */
    size_t syllable_count;
    size_t candidate_count;
    const char *const *candidates; /* `candidate_count` strings; NULL (with count 0) when not
                                    * requested -- check `candidate_count` before dereferencing */
} cppp2_segment;

/* A whole conversion. Free with `cppp2_result_free` -- which releases the segments array and every string reachable
 * from it (`text`, `rendered`, each `syllables[k].initial` / `.fin` and each `candidates[j]`) -- so copy out what you
 * need BEFORE freeing, and do not touch it after. `count` is spans, not input characters (a run of ASCII letters is one
 * span), and shrinks when `on_error` == 1, where failed spans are DROPPED instead of marked. A result is a SNAPSHOT of
 * its own: every byte in it was copied out of the engine, so it stays valid after `cppp2_destroy`; nothing else
 * survives the handle -- every string a caller keeps must be one it copied out of a result (or the static
 * `cppp2_code_text` one). */
typedef struct cppp2_result {
    size_t count;
    // NOT `const cppp2_segment *`: a post-processing step that only rewrites `rendered` (see
    // `cppp2_apply_tone_sandhi`) has to be able to write that one pointer. The strings are still the
    // library's and `rendered` is still `const char *`, so nothing a caller holds becomes writable;
    // widening this member is source-compatible for readers and changes no layout.
    //
    // `count != 0` REQUIRES `segments != NULL`: the two describe one array, and an entry that reads the
    // rows answers CPPP2_ERR_INVALID_ARG for the other shape instead of dereferencing. Both fields are
    // the library's to fill -- a caller reads them and never builds a result by hand (2026-10-27).
    cppp2_segment *segments;
} cppp2_result;

/*
 * The engine's dialect's tone-sandhi pass (变调): a POST-PROCESSING step over an already-converted result, IN
 * PLACE, and deliberately NOT part of `cppp2_query` -- a caller that does not want it pays nothing. Tone
 * sandhi is a property of a DIALECT, so the function asks `Dialect::toneSandhi` (`core/ToneSandhi.h`)
 * instead of assuming; that is what `engine` is for. Today the only declared pass is Mandarin's, and its
 * rules, option meanings and rewrite scope are documented ONCE in `core/ToneSandhi.h` -- not repeated here.
 * `opts` are that pass's switches: NULL means the library's defaults (all three ON, because the C++ struct's
 * defaults ARE that meaning); a non-NULL `opts` is read field by field, gated on `struct_size`, like
 * `cppp2_query_opts`. A second dialect's pass arrives as `cppp2_apply_<dialect>_sandhi` (2.x may add
 * symbols), never as a change to this signature.
 *
 * The RETURN VALUE is a STATUS (`CPPP2_OK` or a CPPP2_ERR_*), NOT the C++ row count -- rows rewritten come
 * back through `out_changed` (may be NULL), because a caller reading the count as a status would see
 * "1 row changed" as CPPP2_ERR_INVALID_ARG.
 *
 * Refusals, each with nothing changed and `*out_changed = 0`: an unusable `engine` answers
 * CPPP2_ERR_NOT_READY, and a dialect that declares NO pass (Cantonese is one) answers
 * CPPP2_ERR_INVALID_ARG. Refusing is the point, not a formality: jyutping is letters-plus-a-tone-digit too,
 * so no shape test can tell it apart from Mandarin, and the 不/一 rules would silently produce readings
 * Cantonese does not have.
 */
typedef struct cppp2_tone_sandhi_opts {
    size_t struct_size; /* sizeof(cppp2_tone_sandhi_opts) as the CALLER's header declares it;
                         * 0 means "exactly this header's struct", the same rule as cppp2_query_opts */
    int32_t third_tone; /* 0/1 -- third-tone runs (你好 -> ni2 hao3) */
    int32_t bu; /* 0/1 -- 不 before a fourth tone (不是 -> bu2 shi4) */
    int32_t yi; /* 0/1 -- 一 before a fourth tone (一定 -> yi2 ding4) */
    /* RESERVED: claims this declaration's TRAILING PADDING (the three fields end at 20, `sizeof` is 24)
     * so a later option cannot be appended into it -- the rule cppp2_query_opts states and S9c measured. */
    int32_t reserved_0;
} cppp2_tone_sandhi_opts;

/* Applies the engine's dialect's tone-sandhi pass to an already-converted result, IN PLACE. See the note above.
 * `engine` is required even though the result already carries its rows: the DIALECT is the only thing that knows
 * whether a pass exists for it. */
CPP_PINYIN2_EXPORT int32_t cppp2_apply_tone_sandhi(const cppp2_engine *engine, cppp2_result *result,
                                                   const cppp2_tone_sandhi_opts *opts, size_t *out_changed);

/*
 * Converts `text` (a UTF-8 sentence). On success `*out_result` is an allocated result and the
 * status is CPPP2_OK. On failure `*out_result` is NULL.
 *
 * A handle that is not ready fails with CPPP2_ERR_NOT_READY instead of returning an empty result:
 * silently converting nothing is the failure mode this library refuses to have.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_query(const cppp2_engine *engine, const char *text, const cppp2_query_opts *opts,
                                       cppp2_result **out_result);

CPP_PINYIN2_EXPORT void cppp2_result_free(cppp2_result *result);

/*
 * Renders ONE structured syllable in `opts`'s style -- the string that row's own style prints, so a shell
 * never implements a style itself (a binding must not own a second, disagreeing spelling). ONE
 * `cppp2_syllable` IS ONE SYLLABLE: a structure carrying a whole multi-syllable string (`fin` = "zhi4 fa2",
 * `initial` = "ni,hao") is refused with CPPP2_ERR_INVALID_ARG, decided on `initial`/`fin` by the separators
 * every surface here uses (whitespace, `,`, `:`, `-`, `'`, `/`; the tables' own are `:` and `,`, and a
 * syllable's spelling contains none).
 *
 * THE DIALECT COMES FROM THE ENGINE HANDLE: rendering needs the dialect's tone notation and its own
 * `ToneConverter`, neither recoverable from `cppp2_syllable` (no dialect field BY DESIGN, docs/CONTRACT.md
 * section 3), and a Cantonese reading rendered by a Mandarin default would produce wrong bytes.
 *
 * NO LENGTH CAP, BY DECISION (2026-10-23): a cap would refuse input that is legal today, and this is a
 * library rather than a service, so the caller owns its own bound. What the library owes the caller is the
 * COST, stated once: a query allocates roughly 200 bytes of result per input byte of Chinese (one `Segment`
 * per character, each carrying a rendered string and up to six candidates), plus one more copy on this C
 * face. Measured row counts (`Benchmark --brief`: 1 MiB of Chinese is 349,540 rows) and the struct sizes in
 * this header give the factor; a 1 MiB input therefore peaks around 100 MB. Size accordingly.
 *
 * FAILURES, every output NULL: CPPP2_ERR_INVALID_ARG for a NULL argument, a not-one-syllable structure, or a
 * spelling that cannot be rebuilt (the (initial, fin) projection is not invertible for the seven readings
 * `include/cpp-pinyin2/core/Types.h` names -- the only copy of that list; measured 2026-10-09: the frozen
 * equivalence corpus contains none of them, so this branch is about the DICTIONARY, not the digest), and the
 * caller keeps the string it has rather than an invented one; CPPP2_ERR_NOT_READY for an unusable handle;
 * CPPP2_ERR_INTERNAL for an allocation failure or an exception caught at the boundary. A style the dialect
 * does not support is not an error: the reading comes back unchanged.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_render_syllable(const cppp2_engine *engine, const cppp2_syllable *syllable,
                                                 const cppp2_query_opts *opts, char **out_text);

/*
 * Every reading of `hanzi` (by definition one character, but a string is accepted) as `*out_count` strings, converted
 * with `opts`'s style and honouring its `v_to_u` / `mark_neutral_as_5` switches exactly as `cppp2_query` does. Free
 * with `cppp2_string_list_free`. The order is the C++ face's (`core/Types.h`, `Segment::candidates`, K9): the dialect's
 * own dictionary order, de-duplicated by the CONVERTED string keeping the FIRST occurrence -- so style and switches
 * decide not only the spellings but how many survive (`ü` and `u` are different strings: two entries that collapse into
 * one with `v_to_u` off stay two with it on). An EMPTY input is a legal empty result, not a failure (the rule
 * `cppp2_query` follows): CPPP2_OK with `*out_readings` NULL / `*out_count` 0 -- the shape `cppp2_style_list` uses for
 * a dialect that declares no styles -- never an allocated empty array. Any NON-empty input yields at least one entry: a
 * character the table does not know answers with ITSELF, exactly as `Segment::candidates` states -- so NULL with a
 * non-zero count never happens.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_readings(const cppp2_engine *engine, const char *hanzi, const cppp2_query_opts *opts,
                                          char ***out_readings, size_t *out_count);

/*
 * Traditional -> simplified, for the FIRST character of `text` -- the C++ API converts one character
 * and this layer does not change that (a longer string is not a sentence-level conversion).
 * RETURNS a status: `CPPP2_OK` on success, an error code otherwise. The RESULT is in `*out_text` --
 * an allocated string the caller frees with `cppp2_string_free`. (This note used to say "returns an
 * allocated string", which reads as if the string were the return value. The status convention here is
 * about the entries that REPORT an outcome: those return `int32_t`. Of the 23 exports, 6 return `void`
 * (`cppp2_string_free`, `cppp2_string_list_free`, `cppp2_style_list_free`, `cppp2_result_free`,
 * `cppp2_destroy`, `cppp2_query_opts_init`) and 4 return a value WITHOUT being a status
 * (`cppp2_open` a handle, `cppp2_version` / `cppp2_dict_dir_candidate` / `cppp2_code_text` a string);
 * the remaining 13 are the `int32_t` status entries. The earlier wording claimed "every entry ... except
 * `cppp2_open`", which was wrong; it then said "three that return a value" and "twenty exports", which
 * went stale when `cppp2_code_text` and the three `_ex` / `tone_convert` entries landed.)
 */
CPP_PINYIN2_EXPORT int32_t cppp2_to_simplified(const cppp2_engine *engine, const char *text, char **out_text);
/* RETURNS a status (`CPPP2_OK` or an error code); the ANSWER is in `*out_text`, allocated with the same
 * allocator the other string entries use, so `cppp2_string_free` releases it.
 *
 * `reading` is ONE reading in this dialect's STORED notation: lowercase letters (the `ü` spelling included) and one
 * trailing tone digit, which is the form `cppp2_readings` and `cppp2_query` with the TONE3 style return --
 * `"ni3"`, `"hao3"`, `"jyut6"`. `style` is the style it should come back in (the same numbers
 * `cppp2_query_opts.style` and `cppp2_style_list` use). The dialect comes from the handle, for the reason
 * `cppp2_render_syllable` gives: the converter and the notation are the dialect's, and a Mandarin converter
 * rendering a Cantonese reading would produce the wrong bytes.
 *
 * WHY THE SHAPE IS CHECKED rather than handed to the converter: `ToneConverter::convert` expects that stored form and
 * does not reject anything else -- measured 2026-10-21, `"ni"` came back `"nii"`, `"nǐ"` came back `"nii3"` and
 * `"nǐ3"` came back `"n3i"`. Plausible-looking garbage is worse than a refusal, so a reading that is not
 * letters-plus-digit is `CPPP2_ERR_INVALID_ARG` here. Which digits are meaningful is the DIALECT's business --
 * Mandarin writes 0-5, Cantonese 1-6 -- so the check accepts any single trailing digit and leaves the rest to
 * the converter.
 *
 * A STYLE THE DIALECT DOES NOT SUPPORT IS NOT AN ERROR: the reading comes back unchanged with `CPPP2_OK`. The
 * decision is the dialect's own declaration -- the entry looks the number up in `dialect.styles` and passes
 * through when it is absent -- not `ToneConverter::convert`'s behaviour. Example: Cantonese declares normal,
 * first-letter and tone3 (tone3 being its default), so asking it for `tone2`, which it does not declare,
 * returns the stored reading untouched. `v_to_u` and `mark_neutral_as_5` are NOT applied here -- this entry takes no
 * options, and switching `ü` behind a caller's back would make a style conversion look like a spelling change. Use the
 * query path when those switches are wanted.
 *
 * FAILURES, every output NULL: `CPPP2_ERR_INVALID_ARG` for a NULL argument, a `reading` that is not one reading
 * (this entry does not split on separators -- pass one syllable), or a `reading` that is not in the stored form
 * described above; `CPPP2_ERR_NOT_READY` for an unusable handle,
 * `CPPP2_ERR_INTERNAL` for an allocation failure or an exception at the boundary.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_tone_convert(const cppp2_engine *engine, const char *reading, int32_t style,
                                              char **out_text);


/* RETURNS a status (`CPPP2_OK` or an error code); the ANSWER is in `*out_is_polyphonic`, where
 * 1 = `hanzi`'s FIRST character STARTS A PHRASE in this dialect's corpus -- exactly the predicate
 * `cpp_pinyin::v2::Engine::isPolyphonic` defines and this function forwards to. NOT "the character has
 * more than one reading of its own": a single-reading character that opens a phrase answers 1, which is
 * why the C++ doc spells the name out as a trap worth reading before use (include/cpp-pinyin2/core/Engine.h,
 * the bullet ending "(C-13)"). Like the conversion above, only the first character is examined. */
CPP_PINYIN2_EXPORT int32_t cppp2_is_polyphonic(const cppp2_engine *engine, const char *hanzi,
                                               int32_t *out_is_polyphonic);

/* ----------------------------------------------------------------- dictionaries --- */

/*
 * Loads a user dictionary / traditional-to-simplified table into an existing engine.
 * `clear_existing` != 0 replaces what is loaded so far (the C++ default).
 *
 * MUTATES the handle: do not run this concurrently with any read on the same handle.
 *
 * FAILURES ARE TWO KINDS, and a host acts on them differently:
 *   CPPP2_ERR_NOT_READY  the handle is not usable (published handles never are, so this is a
 *                        contract guard rather than a state a caller can reach today);
 *   CPPP2_ERR_LOAD       the path or the table was refused -- retry with a different file.
 * There is no `cppp2_error` out-parameter HERE (unlike `cppp2_open`): this spelling predates the error channel
 * and is kept for the hosts that already call it. `cppp2_load_user_dict_ex` below is the same call with `err`;
 * both take the same decision and return the same code, so a host migrates without changing its branches.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_load_user_dict(cppp2_engine *engine, const char *path, int32_t clear_existing);
CPP_PINYIN2_EXPORT int32_t cppp2_load_traditional_dict(cppp2_engine *engine, const char *path, int32_t clear_existing);

/*
 * The same two loads, WITH the error channel. Appended, never substituted: the two names
 * above keep their signatures and their behaviour, and the only difference here is that `err` receives the pair
 * `{code, detail}` the C++ face already had in its `Outcome`.
 *
 * WHY THE DETAIL MATTERS EVEN THOUGH THE PATH IS THE CALLER'S: the path says WHICH file failed, not WHY.
 * The detail strings this library can actually produce for these two calls are `dictDir is empty`,
 * `failed to load: <file> (under <dir>)` (from the initial dictionary load) and `failed to load traditional
 * dict: <path>`; before this entry a C host got the code alone and had no way to ask anything.
 *
 * WHAT THESE TWO ENTRIES DELIBERATELY DO NOT DO: they do not grade the file's CONTENT. The user-dictionary
 * layer is tolerant by design -- an empty or comment-only `user_dict.txt` is legal, and the Cantonese built-in
 * ships as a 0-byte file -- so `CPPP2_ERR_LOAD` here means "the file could not be opened", never "its rows
 * were rejected". A caller that needs a content verdict must do what the Python shell does
 * (`_check_dict_file` in python/cpp_pinyin2/__init__.py): read the file and count its `key:value` rows.
 * (This comment used to justify the entry with three diagnoses -- "refusing to write an empty table",
 * "below the 20-row sanity floor", "the format marker is missing" -- that no code in this tree produced;
 * removed 2026-10-27. The format-marker one does exist, but for `open`'s dictionary directory, in
 * src/dict/DictManifest.cpp, not for these two calls.)
 *
 * `detail` is a fresh allocation owned by the CALLER (free with `cppp2_string_free`), NULL on success and NULL
 * when the library can only classify the failure -- the contract `cppp2_error` states above.
 */
CPP_PINYIN2_EXPORT int32_t cppp2_load_user_dict_ex(cppp2_engine *engine, const char *path, int32_t clear_existing,
                                                   cppp2_error *err);
CPP_PINYIN2_EXPORT int32_t cppp2_load_traditional_dict_ex(cppp2_engine *engine, const char *path,
                                                          int32_t clear_existing, cppp2_error *err);

/* -------------------------------------------------------------------- plugins --- */

/*
 * DELIBERATELY NOT HERE: dialect registration (`Dialect` + `Registry::add`) has no C entry point -- a dialect is
 * defined by a `ToneConverter`, i.e. by C++ function pointers of this library's own type, which C cannot construct, and
 * a C-shaped callback for `postProcess` would have to be encoded to and from `Result`. Id lookups therefore go through
 * a registry this surface is HANDED: `cppp2_open` and `cppp2_dialect_ids` take a nullable `const cppp2_registry *` a
 * C++ host fills with the address of its own `Registry`, so a dialect that host registered IS reachable from here,
 * while a C-only caller passes NULL and gets the two built-ins (nothing else: no process-level registry exists, so an
 * id nobody handed in does not exist). To ship a NEW dialect, use the C++ plugin path: docs/Custom Chinese Dialect.md.
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPP_PINYIN2_C_API_H */
