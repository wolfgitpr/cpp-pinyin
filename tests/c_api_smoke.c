/*
 * The C side of the C ABI, compiled as C: the proof that c_api.h is usable from a language without
 * classes, namespaces or exceptions. Every `cppp_*` entry point is CALLED here, so under a static build
 * this is the only guard that each one is actually linked -- a missing definition fails the link.
 * Usage: c_api_smoke [dictDir] (default "dict", matching the other test binaries).
 * Exit code 0 = all checks passed, 1 = at least one failed.
 */
#include <cpp-pinyin2/ext/c_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, what)                                                                                              \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                                    \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

static int list_contains(char **list, size_t count, const char *needle) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (strcmp(list[i], needle) == 0)
            return 1;
    }
    return 0;
}

#include <stddef.h> /* offsetof: the ABI gate below declares a prefix of cppp2_query_opts */

/* The conformance fixture emitter (tests/conformance_emit.c). Declared here rather than in a header:
   the two are one program, and a mismatch would be a link error, not a silent surprise. */
int cppp_emit_conformance(const char *dict_dir, const char *out_path, const char *commit);

int main(int argc, char **argv) {
    const char *dict_dir = (argc > 1) ? argv[1] : "dict";

    /* The other unusually shaped mode: write conformance/cases.json (plan doc 6.7.6, 6.8.4) and exit.
       Usage: c_api_smoke <dictDir> --emit-conformance <out.json> [<commit>] . The optional commit is
       supplied by the caller (`git rev-parse --short HEAD`) because this program has no business
       shelling out to git; omitting it writes "unknown", which is honest and still deterministic. */
    if (argc > 3 && strcmp(argv[2], "--emit-conformance") == 0)
        return cppp_emit_conformance(argv[1], argv[3], argc > 4 ? argv[4] : NULL);

    /* An unusually shaped mode, kept in this binary because it already links the library: print the
       registered dialect ids, one per line, and nothing else on stdout. tests/dialect_set/
       check_dialect_set.cmake compares this list with res/dict/ and with its frozen expected file, so
       a third language (data or code) fails the build instead of passing silently. */
    if (argc > 2 && strcmp(argv[2], "--print-dialect-ids") == 0) {
        char **ids = NULL;
        size_t count = 0;
        size_t i;
        if (cppp2_dialect_ids(NULL, &ids, &count) != CPPP2_OK)
            return 1;
        for (i = 0; i < count; ++i)
            printf("%s\n", ids[i]);
        cppp2_string_list_free(ids, count);
        return 0;
    }
    cppp2_error err;
    cppp2_engine *h;

    printf("cppp2_version() = %s\n", cppp2_version());
    // The version the C ABI reports must be the project version, not merely a non-empty string:
    // comparing against CPP_PINYIN2_VERSION_STRING (the same PROJECT_VERSION the library was built with)
    // catches a version written down a second time in C++ or a broken configure_file.
    CHECK(strcmp(cppp2_version(), CPP_PINYIN2_VERSION_STRING) == 0,
          "cppp2_version() equals the project version the library was built with");

    /* --- D2: the install-layout candidate (a C caller sees it, which is the point) --- */
#ifdef CPP_PINYIN2_STATIC
    CHECK(cppp2_dict_dir_candidate() == NULL,
          "a static build reports no dictionary-directory candidate: there is no module anchor to resolve");
#else
    {
        const char *candidate = cppp2_dict_dir_candidate();
        CHECK(candidate != NULL, "a shared build reports a dictionary-directory candidate");
        CHECK(candidate != NULL && strstr(candidate, "share/cpp-pinyin2/dict") != NULL,
              "the candidate carries the recorded install layout (share/cpp-pinyin2/dict)");
    }
#endif

    /* --- the status codes and their text (one authority each; the text is NOT written here) --- */
    {
        CHECK(CPPP2_OK == 0 && CPPP2_ERR_INVALID_ARG == 1 && CPPP2_ERR_NOT_READY == 2 && CPPP2_ERR_LOAD == 3 &&
                  CPPP2_ERR_INTERNAL == 4,
              "the C codes ARE the C++ Code numbers");
        CHECK(strcmp(cppp2_code_text(CPPP2_OK), "ok") == 0, "code text: ok");
        CHECK(strcmp(cppp2_code_text(CPPP2_ERR_NOT_READY), "not ready") == 0, "code text: not ready");
        CHECK(strcmp(cppp2_code_text(CPPP2_ERR_INTERNAL), "internal error") == 0, "code text: internal error");
        CHECK(cppp2_code_text(999) != NULL && cppp2_code_text(999)[0] != '\0',
              "a code outside the domain is reported, not NULL and not empty");
    }

    /* --- dialect discovery (a shell should not hardcode the ids) --- */
    {
        char **ids = NULL;
        size_t count = 0;
        CHECK(cppp2_dialect_ids(NULL, &ids, &count) == CPPP2_OK, "cppp2_dialect_ids returns OK");
        CHECK(count >= 2, "at least two dialects are registered");
        CHECK(list_contains(ids, count, "mandarin"), "mandarin is registered");
        CHECK(list_contains(ids, count, "cantonese"), "cantonese is registered");
        cppp2_string_list_free(ids, count);
    }

    /* --- engine lifecycle --- */
    h = cppp2_open(NULL, "mandarin", dict_dir, &err);
    CHECK(err.code == CPPP2_OK, "no error code on success");
    CHECK(err.detail == NULL, "no error detail on success");
    CHECK(h != NULL, "engine opened");
    if (h == NULL) {
        printf("detail: %s (code %d: %s)\n", err.detail ? err.detail : "(no message)", (int)err.code,
               cppp2_code_text(err.code));
        cppp2_string_free(err.detail);
        return 1;
    }
    /* --- the dialect's own style table (a shell should not hardcode the styles either) --- */
    {
        cppp2_style *styles = NULL;
        size_t style_count = 0;
        size_t k;
        CHECK(cppp2_style_list(h, &styles, &style_count) == CPPP2_OK, "cppp2_style_list returns OK");
        /* The count is the direct claim: the per-row checks cannot see a table that lost or
         * gained an entry, and the shells only reach this data through their own refusal tests. */
        CHECK(style_count == 9, "mandarin declares nine styles (test_style_table.cpp pins the same 9)");
        for (k = 0; k < style_count; ++k) {
            CHECK(styles[k].name != NULL && styles[k].name[0] != '\0', "every style row has a name");
            CHECK(styles[k].alias >= 0, "every alias is inside the shared number domain");
        }
        cppp2_style_list_free(styles, style_count);
    }
    /* The second built-in, for the same reason: its table is the small sparse one (0/3/8), which
     * is exactly the shape a hand-written shell table gets wrong. */
    {
        cppp2_engine *can = cppp2_open(NULL, "cantonese", dict_dir, &err);
        cppp2_style *cstyles = NULL;
        size_t cstyle_count = 0;
        CHECK(can != NULL, "cantonese opens for its style table too");
        if (can != NULL) {
            CHECK(cppp2_style_list(can, &cstyles, &cstyle_count) == CPPP2_OK, "cantonese style list returns OK");
            CHECK(cstyle_count == 3, "cantonese declares three styles (0 normal, 3 first-letter, 8 tone3)");
            cppp2_style_list_free(cstyles, cstyle_count);
        }
    }
    /* No readiness probe any more (N4b): a handle `cppp2_open` returned is always usable, and the
     * checks below are what prove it. */

    /* --- an unknown id must be named in the detail, and its code must say "not ready" --- */
    {
        cppp2_error uerr;
        cppp2_engine *nope = cppp2_open(NULL, "no-such-dialect", dict_dir, &uerr);
        CHECK(nope == NULL, "an unknown dialect id yields no handle");
        CHECK(uerr.code == CPPP2_ERR_NOT_READY, "an unknown id is CPPP2_ERR_NOT_READY, not INVALID_ARG");
        CHECK(uerr.detail != NULL && strstr(uerr.detail, "no-such-dialect") != NULL,
              "the error detail names the unknown id");
        cppp2_string_free(uerr.detail);
    }

    /* --- a query, walked through the struct the way an FFI consumer does --- */
    {
        cppp2_query_opts opts;
        cppp2_result *res = NULL;
        size_t i;

        cppp2_query_opts_init(&opts);
        CHECK(opts.struct_size == sizeof(cppp2_query_opts), "opts_init sets struct_size");
        opts.style = 0; /* NORMAL: no tones, safe to assert exact ASCII */

        /* A shell built against an older, smaller cppp2_query_opts declares the size it owns;
           opts_init must write only that prefix -- anything else is a heap overwrite inside the
           caller's process. Sentinel values make this fail loudly if the gate is ever removed:
           without it, on_error/include_candidates get clobbered to 0/1. */
        {
            cppp2_query_opts smaller;
            memset(&smaller, 0, sizeof smaller);
            smaller.on_error = 777;
            smaller.include_candidates = 888;
            smaller.struct_size = offsetof(cppp2_query_opts, on_error); /* owns through `style` */
            cppp2_query_opts_init(&smaller);
            CHECK(smaller.style == -1, "the declared prefix is initialised -- style gets the sentinel, not the old 0");
            CHECK(smaller.on_error == 777 && smaller.include_candidates == 888,
                  "fields outside the declared struct_size are NOT written");
        }

        /* ...and the historical form (struct_size left 0) still means "the whole struct". */
        memset(&opts, 0, sizeof opts);
        cppp2_query_opts_init(&opts);
        CHECK(opts.struct_size == sizeof(cppp2_query_opts), "struct_size 0 means the whole struct");
        opts.style = 0;

        /* --- the READ path treats struct_size == 0 the same way (2026-10-18) ---
           Before this, 0 meant "read no field at all": a caller that filled `style` but never set
           `struct_size` had it silently ignored and got the dialect default. c_api.h and opts_init have
           always said "0 = this header's whole struct"; now every entry point agrees, so this result can
           only differ from the default-opts one below by the style that was asked for. */
        {
            cppp2_query_opts zero;
            cppp2_result *zeroed = NULL;
            memset(&zero, 0, sizeof zero); /* struct_size deliberately left 0 */
            zero.style = 8; /* TONE3: ni3 hao3 */
            CHECK(cppp2_query(h, "\xe4\xbd\xa0\xe5\xa5\xbd", &zero, &zeroed) == CPPP2_OK,
                  "struct_size 0 + an explicit style still returns OK");
            if (zeroed != NULL) {
                CHECK(zeroed->count == 2, "the zeroed-opts query still has two syllables");
                if (zeroed->count == 2) {
                    CHECK(strcmp(zeroed->segments[0].rendered, "ni3") == 0,
                          "struct_size 0 reads the style the caller wrote (TONE3 => ni3, not ni-with-caron)");
                }
                cppp2_result_free(zeroed);
            }
        }

        /* --- C4/N27: an opts with NO explicit style asks for the DIALECT's own default ---
           For Mandarin that is TONE, which is what `EngineConfig{}` means in C++ and what both shells
           pass. Before C4 the default here was 0 (NORMAL), so this entry point and the Mandarin facade
           rendered the same sentence differently; these two checks are what makes a silent return to
           that state impossible on the C side. */
        {
            cppp2_result *toned = NULL;
            cppp2_query_opts empty;
            memset(&empty, 0, sizeof empty);
            cppp2_query_opts_init(&empty);
            CHECK(empty.style == -1, "opts_init hands out the sentinel, not a style");
            CHECK(cppp2_query(h, "\xe4\xbd\xa0\xe5\xa5\xbd", &empty, &toned) == CPPP2_OK,
                  "a query with the default opts returns OK");
            if (toned != NULL) {
                CHECK(toned->count == 2, "the default-style query still has two syllables");
                if (toned->count == 2) {
                    CHECK(strcmp(toned->segments[0].rendered, "n\xc7\x90") == 0,
                          "an empty opts renders the dialect default (TONE: ni with a caron)");
                    CHECK(strcmp(toned->segments[1].rendered, "h\xc7\x8eo") == 0,
                          "...and the same for the second span");
                }
            }
            cppp2_result_free(toned);
        }

        CHECK(cppp2_query(h, "\xe4\xbd\xa0\xe5\xa5\xbd", &opts, &res) == CPPP2_OK, "query returns OK");
        CHECK(res != NULL, "query returns a result");
        if (res != NULL) {
            CHECK(res->count == 2, "hello has two syllables");
            if (res->count == 2) {
                CHECK(strcmp(res->segments[0].text, "\xe4\xbd\xa0") == 0, "first span is the first hanzi");
                CHECK(strcmp(res->segments[0].rendered, "ni") == 0, "first reading is ni in NORMAL");
                CHECK(strcmp(res->segments[1].rendered, "hao") == 0, "second reading is hao in NORMAL");
                CHECK(res->segments[0].failure_kind == CPPP2_OK, "first span did not fail");
                CHECK(res->segments[0].syllables == NULL && res->segments[0].syllable_count == 0,
                      "the structure is NOT built unless include_reading says so");
                CHECK(res->segments[0].candidate_count >= 1, "candidates are included by default");
                CHECK(res->segments[0].candidates[0] != NULL, "candidate text is readable");
                for (i = 0; i < res->count; ++i) {
                    size_t j;
                    printf("  [%u] text=%s rendered=%s failure_kind=%d candidates=%u\n", (unsigned)i,
                           res->segments[i].text, res->segments[i].rendered, (int)res->segments[i].failure_kind,
                           (unsigned)res->segments[i].candidate_count);
                    for (j = 0; j < res->segments[i].candidate_count; ++j)
                        printf("        cand=%s\n", res->segments[i].candidates[j]);
                }
            }
            cppp2_result_free(res);
        }

        /* Style 1 = TONE: the reading must differ from NORMAL (that is what "style" means). */
        {
            cppp2_result *tone = NULL;
            opts.style = 1;
            CHECK(cppp2_query(h, "\xe4\xbd\xa0\xe5\xa5\xbd", &opts, &tone) == CPPP2_OK, "TONE query OK");
            if (tone != NULL && tone->count == 2) {
                CHECK(strcmp(tone->segments[0].rendered, "ni") != 0, "TONE reading differs from NORMAL");
                CHECK((unsigned char)tone->segments[0].rendered[1] >= 0x80, "TONE reading is non-ASCII");
                printf("  TONE: %s %s\n", tone->segments[0].rendered, tone->segments[1].rendered);
            }
            cppp2_result_free(tone);
        }

        /* include_candidates = 0 must leave the candidate list empty, not stale. */
        {
            cppp2_result *bare = NULL;
            opts.style = 0;
            opts.include_candidates = 0;
            CHECK(cppp2_query(h, "\xe8\xa1\x8c", &opts, &bare) == CPPP2_OK, "no-candidates query OK");
            if (bare != NULL && bare->count == 1)
                CHECK(bare->segments[0].candidate_count == 0, "candidates are empty when not requested");
            cppp2_result_free(bare);
            opts.include_candidates = 1;
        }

        /* The structured reading is OPT-IN, and the renderer is the library's own: include_reading = 1
           fills exactly one syllable per non-failed span, and cppp2_render_syllable turns that structure
           back into the span's own bytes -- which is what "the binding does not implement a style"
           means in practice. */
        {
            cppp2_query_opts ro;
            cppp2_result *rich = NULL;
            char *back = NULL;
            memset(&ro, 0, sizeof ro);
            /* Declared size BEFORE init, for the reason spelled out in conformance_emit.c: init reads
               struct_size to decide how far to write, so a declaration left as stack garbage can look
               like "I own only the first N bytes" and every field past that prefix silently keeps its
               garbage instead of the library's default. The fields this case does not write itself --
               on_error and v_to_u -- are the ones that would change the query's answer (v_to_u is
               visible: 略 renders "lve" at the default 0 and "lüe" at 1). */
            ro.struct_size = sizeof ro;
            cppp2_query_opts_init(&ro);
            ro.style = 8; /* TONE3 */
            ro.include_candidates = 0;
            ro.include_reading = 1;
            CHECK(cppp2_query(h, "\xe4\xbd\xa0\xe5\xa5\xbd", &ro, &rich) == CPPP2_OK, "include_reading query OK");
            if (rich != NULL && rich->count == 2) {
                CHECK(rich->segments[0].syllable_count == 1, "one span carries one structured syllable");
                CHECK(rich->segments[0].syllables != NULL, "the structure is present when asked for");
                if (rich->segments[0].syllables != NULL) {
                    const cppp2_syllable *s = rich->segments[0].syllables;
                    printf("  structured: initial=%s fin=%s tone=%d kind=%d source=%d\n", s->initial, s->fin,
                           (int)s->tone, (int)s->tone_kind, (int)s->source);
                    CHECK(s->fin[0] != '\0', "the final is filled");
                    CHECK(s->tone == 3, "ni is the third tone");
                    CHECK(s->tone_kind == CPPP2_TONE_KIND_BASE, "the entry is the table's own form");
                    CHECK(s->source == CPPP2_SOURCE_DICTIONARY, "the reading came from word.txt");
                    CHECK(cppp2_render_syllable(h, s, &ro, &back) == CPPP2_OK, "render_syllable renders the structure");
                    CHECK(back != NULL && strcmp(back, rich->segments[0].rendered) == 0,
                          "the rendered structure IS the span's own string");
                    cppp2_string_free(back);
                }
                /* A separator in a syllable's field is a multi-syllable string: refused, not split. */
                {
                    cppp2_syllable bad;
                    char *nope = NULL;
                    /* ZERO FIRST. This struct is both library-filled and caller-filled, so every field a
                       caller does not set is still read as a value -- `spelling` in particular. */
                    memset(&bad, 0, sizeof bad);
                    bad.initial = "";
                    bad.fin = "ni,hao";
                    bad.tone = 0;
                    bad.tone_kind = CPPP2_TONE_KIND_UNSPECIFIED;
                    bad.source = CPPP2_SOURCE_DICTIONARY;
                    CHECK(cppp2_render_syllable(h, &bad, &ro, &nope) == CPPP2_ERR_INVALID_ARG,
                          "a separator is not one syllable: INVALID_ARG");
                    CHECK(nope == NULL, "and no output is handed back on that failure");
                }
            }
            cppp2_result_free(rich);
        }

        /* THE IDENTITY FIELD (C2). `initial`+`fin` are a PROJECTION, and the projection cannot rebuild
           every reading the tables carry: `hm` (噷) has no final at all, so it comes back empty-handed.
           Before `spelling` existed this entry could only answer CPPP2_ERR_INVALID_ARG -- i.e. "your
           argument is malformed" -- for a reading that had merely been taken apart lossily. The field
           carries the table's own bytes, so the same structure now renders, and the bytes it renders to
           are the ones the query itself produced. */
        {
            cppp2_query_opts ri;
            cppp2_result *r = NULL;
            char *from_spelling = NULL;
            char *from_projection = NULL;
            memset(&ri, 0, sizeof ri);
            ri.struct_size = sizeof ri;
            cppp2_query_opts_init(&ri);
            ri.style = 8; /* TONE3 */
            ri.include_reading = 1;
            CHECK(cppp2_query(h, "\xe5\x99\xb7", &ri, &r) == CPPP2_OK, "query for the non-invertible reading");
            if (r != NULL && r->count == 1 && r->segments[0].syllable_count == 1) {
                const cppp2_syllable *s = r->segments[0].syllables;
                cppp2_syllable no_spelling;
                printf("  identity: spelling=%s initial=%s fin=%s\n", s->spelling, s->initial, s->fin);
                CHECK(s->spelling != NULL && strcmp(s->spelling, "hm") == 0,
                      "the table's own spelling is carried verbatim");
                no_spelling = *s;
                no_spelling.spelling = NULL;
                CHECK(cppp2_render_syllable(h, &no_spelling, &ri, &from_projection) == CPPP2_ERR_INVALID_ARG,
                      "without the spelling the projection really cannot rebuild it (the old blind spot)");
                CHECK(from_projection == NULL, "and that failure hands back nothing");
                CHECK(cppp2_render_syllable(h, s, &ri, &from_spelling) == CPPP2_OK,
                      "with the spelling the very same reading renders");
                CHECK(from_spelling != NULL && strcmp(from_spelling, r->segments[0].rendered) == 0,
                      "and it IS the string the query itself produced");
            }
            cppp2_string_free(from_spelling);
            cppp2_result_free(r);
        }

        /* NULL opts = documented defaults. */
        {
            cppp2_result *def = NULL;
            CHECK(cppp2_query(h, "abc", NULL, &def) == CPPP2_OK, "NULL opts works");
            if (def != NULL) {
                CHECK(def->count == 1, "ascii run is one span");
                if (def->count == 1)
                    CHECK(def->segments[0].failure_kind == CPPP2_ERR_INVALID_ARG,
                          "non-hanzi is a failed span, classified by the code (not by a flag)");
            }
            cppp2_result_free(def);
        }
    }

    /* --- readings / simplified / polyphonic --- */
    {
        char **readings = NULL;
        size_t count = 0;
        char *simp = NULL;
        int32_t poly = -1;
        char *missing;

        CHECK(cppp2_readings(h, "\xe8\xa1\x8c", NULL, &readings, &count) == CPPP2_OK, "readings OK");
        printf("  readings(\xe8\xa1\x8c) = %u\n", (unsigned)count);
        CHECK(count >= 2, "a polyphonic hanzi has two or more readings");
        cppp2_string_list_free(readings, count);

        CHECK(cppp2_is_polyphonic(h, "\xe8\xa1\x8c", &poly) == CPPP2_OK, "is_polyphonic OK");
        CHECK(poly == 1, "xing/hang is polyphonic");
        CHECK(cppp2_is_polyphonic(h, "\xe4\xbd\xa0", &poly) == CPPP2_OK, "is_polyphonic OK (2)");
        CHECK(poly == 0, "ni is not polyphonic");

        /* The contract is the FIRST character of the input (same as the C++ API): 學 -> 学. */
        CHECK(cppp2_to_simplified(h, "\xe5\xad\xb8", &simp) == CPPP2_OK, "to_simplified OK");
        CHECK(simp != NULL && strcmp(simp, "\xe5\xad\xa6") == 0, "traditional 學 becomes 学");
        cppp2_string_free(simp);

        /* A missing user dictionary must be refused, not silently accepted. */
        {
            size_t n = strlen(dict_dir);
            missing = (char *)malloc(n + 32);
            CHECK(missing != NULL, "allocated a path buffer");
            if (missing != NULL) {
                strcpy(missing, dict_dir);
                strcat(missing, "/not-a-user-dict.txt");
                CHECK(cppp2_load_user_dict(h, missing, 1) == CPPP2_ERR_LOAD,
                      "missing user dict returns CPPP2_ERR_LOAD");
                /* Same probe for the traditional-dictionary entry point, and NOT for symmetry alone:
                   this is the only place in the repository that calls it. Measured 2026-10-03:
                   `cppp2_load_traditional_dict` was the one frozen symbol no test referenced, so in a
                   static build (where nothing has a dynamic symbol table to compare, and where a
                   deleted definition changes no compile error) its implementation could be removed
                   with every entry still green. Referencing it here makes that a link error. */
                strcpy(missing, dict_dir);
                strcat(missing, "/not-a-traditional-dict.txt");
                CHECK(cppp2_load_traditional_dict(h, missing, 1) == CPPP2_ERR_LOAD,
                      "missing traditional dict returns CPPP2_ERR_LOAD");
                free(missing);
            }
        }
    }

    /* --- error paths --- */
    {
        cppp2_result *res = (cppp2_result *)&g_failures; /* a non-NULL pointer that is not a result */
        cppp2_engine *bad;

        CHECK(cppp2_query(NULL, "ni", NULL, &res) == CPPP2_ERR_INVALID_ARG, "null handle is rejected");
        CHECK(res == NULL, "out param is cleared on error");

        err.code = CPPP2_ERR_INTERNAL; /* garbage the call below must overwrite */
        err.detail = NULL;
        bad = cppp2_open(NULL, "no-such-dialect", dict_dir, &err);
        CHECK(bad == NULL, "unknown id yields no handle");
        CHECK(err.code == CPPP2_ERR_NOT_READY, "unknown id yields CPPP2_ERR_NOT_READY");
        CHECK(err.detail != NULL, "unknown id yields a detail");
        if (err.detail != NULL) {
            printf("  unknown id detail: %s\n", err.detail);
            CHECK(strstr(err.detail, "unknown") != NULL, "detail says the id is unknown");
            cppp2_string_free(err.detail);
        }

        err.code = CPPP2_ERR_INTERNAL;
        err.detail = NULL;
        bad = cppp2_open(NULL, "mandarin", NULL, &err);
        CHECK(bad == NULL, "NULL dict_dir yields no handle");
        CHECK(err.code == CPPP2_ERR_INVALID_ARG, "NULL dict_dir yields CPPP2_ERR_INVALID_ARG");
        CHECK(err.detail != NULL, "NULL dict_dir yields a detail");
        cppp2_string_free(err.detail);

        /* The free functions accept NULL so a shell needs no null checks of its own. */
        cppp2_string_free(NULL);
        cppp2_string_list_free(NULL, 0);
        cppp2_result_free(NULL);
    }

    cppp2_destroy(h);
    cppp2_destroy(NULL);

    if (g_failures != 0) {
        printf("C ABI smoke: %d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("C ABI smoke: all checks passed\n");
    return 0;
}
