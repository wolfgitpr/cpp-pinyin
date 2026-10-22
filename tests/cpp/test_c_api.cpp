// C ABI gate -- the C surface is compared against the C++ API it wraps, field by field.
//
// Why this exists: a smoke test only proves the symbols link. The interesting failures of a
// marshalling layer are silent ones -- a dropped candidate list, a style that is not passed
// through, a struct_size rule that reads a field the caller said it does not have, an error path
// that returns CPPP2_OK. So every case here runs the SAME input through both APIs and compares.
#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/ToneSandhi.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <cpp-pinyin2/ext/c_api.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{

    using SyllableTuple = std::tuple<std::string, std::string, bool, std::vector<std::string>>;
    // One structured reading (initial, fin, tone, tone_kind, source) as the two faces hold it.
    using ReadingTuple = std::tuple<std::string, std::string, int, int, int>;

    std::filesystem::path dictRoot() { return std::filesystem::current_path() / "dict"; }

    std::vector<ReadingTuple> readingsOf(const cppp2_syllable *s, size_t count) {
        std::vector<ReadingTuple> out;
        for (size_t i = 0; i < count; ++i)
            out.emplace_back(s[i].initial, s[i].fin, s[i].tone, s[i].tone_kind, s[i].source);
        return out;
    }

    std::vector<SyllableTuple> fromC(const cppp2_result *r) {
        std::vector<SyllableTuple> out;
        for (size_t i = 0; i < r->count; ++i) {
            const cppp2_segment &s = r->segments[i];
            std::vector<std::string> cands;
            for (size_t j = 0; j < s.candidate_count; ++j)
                cands.emplace_back(s.candidates[j]);
            out.emplace_back(s.text, s.rendered, s.failure_kind != CPPP2_OK, cands);
        }
        return out;
    }

    std::vector<SyllableTuple> fromCpp(const cpp_pinyin::v2::Result &r) {
        std::vector<SyllableTuple> out;
        for (size_t i = 0; i < r.size(); ++i) {
            const cpp_pinyin::v2::Segment &s = r[i];
            out.emplace_back(s.text, s.rendered, s.failure_kind != cpp_pinyin::v2::Code::Ok, s.candidates);
        }
        return out;
    }

    // NOTE: there is deliberately no "opts -> EngineConfig" helper here that copies every field.
    // The struct_size cases below must NOT go through one: they check that the library ignores the
    // fields a caller's struct_size says it does not have, so the expectation has to be written out
    // per case rather than derived by a mirror of the library's own mapping.
    cppp2_query_opts optsOf(const cpp_pinyin::v2::EngineConfig &cfg) {
        // Zero-initialised before `cppp2_query_opts_init`, and not because init leaves holes: init reads
        // `struct_size` before it can know it is meaningful, and honours it as a declared prefix whenever
        // it looks plausible (>= sizeof(size_t), < sizeof(struct)). Stack garbage in `struct_size` would
        // therefore be honoured as a size nobody declared, and the fields outside it would keep the
        // garbage the frame had -- silently, which is the one outcome this test file exists to catch.
        cppp2_query_opts o{};
        cppp2_query_opts_init(&o);
        o.style = cfg.style;
        o.on_error = (cfg.onError == cpp_pinyin::v2::ErrorHandling::Drop) ? 1 : 0;
        o.include_candidates = cfg.includeCandidates ? 1 : 0;
        o.v_to_u = cfg.vToU ? 1 : 0;
        o.mark_neutral_as_5 = cfg.markNeutralAs5 ? 1 : 0;
        o.include_reading = cfg.includeReading ? 1 : 0;
        return o;
    }

    // The oracle for every comparison below: a registry the test owns. The library has no
    // process-level registry, so a test that names a dialect has to declare one -- and the C ABI
    // looks ids up in the registry it is HANDED, or in its own built-ins when that argument is
    // NULL (a fresh registry carries exactly the same two, which is what the dialect-list case pins).
    const cpp_pinyin::v2::Registry &testRegistry() {
        static const cpp_pinyin::v2::Registry registry;
        return registry;
    }

}  // namespace

// The C face of the post-processing step. The expectations are the ones the C++ rules produce for the
// same input (tests/cpp/test_tone_sandhi.cpp, TONE3 readings), reached through the C entry point:
// 你好 ni3 hao3 -> ni2 hao3, 不是 bu4 shi4 -> bu2 shi4, 一定 yi1 ding4 -> yi2 ding4, 小心 unchanged.
TEST_CASE("C ABI: tone sandhi returns a status, reports the count through out_changed", "[c_api][sandhi]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_error err{};
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), &err);
    REQUIRE(err.code == CPPP2_OK);
    REQUIRE(h != nullptr);

    cpp_pinyin::v2::EngineConfig cfg;
    // Style 8 is TONE3: letters plus a tone digit, the only shape the sandhi shape test accepts. A
    // result rendered in any other style is left alone, which is the "changed == 0" case below.
    cfg.style = 8;
    const cppp2_query_opts o = optsOf(cfg);

    struct Case {
        const char *text;
        const char *first;
        const char *second;
        size_t changed;
    };
    const Case cases[] = {
        {"你好", "ni2", "hao3", 1},
        {"不是", "bu2", "shi4", 1},
        {"一定", "yi2", "ding4", 1},
        {"小心", "xiao3", "xin1", 0},
    };

    for (const Case &one : cases) {
        cppp2_result *cr = nullptr;
        REQUIRE(cppp2_query(h, one.text, &o, &cr) == CPPP2_OK);
        REQUIRE(cr->count == 2);

        size_t changed = 999;
        // NULL opts means the library's own defaults, which are all three rules ON.
        CHECK(cppp2_apply_tone_sandhi(h, cr, nullptr, &changed) == CPPP2_OK);
        CHECK(changed == one.changed);
        CHECK(std::string(cr->segments[0].rendered) == one.first);
        CHECK(std::string(cr->segments[1].rendered) == one.second);
        // The row count never changes, and the input spans are untouched.
        CHECK(cr->count == 2);
        // The input spans are untouched: they still concatenate to what was asked for. (Comparing the
        // first byte of `one.text` would be wrong -- a Hanzi is three bytes, so that is not one span.)
        std::string joined;
        for (size_t i = 0; i < cr->count; ++i)
            joined += cr->segments[i].text;
        CHECK(joined == one.text);

        // out_changed may be NULL: "do not report the count" is not an error.
        cppp2_result *again = nullptr;
        REQUIRE(cppp2_query(h, one.text, &o, &again) == CPPP2_OK);
        CHECK(cppp2_apply_tone_sandhi(h, again, nullptr, nullptr) == CPPP2_OK);
        CHECK(std::string(again->segments[0].rendered) == one.first);
        cppp2_result_free(again);

        // A NULL result is a status error, not a crash.
        CHECK(cppp2_apply_tone_sandhi(h, nullptr, nullptr, nullptr) == CPPP2_ERR_INVALID_ARG);
        // So is an unusable handle: the DIALECT is what decides whether a pass exists, so there is no
        // "apply it anyway" reading of a NULL engine.
        CHECK(cppp2_apply_tone_sandhi(nullptr, cr, nullptr, nullptr) == CPPP2_ERR_NOT_READY);

        cppp2_result_free(cr);
    }

    // The pass belongs to the DIALECT, so Cantonese -- which declares none -- must refuse rather than rewrite
    // jyutping with 不/一 rules it does not have. This is the assertion that could not exist before the C entry
    // took an engine: back then this call returned CPPP2_OK and changed the rows.
    {
        cppp2_error cerr{};
        cppp2_engine *cantonese = cppp2_open(nullptr, "cantonese", dictRoot().string().c_str(), &cerr);
        REQUIRE(cantonese != nullptr);
        cppp2_query_opts cOpts{};
        cppp2_query_opts_init(&cOpts);
        cOpts.style = static_cast<int>(cpp_pinyin::v2::CantoneseStyle::TONE3);
        cppp2_result *rows = nullptr;
        REQUIRE(cppp2_query(cantonese, "你好", &cOpts, &rows) == CPPP2_OK);
        REQUIRE(rows->count > 0);
        const std::string before = rows->segments[0].rendered;
        size_t changed = 999;
        CHECK(cppp2_apply_tone_sandhi(cantonese, rows, nullptr, &changed) == CPPP2_ERR_INVALID_ARG);
        CHECK(changed == 0);  // refused, so nothing is claimed and nothing is done
        CHECK(std::string(rows->segments[0].rendered) == before);
        cppp2_result_free(rows);
        cppp2_destroy(cantonese);
    }
    cppp2_destroy(h);
}
TEST_CASE("C ABI: tone_convert converts one reading between styles", "[c_api]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_error err{};
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), &err);
    REQUIRE(err.code == CPPP2_OK);
    REQUIRE(h != nullptr);

    char *text = nullptr;
    CHECK(cppp2_tone_convert(h, "ni3", 1 /* TONE */, &text) == CPPP2_OK);
    REQUIRE(text != nullptr);
    CHECK(std::string(text) == "n\xC7\x90");  // nǐ
    cppp2_string_free(text);

    // -1 is the sentinel "the dialect's own default", and this entry used to hand it to `convert` unresolved:
    // the caller got its own string back with CPPP2_OK (measured 2026-10-22). Both shells default to -1, so
    // this is the shape every `tone_convert("ni3")` takes.
    text = nullptr;
    CHECK(cppp2_tone_convert(h, "ni3", -1, &text) == CPPP2_OK);
    REQUIRE(text != nullptr);
    CHECK(std::string(text) == "n\xC7\x90");  // nǐ -- Mandarin's declared default is the marked style
    cppp2_string_free(text);

    // A style this dialect does not fill returns the reading UNCHANGED with success: that is
    // `ToneConverter::convert`'s contract, and the reason no "unsupported style" code exists.
    text = nullptr;
    CHECK(cppp2_tone_convert(h, "ni3", 99, &text) == CPPP2_OK);
    REQUIRE(text != nullptr);
    CHECK(std::string(text) == "ni3");
    cppp2_string_free(text);

    // Cantonese writes tones 1-6, so a tone-6 reading must convert like any other. This used to be
    // CPPP2_ERR_INVALID_ARG: the accepted digit range was Mandarin's 0-5 (measured 2026-10-22).
    {
        cppp2_error cerr{};
        cppp2_engine *ch = cppp2_open(nullptr, "cantonese", dir.string().c_str(), &cerr);
        REQUIRE(cerr.code == CPPP2_OK);
        REQUIRE(ch != nullptr);
        char *canto = nullptr;
        CHECK(cppp2_tone_convert(ch, "jyut6", -1, &canto) == CPPP2_OK);
        REQUIRE(canto != nullptr);
        CHECK(std::string(canto) == "jyut6");  // tone3 is Cantonese's declared default
        cppp2_string_free(canto);
        cppp2_destroy(ch);
    }

    // ONE reading: a separator is a bad argument here, and the query path is what splits.
    text = nullptr;
    CHECK(cppp2_tone_convert(h, "ni3 hao3", 1, &text) == CPPP2_ERR_INVALID_ARG);
    CHECK(text == nullptr);

    // NULL / empty are refused before anything is converted, and a NULL out-parameter too.
    CHECK(cppp2_tone_convert(h, nullptr, 1, &text) == CPPP2_ERR_INVALID_ARG);
    CHECK(cppp2_tone_convert(h, "", 1, &text) == CPPP2_ERR_INVALID_ARG);
    CHECK(cppp2_tone_convert(h, "ni3", 1, nullptr) == CPPP2_ERR_INVALID_ARG);

    cppp2_destroy(h);
}

TEST_CASE("C ABI: query is field-identical to the C++ API", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_error err{};
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), &err);
    REQUIRE(err.code == CPPP2_OK);
    REQUIRE(err.detail == nullptr);
    REQUIRE(h != nullptr);
    // No readiness probe any more (N4b): a handle cppp2_open returned is always usable, and the
    // queries below are what prove it.

    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "mandarin").value();

    const std::vector<std::string> texts = {"你好", "中国银行",  "hello 世界", "行",       "龘",
                                            "重",   "小星星 21", "{}[]",       "一不留神", "粤港澳大湾区"};
    const std::vector<int> styles = {0, 1, 2, 3, 4, 5, 6, 7, 8};

    for (const std::string &text : texts) {
        for (int style : styles) {
            for (bool candidates : {true, false}) {
                for (bool vToU : {false, true}) {
                    cpp_pinyin::v2::EngineConfig cfg;
                    cfg.style = style;
                    cfg.includeCandidates = candidates;
                    cfg.vToU = vToU;
                    const cppp2_query_opts o = optsOf(cfg);

                    cppp2_result *cr = nullptr;
                    REQUIRE(cppp2_query(h, text.c_str(), &o, &cr) == CPPP2_OK);
                    REQUIRE(cr != nullptr);
                    const auto got = fromC(cr);
                    const auto want = fromCpp(cpp.query(text, cfg).value());
                    INFO("text=" << text << " style=" << style << " cands=" << candidates << " vToU=" << vToU);
                    CHECK(got == want);
                    cppp2_result_free(cr);
                }
            }
        }
    }

    cppp2_destroy(h);
}

TEST_CASE("C ABI: cantonese and capabilities go through unchanged", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_error err{};
    cppp2_engine *h = cppp2_open(nullptr, "cantonese", dir.string().c_str(), &err);
    REQUIRE(err.detail == nullptr);
    REQUIRE(h != nullptr);

    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "cantonese").value();

    // 0 / 3 / 8 are the styles Cantonese supports; an unsupported one is passthrough by contract.
    for (int style : {0, 3, 8, 1}) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = style;
        const cppp2_query_opts o = optsOf(cfg);
        cppp2_result *cr = nullptr;
        REQUIRE(cppp2_query(h, "粵語好正", &o, &cr) == CPPP2_OK);
        CHECK(fromC(cr) == fromCpp(cpp.query("粵語好正", cfg).value()));
        cppp2_result_free(cr);
    }

    // vToU / markNeutralAs5 are masked by the dialect's capability flags -- both APIs see the same.
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = 8;
    cfg.vToU = true;
    cfg.markNeutralAs5 = true;
    const cppp2_query_opts o = optsOf(cfg);
    cppp2_result *cr = nullptr;
    REQUIRE(cppp2_query(h, "女你好", &o, &cr) == CPPP2_OK);
    CHECK(fromC(cr) == fromCpp(cpp.query("女你好", cfg).value()));
    cppp2_result_free(cr);

    cppp2_destroy(h);
}

TEST_CASE("C ABI: readings reports nothing as NULL-with-count-0, and empty input is not an error", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    // The documented "there is nothing here" shape for this entry point (`cppp2_dialect_ids` uses the
    // same one): a NULL array with count 0, never an allocated empty array -- and never an INVALID_ARG
    // for an empty INPUT, which the contract calls a legal empty result.
    char **list = reinterpret_cast<char **>(0x1);
    size_t count = 99;
    CHECK(cppp2_readings(h, "", nullptr, &list, &count) == CPPP2_OK);
    CHECK(list == nullptr);
    CHECK(count == 0);

    // ...while a non-empty input that simply has no table reading answers with ITSELF (the rule
    // `Segment::candidates` states): 1 element, not the empty shape, so the two cases stay apart.
    list = nullptr;
    count = 0;
    REQUIRE(cppp2_readings(h, "\xe5\x95\x8e", nullptr, &list, &count) == CPPP2_OK);
    REQUIRE(count == 1);
    CHECK(std::string(list[0]) == "\xe5\x95\x8e");
    cppp2_string_list_free(list, count);

    cppp2_destroy(h);
}

TEST_CASE("C ABI: readings / toSimplified / isPolyphonic match the C++ API", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);
    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "mandarin").value();

    for (const char *ch : {"行", "你", "重", "长", "一"}) {
        for (int style : {0, 1, 3, 6, 8}) {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = style;
            const cppp2_query_opts o = optsOf(cfg);

            char **list = nullptr;
            size_t count = 0;
            REQUIRE(cppp2_readings(h, ch, &o, &list, &count) == CPPP2_OK);
            const std::vector<std::string> want = cpp.readings(ch, cfg).value();
            REQUIRE(count == want.size());
            for (size_t i = 0; i < count; ++i)
                CHECK(std::string(list[i]) == want[i]);
            cppp2_string_list_free(list, count);
        }

        int32_t poly = -1;
        REQUIRE(cppp2_is_polyphonic(h, ch, &poly) == CPPP2_OK);
        CHECK((poly != 0) == cpp.isPolyphonic(ch));

        char *simp = nullptr;
        REQUIRE(cppp2_to_simplified(h, ch, &simp) == CPPP2_OK);
        CHECK(std::string(simp) == cpp.toSimplified(ch).value_or_throw());
        cppp2_string_free(simp);
    }

    cppp2_destroy(h);
}

TEST_CASE("C ABI: dialect list comes from the registry it is handed", "[c_api][registry]") {
    char **ids = nullptr;
    size_t count = 0;
    REQUIRE(cppp2_dialect_ids(nullptr, &ids, &count) == CPPP2_OK);
    REQUIRE(count >= 2);

    std::vector<std::string> got;
    for (size_t i = 0; i < count; ++i)
        got.emplace_back(ids[i]);
    cppp2_string_list_free(ids, count);

    CHECK(got == testRegistry().ids());
    CHECK(std::find(got.begin(), got.end(), "mandarin") != got.end());
    CHECK(std::find(got.begin(), got.end(), "cantonese") != got.end());

    // A registry the caller owns is what the nullable `cppp2_registry *` is FOR (Q3): an id the host
    // registered must be visible through the C face, which is the only way a C-facing caller can reach
    // a dialect that no built-in covers. The creator is deliberately inert -- `ids()` never calls it.
    cpp_pinyin::v2::Registry mine;
    REQUIRE(mine.add("example-extra", [] { return std::make_shared<const cpp_pinyin::v2::Dialect>(); }));
    const cppp2_registry *handle = reinterpret_cast<const cppp2_registry *>(&mine);

    ids = nullptr;
    count = 0;
    REQUIRE(cppp2_dialect_ids(handle, &ids, &count) == CPPP2_OK);
    got.clear();
    for (size_t i = 0; i < count; ++i)
        got.emplace_back(ids[i]);
    cppp2_string_list_free(ids, count);
    CHECK(got == mine.ids());
    CHECK(std::find(got.begin(), got.end(), "example-extra") != got.end());
    // ...and the built-in list is untouched by that: the registry is an argument, not a global.
    ids = nullptr;
    count = 0;
    REQUIRE(cppp2_dialect_ids(nullptr, &ids, &count) == CPPP2_OK);
    got.clear();
    for (size_t i = 0; i < count; ++i)
        got.emplace_back(ids[i]);
    cppp2_string_list_free(ids, count);
    CHECK(std::find(got.begin(), got.end(), "example-extra") == got.end());

    // cppp2_open resolves through the SAME argument: "example-extra" is unknown to the built-ins
    // (NOT_READY) and known to `mine` (LOAD, because the inert dialect has no dictionary under
    // `dir`). Without the registry parameter the second call could not be told from the first.
    const std::filesystem::path dir = dictRoot();
    cppp2_error err{};
    CHECK(cppp2_open(nullptr, "example-extra", dir.string().c_str(), &err) == nullptr);
    CHECK(err.code == CPPP2_ERR_NOT_READY);
    REQUIRE(err.detail != nullptr);
    CHECK(std::string(err.detail).find("example-extra") != std::string::npos);
    cppp2_string_free(err.detail);

    err = cppp2_error{};
    CHECK(cppp2_open(handle, "example-extra", dir.string().c_str(), &err) == nullptr);
    CHECK(err.code == CPPP2_ERR_LOAD);
    cppp2_string_free(err.detail);
}

TEST_CASE("C ABI: the codes and their text have one authority each", "[c_api][error]") {
    // The numbers are the C++ `Code` values (the implementation pins that with static_assert); this
    // case pins the other half -- that the text comes from the library and not from a second table
    // that could drift from `cpp_pinyin::v2::codeText`.
    for (int32_t code = CPPP2_OK; code <= CPPP2_ERR_INTERNAL; ++code)
        CHECK(std::string(cppp2_code_text(code)) == cpp_pinyin::v2::codeText(static_cast<cpp_pinyin::v2::Code>(code)));
    CHECK(std::string(cppp2_code_text(CPPP2_ERR_NOT_READY)) == "not ready");
    // A value outside the domain: reported, not a crash and not an empty string.
    CHECK(std::string(cppp2_code_text(999)) == "unknown code");
}

TEST_CASE("C ABI: errors are reported, never swallowed", "[c_api][error]") {
    const std::filesystem::path dir = dictRoot();

    SECTION("unknown dialect id") {
        cppp2_error err{};
        cppp2_engine *h = cppp2_open(nullptr, "example-not-registered", dir.string().c_str(), &err);
        CHECK(h == nullptr);
        CHECK(err.code == CPPP2_ERR_NOT_READY);  // an id that does not resolve is not "invalid argument"
        REQUIRE(err.detail != nullptr);
        // c_api.h promises the detail NAMES the id, so a shell can say which one was wrong.
        CHECK(std::string(err.detail).find("example-not-registered") != std::string::npos);
        CHECK(std::string(err.detail).find("unknown") != std::string::npos);
        cppp2_string_free(err.detail);
    }

    SECTION("dict_dir is mandatory -- no implicit path") {
        cppp2_error err{};
        CHECK(cppp2_open(nullptr, "mandarin", nullptr, &err) == nullptr);
        CHECK(err.code == CPPP2_ERR_INVALID_ARG);
        REQUIRE(err.detail != nullptr);
        cppp2_string_free(err.detail);
    }

    SECTION("a dict dir without the files fails loudly and names what was looked for") {
        const std::filesystem::path empty = std::filesystem::current_path() / "no-such-dict-root";
        cppp2_error err{};
#ifdef CPP_PINYIN2_EMBED_DICTS
        // An embedded build carries the tables, so this root is not a failure -- the library answers from
        // itself. The opposite assertion is the non-embedded case below (D3,
        // docs/DECISIONS.md section 4.1).
        const cppp2_engine *handle = cppp2_open(nullptr, "mandarin", empty.string().c_str(), &err);
        REQUIRE(handle != nullptr);
        CHECK(err.code == CPPP2_OK);
        cppp2_destroy(const_cast<cppp2_engine *>(handle));
#else
        CHECK(cppp2_open(nullptr, "mandarin", empty.string().c_str(), &err) == nullptr);
        CHECK(err.code == CPPP2_ERR_LOAD);  // the id resolved; the DICTIONARY is what failed
        REQUIRE(err.detail != nullptr);
        CHECK(std::string(err.detail).find("mandarin") != std::string::npos);
        cppp2_string_free(err.detail);
#endif
    }

    SECTION("success leaves the channel empty, and no channel at all is allowed") {
        cppp2_error err{};
        err.code = CPPP2_ERR_INTERNAL;  // garbage a call must overwrite
        err.detail = nullptr;
        cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), &err);
        REQUIRE(h != nullptr);
        CHECK(err.code == CPPP2_OK);
        CHECK(err.detail == nullptr);

        char **ids = nullptr;
        size_t count = 0;
        CHECK(cppp2_dialect_ids(nullptr, &ids, &count) == CPPP2_OK);  // NULL err: no channel wanted
        cppp2_string_list_free(ids, count);
        cppp2_destroy(h);
    }

    SECTION("null handles and null arguments return status codes, not crashes") {
        cppp2_result *r = nullptr;
        CHECK(cppp2_query(nullptr, "你好", nullptr, &r) == CPPP2_ERR_INVALID_ARG);
        CHECK(r == nullptr);

        cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
        REQUIRE(h != nullptr);
        CHECK(cppp2_query(h, nullptr, nullptr, &r) == CPPP2_ERR_INVALID_ARG);
        CHECK(cppp2_query(h, "你好", nullptr, nullptr) == CPPP2_ERR_INVALID_ARG);
        CHECK(cppp2_to_simplified(h, nullptr, nullptr) == CPPP2_ERR_INVALID_ARG);
        CHECK(cppp2_readings(h, "你", nullptr, nullptr, nullptr) == CPPP2_ERR_INVALID_ARG);

        // cppp2_render_syllable: NULL arguments, and (a real argument, not a null one) a structure that
        // describes nothing -- the default-constructed one a callback-written row carries.
        cppp2_query_opts ro{};
        cppp2_query_opts_init(&ro);
        char *rendered = reinterpret_cast<char *>(&ro);
        CHECK(cppp2_render_syllable(h, nullptr, &ro, &rendered) == CPPP2_ERR_INVALID_ARG);
        CHECK(rendered == nullptr);
        const cppp2_syllable nothing{};
        rendered = reinterpret_cast<char *>(&ro);
        CHECK(cppp2_render_syllable(h, &nothing, &ro, &rendered) == CPPP2_ERR_INVALID_ARG);
        CHECK(rendered == nullptr);
        CHECK(cppp2_render_syllable(nullptr, &nothing, &ro, &rendered) == CPPP2_ERR_INVALID_ARG);
        CHECK(cppp2_render_syllable(h, &nothing, &ro, nullptr) == CPPP2_ERR_INVALID_ARG);

        CHECK(cppp2_load_user_dict(h, nullptr, 1) == CPPP2_ERR_INVALID_ARG);

        // A missing user dictionary is a refusal, not a silent success.
        const std::string missing = (dir / "not-a-file.txt").string();
        CHECK(cppp2_load_user_dict(h, missing.c_str(), 1) == CPPP2_ERR_LOAD);

        // The `_ex` spelling returns the SAME code and additionally says WHY (2026-10-20, audit H04). Two
        // properties matter and neither is visible from the header alone: the code must not differ between the
        // spellings (a host migrating must not have to change its branches), and `detail` must be a caller-owned
        // string that actually arrives -- NULL there would make the whole entry point pointless.
        cppp2_error tagged{};
        tagged.code = -1;
        tagged.detail = reinterpret_cast<char *>(&tagged);
        CHECK(cppp2_load_user_dict_ex(h, missing.c_str(), 1, &tagged) == CPPP2_ERR_LOAD);
        CHECK(tagged.code == CPPP2_ERR_LOAD);
        REQUIRE(tagged.detail != nullptr);
        CHECK(std::string(tagged.detail).find("not-a-file.txt") != std::string::npos);
        cppp2_string_free(tagged.detail);

        // Success writes BOTH fields, so a host never pre-initialises and never keeps the previous message.
        cppp2_error cleared{};
        cleared.code = -1;
        cleared.detail = reinterpret_cast<char *>(&cleared);
        const std::string realUserDict = (dir / "mandarin" / "user_dict.txt").string();
        CHECK(cppp2_load_user_dict_ex(h, realUserDict.c_str(), 1, &cleared) == CPPP2_OK);
        CHECK(cleared.code == CPPP2_OK);
        CHECK(cleared.detail == nullptr);
        cppp2_destroy(h);

        // The free functions accept NULL, so a caller needs no null checks of its own.
        cppp2_destroy(nullptr);
        cppp2_string_free(nullptr);
        cppp2_string_list_free(nullptr, 0);
        cppp2_result_free(nullptr);
    }
}

TEST_CASE("C ABI: struct_size rules -- a smaller struct is read only where it says", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);
    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "mandarin").value();

    // A caller built against the first version of the struct only knows struct_size + style.
    cppp2_query_opts old{};
    old.struct_size = offsetof(cppp2_query_opts, on_error);  // everything after it is "absent"
    old.style = 1;  // TONE
    old.on_error = 1;  // must be ignored (not covered)
    old.include_candidates = 0;  // must be ignored -> defaults to true
    old.v_to_u = 1;  // must be ignored
    old.mark_neutral_as_5 = 1;  // must be ignored

    cppp2_result *cr = nullptr;
    REQUIRE(cppp2_query(h, "女一", &old, &cr) == CPPP2_OK);

    cpp_pinyin::v2::EngineConfig cfg;  // defaults: includeCandidates=true, vToU=false, markNeutralAs5=false
    cfg.style = 1;
    CHECK(fromC(cr) == fromCpp(cpp.query("女一", cfg).value()));
    cppp2_result_free(cr);

    // `struct_size == 0` means "my copy is exactly the declaration in this header" (c_api.h), so the
    // fields the caller FILLED are read: a zeroed struct that says TONE3 renders TONE3, and its
    // `include_candidates = 0` is honoured. Until 2026-10-18 this block pinned the opposite reading
    // ("covers nothing, so every field falls back to the documented default") -- i.e. it pinned a
    // caller's explicit `style` being dropped with no diagnostic. That is the defect this now guards.
    cppp2_query_opts zero{};
    zero.style = 8;  // TONE3
    zero.include_candidates = 0;  // honoured: candidates are not built
    REQUIRE(cppp2_query(h, "你好", &zero, &cr) == CPPP2_OK);
    cpp_pinyin::v2::EngineConfig sameRequest;
    sameRequest.style = 8;
    sameRequest.includeCandidates = false;
    CHECK(fromC(cr) == fromCpp(cpp.query("你好", sameRequest).value()));
    cppp2_result_free(cr);
    CHECK(zero.struct_size == 0);  // the library does not write back into the caller's struct

    // NULL means "all defaults" and must equal a fully initialised default struct.
    cppp2_query_opts def{};
    cppp2_query_opts_init(&def);
    REQUIRE(cppp2_query(h, "你好", nullptr, &cr) == CPPP2_OK);
    const auto viaNull = fromC(cr);
    cppp2_result_free(cr);
    REQUIRE(cppp2_query(h, "你好", &def, &cr) == CPPP2_OK);
    CHECK(viaNull == fromC(cr));
    cppp2_result_free(cr);

    cppp2_destroy(h);
}

TEST_CASE("C ABI: an option must not land in an older declaration's tail padding", "[c_api][engine]") {
    // THE BUG THIS PINS (S9c): `include_reading` was appended at offset 28, which in the FIRST
    // declaration of the struct was TRAILING PADDING. A caller built against that declaration reports
    // `struct_size` 32, so it *does* cover 28..31 and the library cannot tell padding from a declared
    // field: with 0x5A there, a caller that asked for nothing got the structure anyway (measured: 4 of
    // 7 rows). The contract: an option must live where no older declaration's `struct_size` can reach
    // it, and an old caller then gets the documented default -- for `include_reading` 0, "not
    // requested", NOT "this span has no reading".
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    // The declaration a shell built against the pre-S9c header had, spelled out field by field: the
    // last one it ever declared is `mark_neutral_as_5`, and whatever follows is padding, not an option.
    struct OldQueryOptsV1 {
        size_t struct_size;
        int32_t style;
        int32_t on_error;
        int32_t include_candidates;
        int32_t v_to_u;
        int32_t mark_neutral_as_5;
        int32_t old_tail_padding;  // 28..31 in V1: never written, never read, never declared
    };
    static_assert(sizeof(OldQueryOptsV1) == 32, "the V1 declaration of cppp2_query_opts is 32 bytes");
    CHECK(offsetof(cppp2_query_opts, include_reading) >= sizeof(OldQueryOptsV1));

    OldQueryOptsV1 v1{};
    std::memset(&v1, 0x5A, sizeof v1);  // every byte garbage, exactly like an uninitialised frame
    v1.struct_size = sizeof v1;  // 32 -- the only size a V1 caller can declare
    v1.style = 1;  // TONE
    v1.on_error = 0;
    v1.include_candidates = 0;
    v1.v_to_u = 0;
    v1.mark_neutral_as_5 = 0;

    // What that caller hands in: a 32-byte struct. Bytes past 32 are not part of the call.
    cppp2_query_opts opts{};
    std::memcpy(&opts, &v1, sizeof v1);

    cpp_pinyin::v2::EngineConfig cfg;  // the old caller's config: no structure (the default), no candidates
    cfg.style = 1;
    cfg.includeCandidates = false;
    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "mandarin").value();

    cppp2_result *cr = nullptr;
    REQUIRE(cppp2_query(h, "你好世界", &opts, &cr) == CPPP2_OK);
    REQUIRE(cr->count == 4);
    for (size_t i = 0; i < cr->count; ++i) {
        const cppp2_segment &s = cr->segments[i];
        INFO("row " << i << " (" << s.text << ")");
        CHECK(s.failure_kind == CPPP2_OK);  // so the two checks below are decisions, not failures
        CHECK(s.syllables == nullptr);  // "the caller did not request the structure"...
        CHECK(s.syllable_count == 0);
    }
    // ...and NOT "this span has no reading": the whole call is still the call a V1 caller always got.
    CHECK(fromC(cr) == fromCpp(cpp.query("你好世界", cfg).value()));
    cppp2_result_free(cr);
    cppp2_destroy(h);
}

TEST_CASE("C ABI: a shared handle survives concurrent readers", "[c_api][thread]") {
    // This is the C#/Python usage pattern, not a synthetic one: both bindings may call into the
    // library from several threads at once (a ctypes CDLL call releases the GIL), so the contract
    // "read functions are safe on a shared handle" has to hold through the marshalling layer too --
    // that layer allocates, and an allocation bug here would only show up under contention.
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    cppp2_query_opts opts{};
    cppp2_query_opts_init(&opts);
    opts.style = 1;

    cppp2_result *baseline = nullptr;
    REQUIRE(cppp2_query(h, "中国银行 21 世界", &opts, &baseline) == CPPP2_OK);
    const auto want = fromC(baseline);
    cppp2_result_free(baseline);

    constexpr int kThreads = 4;
    constexpr int kRounds = 40;
    std::vector<std::thread> workers;
    std::atomic<int> mismatches{0};
    std::atomic<int> errors{0};
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back(
            [&]()
            {
                for (int i = 0; i < kRounds; ++i) {
                    cppp2_result *r = nullptr;
                    if (cppp2_query(h, "中国银行 21 世界", &opts, &r) != CPPP2_OK || r == nullptr) {
                        ++errors;
                        continue;
                    }
                    if (fromC(r) != want)
                        ++mismatches;
                    cppp2_result_free(r);

                    char **readings = nullptr;
                    size_t count = 0;
                    if (cppp2_readings(h, "行", &opts, &readings, &count) != CPPP2_OK || count == 0)
                        ++errors;
                    cppp2_string_list_free(readings, count);
                }
            });
    }
    for (std::thread &w : workers)
        w.join();

    CHECK(errors == 0);
    CHECK(mismatches == 0);
    cppp2_destroy(h);
}

TEST_CASE("C ABI: strings we hand out are ours to free, and are not shared", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    // Two results in flight at once: freeing one must not disturb the other (each result owns
    // its own copies rather than pointing into engine state).
    cppp2_result *a = nullptr;
    cppp2_result *b = nullptr;
    REQUIRE(cppp2_query(h, "你好世界", nullptr, &a) == CPPP2_OK);
    REQUIRE(cppp2_query(h, "你好世界", nullptr, &b) == CPPP2_OK);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto sa = fromC(a);
    cppp2_result_free(a);
    CHECK(fromC(b) == sa);
    CHECK(sa.size() == 4);
    cppp2_result_free(b);

    CHECK(std::string(cppp2_version()).find('.') != std::string::npos);

    // D2: the candidate is the build's install layout resolved against the library's own module. A static
    // build has no anchor -- and saying so is the documented answer, not a failure.
    const char *candidate = cppp2_dict_dir_candidate();
#ifdef CPP_PINYIN2_STATIC
    CHECK(candidate == nullptr);
#else
    REQUIRE(candidate != nullptr);
    CHECK(std::string(candidate).find("share/cpp-pinyin2/dict") != std::string::npos);
#endif
    cppp2_destroy(h);
}

TEST_CASE("C ABI: the structured reading is opt-in, and one span is one syllable", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);
    const cpp_pinyin::v2::Engine cpp = cpp_pinyin::v2::Engine::open(testRegistry(), dir, "mandarin").value();
    // The engine is opened with .value(), which throws if it fails -- an "is it ready"
    // assertion here would be vacuous.

    // Two ASCII/digit runs, the space between them and four hanzi -- the ASCII runs and the space are
    // spans of their own, and it is exactly those that must not enter the syllable array.
    const std::string text = "hello 中国银行 21";

    cpp_pinyin::v2::EngineConfig cfgOff;
    cfgOff.style = 8;
    const cppp2_query_opts oOff = optsOf(cfgOff);
    cppp2_result *rOff = nullptr;
    REQUIRE(cppp2_query(h, text.c_str(), &oOff, &rOff) == CPPP2_OK);
    CHECK(rOff->count >= 5);
    // The default: no structure ANYWHERE, and that is "not requested", not "no reading" -- see below.
    for (size_t i = 0; i < rOff->count; ++i) {
        CHECK(rOff->segments[i].syllables == nullptr);
        CHECK(rOff->segments[i].syllable_count == 0);
    }

    cpp_pinyin::v2::EngineConfig cfgOn = cfgOff;
    cfgOn.includeReading = true;
    const cppp2_query_opts oOn = optsOf(cfgOn);
    cppp2_result *rOn = nullptr;
    REQUIRE(cppp2_query(h, text.c_str(), &oOn, &rOn) == CPPP2_OK);
    REQUIRE(rOn->count == rOff->count);
    // Turning the switch on changes the structure column and NOTHING else -- byte for byte.
    CHECK(fromC(rOn) == fromC(rOff));

    const cpp_pinyin::v2::Result want = cpp.query(text, cfgOn).value();
    REQUIRE(want.size() == rOn->count);
    size_t nonFailed = 0;
    size_t failedSpans = 0;
    for (size_t i = 0; i < rOn->count; ++i) {
        const cppp2_segment &s = rOn->segments[i];
        INFO("span " << i << " text=" << s.text << " failure_kind=" << s.failure_kind);
        CHECK(s.failure_kind == static_cast<int32_t>(want[i].failure_kind));
        if (s.failure_kind != CPPP2_OK) {
            // A failed span does NOT enter the syllable array, and its rendered string is its own text.
            ++failedSpans;
            CHECK(s.syllables == nullptr);
            CHECK(s.syllable_count == 0);
            CHECK(std::string(s.rendered) == want[i].text);
            CHECK(std::string(cppp2_code_text(s.failure_kind)) != "ok");
            continue;
        }
        ++nonFailed;
        REQUIRE(s.syllables != nullptr);
        CHECK(s.syllable_count == 1);  // one input span is one syllable (A2)
        CHECK(readingsOf(s.syllables, s.syllable_count) ==
              std::vector<ReadingTuple>{ReadingTuple{want[i].reading.initial, want[i].reading.fin, want[i].reading.tone,
                                                     static_cast<int>(want[i].reading.tone_kind),
                                                     static_cast<int>(want[i].reading.source)}});
    }
    CHECK(nonFailed == 4);  // 中国银行: one syllable per hanzi
    CHECK(failedSpans == rOn->count - nonFailed);  // everything else, and nothing else
    cppp2_result_free(rOn);
    cppp2_result_free(rOff);
    cppp2_destroy(h);
}

TEST_CASE("C ABI: render_syllable keeps the style in the library and rejects a separator", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    // The row's own structure, rendered through the row's own style, IS the row's own bytes: that is
    // what the entry is for (a shell asks the library instead of spelling the style out itself).
    for (int style : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = style;
        cfg.includeReading = true;
        const cppp2_query_opts o = optsOf(cfg);
        cppp2_result *r = nullptr;
        REQUIRE(cppp2_query(h, "行", &o, &r) == CPPP2_OK);
        REQUIRE(r->count == 1);
        REQUIRE(r->segments[0].syllable_count == 1);
        char *out = nullptr;
        INFO("style=" << style);
        CHECK(cppp2_render_syllable(h, r->segments[0].syllables, &o, &out) == CPPP2_OK);
        REQUIRE(out != nullptr);
        CHECK(std::string(out) == r->segments[0].rendered);
        cppp2_string_free(out);
        cppp2_result_free(r);
    }

    // The same for cantonese, whose (initial, fin) is jyutping and whose tone is its last character:
    // the notation is the dialect's, so this cannot be faked with the mandarin path.
    {
        cppp2_engine *c = cppp2_open(nullptr, "cantonese", dir.string().c_str(), nullptr);
        REQUIRE(c != nullptr);
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = 8;
        cfg.includeReading = true;
        const cppp2_query_opts o = optsOf(cfg);
        cppp2_result *r = nullptr;
        REQUIRE(cppp2_query(c, "粵語", &o, &r) == CPPP2_OK);
        REQUIRE(r->count == 2);
        char *out = nullptr;
        CHECK(cppp2_render_syllable(c, r->segments[1].syllables, &o, &out) == CPPP2_OK);
        REQUIRE(out != nullptr);
        CHECK(std::string(out) == r->segments[1].rendered);
        cppp2_string_free(out);
        cppp2_result_free(r);
        cppp2_destroy(c);
    }

    // THE SINGLE-SYLLABLE CONTRACT, checked rather than assumed (plan A2): a whole multi-syllable
    // string in one syllable's field cannot be rendered, and saying so beats silently rendering a
    // fragment. Every separator any surface of this library uses between syllables is covered.
    cppp2_query_opts o{};
    cppp2_query_opts_init(&o);
    for (const char *fin : {"zhi4 fa2", "ni,hao", "zhi4:fa2", "ni-hao", "ni'hao", "ni/hao", "ni\thao", "ni\nhao"}) {
        cppp2_syllable s{};
        s.initial = "";
        s.fin = fin;
        char *out = reinterpret_cast<char *>(&o);
        INFO("fin=" << fin);
        CHECK(cppp2_render_syllable(h, &s, &o, &out) == CPPP2_ERR_INVALID_ARG);
        CHECK(out == nullptr);
    }
    // A separator in `initial` is the same answer, and one that is not a separator at all is fine.
    {
        cppp2_syllable s{};
        s.initial = "n i";
        s.fin = "i3";
        char *out = nullptr;
        CHECK(cppp2_render_syllable(h, &s, &o, &out) == CPPP2_ERR_INVALID_ARG);
        CHECK(out == nullptr);
    }

    cppp2_destroy(h);
}

TEST_CASE("C ABI: a result outlives the handle that produced it", "[c_api][engine]") {
    const std::filesystem::path dir = dictRoot();
    cppp2_engine *h = cppp2_open(nullptr, "mandarin", dir.string().c_str(), nullptr);
    REQUIRE(h != nullptr);

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.includeReading = true;
    const cppp2_query_opts o = optsOf(cfg);
    cppp2_result *r = nullptr;
    REQUIRE(cppp2_query(h, "你好世界", &o, &r) == CPPP2_OK);
    const auto rows = fromC(r);
    const auto structure = readingsOf(r->segments[0].syllables, r->segments[0].syllable_count);
    CHECK(structure.size() == 1);

    cppp2_destroy(h);

    // Everything a result points at is its own copy -- segments, rendered strings, syllables, the
    // syllable strings and the candidates -- so it is still readable after the handle is gone. It is
    // still the RESULT's owner that releases it (`cppp2_result_free`), and nothing else.
    CHECK(fromC(r) == rows);
    CHECK(readingsOf(r->segments[0].syllables, r->segments[0].syllable_count) == structure);
    CHECK(rows.size() == 4);
    cppp2_result_free(r);
}

// H05 (2026-10-20, audit round 3): the install-location question had a C face and both shells and NO C++
// face -- while its intended readers are exactly the hosts without a CMake package or a .pc file. The two
// spellings now sit on one cache, so this case is what keeps them from drifting: whatever the anchor is in
// this build (empty under CPP_PINYIN2_STATIC, a real path in a shared build), the two must agree.
TEST_CASE("C ABI: dict_dir_candidate and the C++ face answer the same thing", "[c_api][dict]") {
    const char *fromC = cppp2_dict_dir_candidate();
    const std::string fromCpp = cpp_pinyin::v2::dictDirCandidate();
    if (fromCpp.empty())
        CHECK(fromC == nullptr);
    else {
        REQUIRE(fromC != nullptr);
        CHECK(std::string(fromC) == fromCpp);
    }
    // A cached process-wide value: asking twice cannot change the answer.
    CHECK(cpp_pinyin::v2::dictDirCandidate() == fromCpp);
}
