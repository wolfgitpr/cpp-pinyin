/*
 * conformance/cases.json emitter, `--emit-conformance` of the CApiSmoke program (its own TU so
 * c_api_smoke.c stays one job). PROVENANCE (decision D35 = A, 2026-10-01): inputs are hand-picked and
 * expected values come from the engine under test, so this is a lock against regression, NOT an oracle
 * -- the oracle stays tests/catch2/test_c_api.cpp, deliberately not converted to read this fixture.
 * Two invariants the writer must keep: BINARY mode (a text-mode stream on Windows would rewrite "\n" as
 * "\r\n", and cases.sha256 is checked against the bytes, so it would pass on the generating machine and
 * fail in a fresh checkout; .gitattributes pins eol=lf likewise), and write "<path>.tmp" then rename only
 * after every case was emitted, so a generator that dies halfway leaves no plausible fixture to commit.
 */
#include <cpp-pinyin2/ext/c_api.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 你 好 世 界 中 国 人 民 行 學 学 略 */
#define T_NI "\xe4\xbd\xa0"
#define T_HAO "\xe5\xa5\xbd"
#define T_SHI "\xe4\xb8\x96"
#define T_JIE "\xe7\x95\x8c"
#define T_ZHONG "\xe4\xb8\xad"
#define T_GUO "\xe5\x9b\xbd"
#define T_REN "\xe4\xba\xba"
#define T_MIN "\xe6\xb0\x91"
#define T_HANG "\xe8\xa1\x8c"
#define T_XUE_TRAD "\xe5\xad\xb8"
#define T_XUE_SIMP "\xe5\xad\xa6"
#define T_LVE "\xe7\x95\xa5" /* 略, U+7565 */

#define T_NIHAO T_NI T_HAO
#define T_NIHAOSHIJIE T_NI T_HAO T_SHI T_JIE
#define T_ZHONGGUORENMIN T_ZHONG T_GUO T_REN T_MIN
#define T_XUEXI_TRAD T_XUE_TRAD "\xe7\xbf\x92" /* 習, U+7FD2 */

static int g_emit_failures = 0;

#define ECHECK(cond, what)                                                                                             \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                                    \
            ++g_emit_failures;                                                                                         \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

typedef struct {
    const char *id;
    const char *dialect;
    const char *op; /* query | readings | to_simplified | is_polyphonic */
    const char *text;
    int style;
    int candidates; /* query only */
    int v_to_u; /* query only */
} conf_case;

/* Hand-picked inputs (D35). Coverage required by section 6.8.4: both built-in dialects, at least
   three styles each, a polyphonic character, ASCII/digit/punctuation runs mixed with hanzi, the
   empty string, pure ASCII, traditional characters, and negative-shaped paths (a non-hanzi where a
   reading is expected, a span that must be marked failed). Scale is the separate block below. */
static const conf_case g_cases[] = {
    /* --- mandarin queries, four styles --- */
    {"m-normal-hello", "mandarin", "query", T_NIHAOSHIJIE, 0, 0, 0},
    {"m-tone-hello", "mandarin", "query", T_NIHAOSHIJIE, 1, 0, 0},
    {"m-tone3-hello", "mandarin", "query", T_NIHAOSHIJIE, 8, 0, 0},
    {"m-firstletter-hello", "mandarin", "query", T_NIHAOSHIJIE, 3, 0, 0},
    {"m-tone3-people", "mandarin", "query", T_ZHONGGUORENMIN, 8, 0, 0},
    {"m-normal-people", "mandarin", "query", T_ZHONGGUORENMIN, 0, 0, 0},
    /* --- polyphonic character, with and without candidates --- */
    {"m-tone3-hang-plain", "mandarin", "query", T_HANG, 8, 0, 0},
    {"m-tone3-hang-candidates", "mandarin", "query", T_HANG, 8, 1, 0},
    /* --- v_to_u, a per-dialect switch whose direction is worth writing down. Types.h says it
       outright ("ü 记作 v，除非 vToU=true"), and the 2026-10-01 probe agrees: flag off -> "lve" /
       "lve4", flag on -> "lüe" / "lüe4". The name reads the other way round, but it is the library's
       name on all three surfaces (Config::vToU, cppp2_query_opts.v_to_u, the shell's v_to_u) and
       renaming one of them would be worse than documenting the direction.
       女 was the first pick and locked nothing: its reading is "nv" with the flag either way.
       Cantonese ignores the switch completely (Cantonese.cpp), which the c- pair pins down so that
       a future dialect cannot start honouring it unnoticed. --- */
    {"m-normal-lve-plain", "mandarin", "query", T_LVE, 0, 0, 0},
    {"m-normal-lve-as-u", "mandarin", "query", T_LVE, 0, 0, 1},
    {"m-tone3-lve-plain", "mandarin", "query", T_LVE, 8, 0, 0},
    {"m-tone3-lve-as-u", "mandarin", "query", T_LVE, 8, 0, 1},
    {"c-normal-lve-plain", "cantonese", "query", T_LVE, 0, 0, 0},
    {"c-normal-lve-as-u", "cantonese", "query", T_LVE, 0, 0, 1},
    /* --- ASCII, digits, punctuation, empty, traditional --- */
    {"m-normal-ascii", "mandarin", "query", "abc", 0, 0, 0},
    {"m-normal-digits", "mandarin", "query", "abc123", 0, 0, 0},
    {"m-normal-mixed", "mandarin", "query", "abc" T_NI ",!?", 0, 0, 0},
    {"m-normal-empty", "mandarin", "query", "", 0, 0, 0},
    {"m-normal-failed-span", "mandarin", "query", "abc" T_NI, 0, 0, 0},
    {"m-tone3-traditional", "mandarin", "query", T_XUEXI_TRAD, 8, 0, 0},
    /* --- cantonese queries --- */
    {"c-normal-hello", "cantonese", "query", T_NIHAO, 0, 0, 0},
    {"c-tone3-hello", "cantonese", "query", T_NIHAO, 8, 0, 0},
    {"c-firstletter-hello", "cantonese", "query", T_NIHAO, 3, 0, 0},
    {"c-tone3-hang-candidates", "cantonese", "query", T_HANG, 8, 1, 0},
    {"c-normal-digits", "cantonese", "query", "123", 0, 0, 0},
    {"c-normal-mixed", "cantonese", "query", T_ZHONGGUORENMIN "abc", 0, 0, 0},
    {"c-normal-empty", "cantonese", "query", "", 0, 0, 0},
    {"c-tone3-traditional", "cantonese", "query", T_XUEXI_TRAD, 8, 0, 0},
    /* --- readings --- */
    {"m-readings-hang-normal", "mandarin", "readings", T_HANG, 0, 0, 0},
    {"m-readings-hang-tone3", "mandarin", "readings", T_HANG, 8, 0, 0},
    {"m-readings-ni-tone3", "mandarin", "readings", T_NI, 8, 0, 0},
    {"m-readings-ascii-normal", "mandarin", "readings", "a", 0, 0, 0},
    {"c-readings-hang-tone3", "cantonese", "readings", T_HANG, 8, 0, 0},
    {"c-readings-ni-normal", "cantonese", "readings", T_NI, 0, 0, 0},
    /* --- to_simplified (the contract is the FIRST character) --- */
    {"m-simplify-traditional", "mandarin", "to_simplified", T_XUE_TRAD, 0, 0, 0},
    {"m-simplify-pair", "mandarin", "to_simplified", T_XUEXI_TRAD, 0, 0, 0},
    {"m-simplify-plain", "mandarin", "to_simplified", T_NI, 0, 0, 0},
    {"m-simplify-ascii", "mandarin", "to_simplified", "a", 0, 0, 0},
    {"c-simplify-traditional", "cantonese", "to_simplified", T_XUE_TRAD, 0, 0, 0},
    /* --- is_polyphonic --- */
    {"m-poly-hang", "mandarin", "is_polyphonic", T_HANG, 0, 0, 0},
    {"m-poly-ni", "mandarin", "is_polyphonic", T_NI, 0, 0, 0},
    {"m-poly-ascii", "mandarin", "is_polyphonic", "a", 0, 0, 0},
    {"c-poly-hang", "cantonese", "is_polyphonic", T_HANG, 0, 0, 0},
    /* --- style_list / render_syllable / apply_tone_sandhi (2026-10-20, audit round 2 §12.3 B18) ---
       C-ABI-ONLY ON PURPOSE. The Python shell and the C# shell expose none of these three as a public
       method of their own (they are C-surface operations: a style table dump, a re-render of one
       structured syllable, and an in-place transform of a built result), so a case here compares the C
       ABI against the fixture, NOT against a second language. The consumers mark them `c-abi-only`
       explicitly rather than skipping them silently -- see KNOWN_OPS in python/tests/test_conformance.py
       and ConformanceTests.cs. Covering them at all is the point: before this, none of the three
       appeared in any cross-language fixture. */
    {"m-styles", "mandarin", "style_list", "", 0, 0, 0},
    {"c-styles", "cantonese", "style_list", "", 0, 0, 0},
    {"m-render-tone", "mandarin", "render_syllable", T_NI, 1, 0, 0},
    {"c-render-tone", "cantonese", "render_syllable", T_HANG, 1, 0, 0},
    {"m-sandhi-nihao", "mandarin", "apply_tone_sandhi", T_NIHAO, 8, 0, 0},
    {"m-sandhi-queries", "mandarin", "apply_tone_sandhi", T_ZHONGGUORENMIN, 8, 0, 0},
};

/* ------------------------------------------------------------------ scale block --- */

/*
 * A 4096-character stress input. Everything needed to rebuild it is IN THE FILE -- seed, the LCG
 * constants, the modulus, the 32-bit mask and the alphabet -- so any consumer reproduces the exact same
 * text with 32-bit integer arithmetic and no shared code. The expectation is the span count plus the
 * first and last eight readings, not all 4096 (that would put a megabyte of duplicate expectation in
 * the fixture to lock down what head, tail and count already catch).
 */
#define SCALE_SEED 20261001u
#define SCALE_MULTIPLIER 1103515245u
#define SCALE_INCREMENT 12345u
#define SCALE_MODULUS 2147483648u /* 2^31 */
#define SCALE_MASK 4294967295u /* 2^32 - 1 */
#define SCALE_LENGTH 4096
#define SCALE_EDGE 8

static const char *const g_alphabet[] = {
    T_NI, T_HAO, T_ZHONG, T_GUO, T_REN, T_MIN, T_HANG, "a", "b", "1", ",", "!",
};
#define ALPHABET_SIZE (sizeof g_alphabet / sizeof g_alphabet[0])

/* -------------------------------------------------------------------- emitting --- */

static void json_string(FILE *out, const char *s) {
    /* Minimal but correct: the bytes above 0x7F pass through untouched (the file is UTF-8) and the
       characters JSON cannot carry literally are escaped. */
    fputc('"', out);
    for (; *s != '\0'; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (c < 0x20)
                fprintf(out, "\\u%04x", c);
            else
                fputc(c, out);
            break;
        }
    }
    fputc('"', out);
}

static int query_syllables(const cppp2_engine *engine, const conf_case *c, cppp2_result **out) {
    cppp2_query_opts opts;
    memset(&opts, 0, sizeof opts);
    /* Declared size FIRST: cppp2_query_opts_init only writes the fields inside it (decision D19), so
       calling init on a zeroed struct_size would leave every field at whatever the memset put there
       and quietly stop honouring the library's defaults. */
    opts.struct_size = sizeof opts;
    cppp2_query_opts_init(&opts);
    opts.style = c->style;
    opts.include_candidates = c->candidates;
    opts.v_to_u = c->v_to_u;
    *out = NULL;
    if (cppp2_query(engine, c->text, &opts, out) != CPPP2_OK) {
        printf("FAIL %s: cppp2_query returned a non-OK status\n", c->id);
        ++g_emit_failures;
        return 0;
    }
    return 1;
}

/* Like `query_syllables`, but with `include_reading` ON: `render_syllable` takes a `cppp2_syllable*` and
   `apply_tone_sandhi` rewrites structured rows, so both need the structure a plain query does not build. */
static int query_structured(const cppp2_engine *engine, const conf_case *c, cppp2_result **out) {
    cppp2_query_opts opts;
    memset(&opts, 0, sizeof opts);
    opts.struct_size = sizeof opts;
    cppp2_query_opts_init(&opts);
    opts.style = c->style;
    opts.include_candidates = c->candidates;
    opts.v_to_u = c->v_to_u;
    opts.include_reading = 1;
    *out = NULL;
    if (cppp2_query(engine, c->text, &opts, out) != CPPP2_OK) {
        printf("FAIL %s: cppp2_query (structured) returned a non-OK status\n", c->id);
        ++g_emit_failures;
        return 0;
    }
    return 1;
}

static int emit_case(FILE *out, const cppp2_engine *engine, const conf_case *c) {
    fputs("    {\n", out);
    fputs("      \"id\": ", out);
    json_string(out, c->id);
    fputs(",\n      \"dialect\": ", out);
    json_string(out, c->dialect);
    fputs(",\n      \"op\": ", out);
    json_string(out, c->op);
    fputs(",\n      \"text\": ", out);
    json_string(out, c->text);
    fprintf(out, ",\n      \"style\": %d,\n      \"candidates\": %s,\n      \"vToU\": %s,\n", c->style,
            c->candidates ? "true" : "false", c->v_to_u ? "true" : "false");

    if (strcmp(c->op, "query") == 0) {
        cppp2_result *res = NULL;
        size_t i;
        int first = 1;
        if (!query_syllables(engine, c, &res))
            return 0;
        fputs("      \"expect_syllables\": [", out);
        if (res != NULL) {
            for (i = 0; i < res->count; ++i) {
                const cppp2_segment *s = &res->segments[i];
                size_t j;
                if (!first)
                    fputc(',', out);
                first = 0;
                fputs("\n        {\"text\": ", out);
                json_string(out, s->text);
                fputs(", \"reading\": ", out);
                json_string(out, s->rendered);
                fprintf(out, ", \"failed\": %s, \"candidates\": [", s->failure_kind != CPPP2_OK ? "true" : "false");
                for (j = 0; j < s->candidate_count; ++j) {
                    if (j != 0)
                        fputs(", ", out);
                    json_string(out, s->candidates[j]);
                }
                fputs("]}", out);
            }
            if (!first)
                fputs("\n      ", out);
            cppp2_result_free(res);
        }
        fputs("],\n      \"expect_readings\": []\n    }", out);
    } else if (strcmp(c->op, "readings") == 0) {
        char **list = NULL;
        size_t count = 0;
        size_t i;
        cppp2_query_opts opts;
        memset(&opts, 0, sizeof opts);
        opts.struct_size = sizeof opts;
        cppp2_query_opts_init(&opts);
        /* The style has to travel with the request: reading styles differ (NORMAL vs TONE3 are two
           different strings for the same character), and a NULL opts here would silently record the
           default style's readings under a case that asked for another one. */
        opts.style = c->style;
        if (cppp2_readings(engine, c->text, &opts, &list, &count) != CPPP2_OK) {
            printf("FAIL %s: cppp2_readings returned a non-OK status\n", c->id);
            ++g_emit_failures;
            return 0;
        }
        fputs("      \"expect_syllables\": [],\n      \"expect_readings\": [", out);
        for (i = 0; i < count; ++i) {
            if (i != 0)
                fputs(", ", out);
            json_string(out, list[i]);
        }
        fputs("]\n    }", out);
        cppp2_string_list_free(list, count);
    } else if (strcmp(c->op, "to_simplified") == 0) {
        char *simple = NULL;
        if (cppp2_to_simplified(engine, c->text, &simple) != CPPP2_OK) {
            printf("FAIL %s: cppp2_to_simplified returned a non-OK status\n", c->id);
            ++g_emit_failures;
            return 0;
        }
        /* `expect` (a scalar) is the schema-1 extension for the two ops whose result is not a list.
           It is additive: no existing field changes meaning. */
        fputs("      \"expect_syllables\": [],\n      \"expect_readings\": [],\n      \"expect\": ", out);
        json_string(out, simple != NULL ? simple : "");
        fputs("\n    }", out);
        cppp2_string_free(simple);
    } else if (strcmp(c->op, "is_polyphonic") == 0) {
        int32_t poly = -1;
        if (cppp2_is_polyphonic(engine, c->text, &poly) != CPPP2_OK) {
            printf("FAIL %s: cppp2_is_polyphonic returned a non-OK status\n", c->id);
            ++g_emit_failures;
            return 0;
        }
        fprintf(out,
                "      \"expect_syllables\": [],\n      \"expect_readings\": [],\n"
                "      \"expect\": %s\n    }",
                poly == 1 ? "true" : "false");
    } else if (strcmp(c->op, "style_list") == 0) {
        cppp2_style *styles = NULL;
        size_t count = 0;
        size_t i;
        if (cppp2_style_list(engine, &styles, &count) != CPPP2_OK) {
            printf("FAIL %s: cppp2_style_list returned a non-OK status\n", c->id);
            ++g_emit_failures;
            return 0;
        }
        /* A LIST, so it gets its own field: `expect` is a scalar by contract (the two ops above use it),
           and stuffing a list into it would change what an existing field means. */
        fputs("      \"expect_syllables\": [],\n      \"expect_readings\": [],\n      \"expect_styles\": [", out);
        for (i = 0; i < count; ++i) {
            if (i != 0)
                fputs(", ", out);
            json_string(out, styles[i].name);
        }
        fputs("]\n    }", out);
        cppp2_style_list_free(styles, count);
    } else if (strcmp(c->op, "render_syllable") == 0) {
        /* Render ONE table spelling in `style`. The syllable is built from the dictionary's own reading
           for `text`, obtained through the structured query path (`include_reading`), so what this case
           pins is `cppp2_render_syllable` reproducing what the query already produced. */
        cppp2_query_opts qopts;
        cppp2_query_opts ropts;
        cppp2_result *res = NULL;
        char *rendered = NULL;
        const conf_case probe = *c;
        if (!query_structured(engine, &probe, &res))
            return 0;
        ECHECK(res != NULL && res->count > 0 && res->segments[0].syllable_count == 1,
               "render_syllable needs a row with exactly one structured syllable");
        if (res == NULL || res->count == 0 || res->segments[0].syllable_count != 1) {
            cppp2_result_free(res);
            return 0;
        }
        memset(&ropts, 0, sizeof ropts);
        ropts.struct_size = sizeof ropts;
        cppp2_query_opts_init(&ropts);
        ropts.style = c->style;
        if (cppp2_render_syllable(engine, &res->segments[0].syllables[0], &ropts, &rendered) != CPPP2_OK) {
            printf("FAIL %s: cppp2_render_syllable returned a non-OK status\n", c->id);
            ++g_emit_failures;
            cppp2_result_free(res);
            return 0;
        }
        fputs("      \"expect_syllables\": [],\n      \"expect_readings\": [],\n      \"expect\": ", out);
        json_string(out, rendered != NULL ? rendered : "");
        fputs("\n    }", out);
        cppp2_string_free(rendered);
        cppp2_result_free(res);
        (void)qopts;
    } else if (strcmp(c->op, "apply_tone_sandhi") == 0) {
        /* Build the result at TONE3 (the only shape the pass can rewrite), then let the pass transform it
           in place. Both halves are recorded: how many rows changed AND what every row holds afterwards,
           because a count alone would not notice a rewrite of the wrong row. */
        cppp2_tone_sandhi_opts sopts;
        cppp2_result *res = NULL;
        size_t i;
        size_t changed = (size_t)-1;
        const conf_case probe = *c;
        if (!query_structured(engine, &probe, &res))
            return 0;
        /* `NULL` opts, not a zero-filled struct: c_api.h documents NULL as "the library's defaults"
           (all three switches ON), while a memset-to-zero struct legitimately means "all three OFF" --
           measured here first: with the zeroed struct the pass reported `changed = 0` for ni3 hao3 and
           the fixture recorded a pass that did nothing. The defaults are what a plain call gets, so they
           are what the conformance case pins. */
        (void)sopts;
        if (cppp2_apply_tone_sandhi(engine, res, NULL, &changed) != CPPP2_OK) {
            printf("FAIL %s: cppp2_apply_tone_sandhi returned a non-OK status\n", c->id);
            ++g_emit_failures;
            cppp2_result_free(res);
            return 0;
        }
        fprintf(out,
                "      \"expect_syllables\": [],\n      \"expect_readings\": [],\n"
                "      \"expect_changed\": %u,\n      \"expect_rendered\": [",
                (unsigned)changed);
        for (i = 0; i < res->count; ++i) {
            if (i != 0)
                fputs(", ", out);
            json_string(out, res->segments[i].rendered);
        }
        fputs("]\n    }", out);
        cppp2_result_free(res);
    } else {
        printf("FAIL %s: unknown op '%s'\n", c->id, c->op);
        ++g_emit_failures;
        return 0;
    }
    fputs("\n", out);
    return 1;
}

static int emit_scale(FILE *out, const cppp2_engine *engine) {
    static char text[SCALE_LENGTH * 4 + 1];
    cppp2_query_opts opts;
    cppp2_result *res = NULL;
    uint32_t x = SCALE_SEED;
    size_t used = 0;
    int i;

    for (i = 0; i < SCALE_LENGTH; ++i) {
        const char *ch;
        x = (x * SCALE_MULTIPLIER + SCALE_INCREMENT) % SCALE_MODULUS;
        ch = g_alphabet[x % ALPHABET_SIZE];
        memcpy(text + used, ch, strlen(ch));
        used += strlen(ch);
    }
    text[used] = '\0';

    memset(&opts, 0, sizeof opts);
    opts.struct_size = sizeof opts; /* before init, for the reason given in query_syllables */
    cppp2_query_opts_init(&opts);
    opts.style = 8; /* TONE3, the style with the most structure to lose */
    if (cppp2_query(engine, text, &opts, &res) != CPPP2_OK) {
        printf("FAIL scale: cppp2_query returned a non-OK status\n");
        ++g_emit_failures;
        return 0;
    }
    fputs("  \"scale\": {\n", out);
    fprintf(out, "    \"seed\": %u,\n    \"multiplier\": %u,\n    \"increment\": %u,\n", SCALE_SEED, SCALE_MULTIPLIER,
            SCALE_INCREMENT);
    fprintf(out, "    \"modulus\": %u,\n    \"mask\": %u,\n    \"length\": %d,\n", SCALE_MODULUS, SCALE_MASK,
            SCALE_LENGTH);
    fputs("    \"algorithm\": \"x = (x * multiplier + increment) mod modulus; repeat length times; "
          "character = alphabet[x mod alphabet_size]\",\n",
          out);
    fputs("    \"dialect\": \"mandarin\",\n    \"op\": \"query\",\n    \"style\": 8,\n", out);
    fputs("    \"alphabet\": [", out);
    for (i = 0; i < (int)ALPHABET_SIZE; ++i) {
        if (i != 0)
            fputs(", ", out);
        json_string(out, g_alphabet[i]);
    }
    fputs("],\n", out);
    if (res != NULL) {
        size_t tail_start = res->count > SCALE_EDGE ? res->count - SCALE_EDGE : 0;
        size_t k;
        fprintf(out, "    \"expect_span_count\": %u,\n", (unsigned)res->count);
        fputs("    \"expect_head\": [", out);
        for (k = 0; k < SCALE_EDGE && k < res->count; ++k) {
            if (k != 0)
                fputs(", ", out);
            json_string(out, res->segments[k].rendered);
        }
        fputs("],\n    \"expect_tail\": [", out);
        for (k = tail_start; k < res->count; ++k) {
            if (k != tail_start)
                fputs(", ", out);
            json_string(out, res->segments[k].rendered);
        }
        fputs("]\n  },\n", out);
        cppp2_result_free(res);
    } else {
        /* Unreachable unless cppp2_query starts returning OK with a NULL result. Kept as a hard stop
           rather than an empty expectation: a fixture with no scale expectation would still look
           complete. */
        printf("FAIL scale: cppp2_query returned OK with no result\n");
        ++g_emit_failures;
        return 0;
    }
    return 1;
}

int cppp_emit_conformance(const char *dict_dir, const char *out_path, const char *commit) {
    char tmp_path[4096];
    cppp2_engine *engines[2] = {NULL, NULL};
    const char *dialect_ids[2] = {"mandarin", "cantonese"};
    FILE *out;
    size_t i;
    size_t emitted = 0;

    if (strlen(out_path) + 8 >= sizeof tmp_path) {
        printf("FAIL --emit-conformance: output path is too long\n");
        return 1;
    }
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", out_path);

    for (i = 0; i < 2; ++i) {
        cppp2_error err;
        err.code = CPPP2_OK;
        err.detail = NULL;
        engines[i] = cppp2_open(NULL, dialect_ids[i], dict_dir, &err);
        if (engines[i] == NULL) {
            printf("FAIL --emit-conformance: cannot open the %s engine: %s (code %d: %s)\n", dialect_ids[i],
                   err.detail != NULL ? err.detail : "(no message)", (int)err.code, cppp2_code_text(err.code));
            cppp2_string_free(err.detail);
            return 1;
        }
        cppp2_string_free(err.detail);
    }

    out = fopen(tmp_path, "wb");
    if (out == NULL) {
        printf("FAIL --emit-conformance: cannot write %s\n", tmp_path);
        return 1;
    }

    fputs("{\n", out);
    fputs("  \"schema\": 1,\n", out);
    fprintf(out, "  \"cases_count\": %u,\n", (unsigned)(sizeof g_cases / sizeof g_cases[0]));
    fputs("  \"generated_by_commit\": ", out);
    json_string(out, commit != NULL ? commit : "unknown");
    fputs(",\n", out);
    fputs("  \"dialects\": [\"cantonese\", \"mandarin\"],\n", out);
    if (!emit_scale(out, engines[0]))
        goto failed;
    fputs("  \"cases\": [\n", out);
    for (i = 0; i < sizeof g_cases / sizeof g_cases[0]; ++i) {
        const cppp2_engine *engine = strcmp(g_cases[i].dialect, "mandarin") == 0 ? engines[0] : engines[1];
        if (i != 0)
            fputs(",\n", out);
        if (!emit_case(out, engine, &g_cases[i]))
            goto failed;
        ++emitted;
    }
    fputs("\n  ]\n}\n", out);

    if (fclose(out) != 0) {
        printf("FAIL --emit-conformance: flushing %s failed\n", tmp_path);
        goto failed_no_close;
    }
    ECHECK(emitted == sizeof g_cases / sizeof g_cases[0],
           "every case in the table was emitted (cases_count must equal len(cases))");

    if (g_emit_failures != 0) {
        printf("conformance: %d failure(s), no fixture written\n", g_emit_failures);
        remove(tmp_path);
    } else {
        /* POSIX rename() replaces an existing destination; MSVC's rename() refuses with EEXIST. That
           is how the first version of this file passed its very first generation and then failed on
           every regeneration -- proved 2026-10-01: "cannot move ... into place", exit 1, and the file
           left on disk was simply the old fixture, which makes a naive "did the hash change?" check
           report success. Remove first (and confirm it is really gone), then rename. */
        FILE *still_there;
        remove(out_path);
        still_there = fopen(out_path, "rb");
        if (still_there != NULL) {
            fclose(still_there);
            printf("FAIL --emit-conformance: cannot replace the existing %s\n", out_path);
            remove(tmp_path);
            g_emit_failures = 1;
        } else if (rename(tmp_path, out_path) != 0) {
            printf("FAIL --emit-conformance: cannot move %s into place\n", tmp_path);
            remove(tmp_path);
            g_emit_failures = 1;
        } else {
            printf("conformance: wrote %u case(s) to %s\n", (unsigned)emitted, out_path);
        }
    }

    for (i = 0; i < 2; ++i)
        cppp2_destroy(engines[i]);
    return g_emit_failures != 0 ? 1 : 0;

failed:
    fclose(out);
failed_no_close:
    remove(tmp_path);
    printf("conformance: %d failure(s), no fixture written\n", g_emit_failures);
    for (i = 0; i < 2; ++i)
        cppp2_destroy(engines[i]);
    return 1;
}
