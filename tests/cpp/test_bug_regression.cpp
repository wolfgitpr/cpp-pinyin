#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <cpp-pinyin2/core/G2pglobal.h>
#include <cpp-pinyin2/core/U16Str.h>
#include "dialect_fixtures.h"

namespace
{
    struct BugFixture {
        cpp_pinyin::v2::Engine man;
        cpp_pinyin::v2::Engine can;

        BugFixture() : man(cpp_test::mandarin()), can(cpp_test::cantonese()) {}
    };
}  // namespace

static std::string qm(cpp_pinyin::v2::Engine &m, const std::string &text,
                      cpp_pinyin::v2::MandarinStyle style = cpp_pinyin::v2::MandarinStyle::TONE3,
                      cpp_pinyin::v2::ErrorHandling err = cpp_pinyin::v2::ErrorHandling::Default,
                      bool candidates = true, bool n5 = false, bool v2u = false) {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = style;
    cfg.onError = err;
    cfg.includeCandidates = candidates;
    cfg.markNeutralAs5 = n5;
    cfg.vToU = v2u;
    return m.query(text, cfg).value().join();
}

TEST_CASE_METHOD(BugFixture, "BUG-002: Kana merging with small kana (post-fix)", "[bug002][kana]") {
    CHECK(qm(man, u8"東京タワー", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"dong jing タ ワ ー");
    CHECK(qm(man, u8"举杯あャ坐ュ饮", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"ju bei あャ zuo ュ yin");
    CHECK(qm(man, u8"あいう", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"あ い う");
}

TEST_CASE_METHOD(BugFixture, "BUG-003: Error::Ignore filters non-hanzi (post-fix)", "[bug003][error]") {
    CHECK(qm(man, u8"hello世界", cpp_pinyin::v2::MandarinStyle::TONE3, cpp_pinyin::v2::ErrorHandling::Drop, false) ==
          u8"shi4 jie4");
    CHECK(qm(man, u8"明月@1几32时有##一", cpp_pinyin::v2::MandarinStyle::TONE3, cpp_pinyin::v2::ErrorHandling::Drop,
             false) == u8"ming2 yue4 ji3 shi2 you3 yi1");
    CHECK(qm(man, u8"abc", cpp_pinyin::v2::MandarinStyle::TONE3, cpp_pinyin::v2::ErrorHandling::Drop, false).empty());
    CHECK(qm(man, u8"好 test 的", cpp_pinyin::v2::MandarinStyle::TONE3, cpp_pinyin::v2::ErrorHandling::Drop, false) ==
          u8"hao3 de");
}

TEST_CASE_METHOD(BugFixture, "BUG-004: Empty/single-char input safety (post-fix only)", "[bug004][empty][!mayfail]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    CHECK(man.query(u8"", cfg).value().empty());
}

TEST_CASE_METHOD(BugFixture, "BUG-007: getDefaultPinyin sequential consistency", "[bug007][thread_safety]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;

    for (int round = 0; round < 5; round++) {
        auto res_xing = man.readings(u8"行", cfg).value();
        auto res_le = man.readings(u8"了", cfg).value();
        auto res_de = man.readings(u8"的", cfg).value();

        INFO("Round " << round);
        REQUIRE(res_xing.size() == 4);
        CHECK(res_xing[0] == "xing2");
        CHECK(res_xing[1] == "hang2");

        REQUIRE(res_le.size() == 2);
        CHECK(res_le[0] == "le");
        CHECK(res_le[1] == "liao3");

        REQUIRE(res_de.size() == 4);
        CHECK(res_de[0] == "de");
        CHECK(res_de[1] == "di4");
    }
}


TEST_CASE("U16Str round-trip", "[utf8][u16str]") {
    {
        std::string s = "A";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
    {
        std::string s = u8"é";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
    {
        std::string s = u8"中";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
    {
        std::string s = u8"𠀀";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
    {
        std::string s = u8"中文测试";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
    {
        char16_t ch = u'A';
        CHECK(cpp_pinyin::v2::u16strToUtf8str(ch) == "A");
    }
    {
        char16_t ch = u'中';
        auto result = cpp_pinyin::v2::u16strToUtf8str(ch);
        CHECK(result == u8"中");
    }
    {
        std::string s = u8"āáǎàōóǒòēéěè";
        auto u16 = cpp_pinyin::v2::utf8strToU16str(s);
        CHECK(cpp_pinyin::v2::u16strToUtf8str(u16) == s);
    }
}

TEST_CASE_METHOD(BugFixture, "Result::toVector", "[api][result]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"你好", cfg).value();
    auto vec = res.toVector();
    REQUIRE(vec.size() == 2);
    CHECK(vec[0] == "ni3");
    CHECK(vec[1] == "hao3");
}

TEST_CASE_METHOD(BugFixture, "Result::join with custom delimiter", "[api][result]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"你好", cfg).value();
    CHECK(res.join("-") == "ni3-hao3");
    CHECK(res.join("") == "ni3hao3");
    CHECK(res.join("||") == "ni3||hao3");
}

TEST_CASE_METHOD(BugFixture, "Segment candidates for polyphonic chars", "[api][candidates]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.includeCandidates = true;
    auto res = man.query(u8"行", cfg).value();
    REQUIRE(res.size() == 1);
    CHECK((res[0].failure_kind != cpp_pinyin::v2::Code::Ok) == false);
    CHECK(res[0].candidates.size() >= 2);

    cfg.includeCandidates = false;
    auto res2 = man.query(u8"行", cfg).value();
    REQUIRE(res2.size() == 1);
    CHECK(res2[0].candidates.empty());
}

TEST_CASE_METHOD(BugFixture, "Segment failed flag for non-hanzi", "[api][failed_flag]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"hello你好123", cfg).value();

    bool foundHanziOk = false;
    bool foundNonHanziFail = false;
    for (const auto &s : res) {
        if (s.text == u8"你" || s.text == u8"好") {
            CHECK((s.failure_kind != cpp_pinyin::v2::Code::Ok) == false);
            foundHanziOk = true;
        }
        if (s.text == "1" || s.text == "2" || s.text == "3" || s.text == "hello") {
            CHECK((s.failure_kind != cpp_pinyin::v2::Code::Ok) == true);
            foundNonHanziFail = true;
        }
    }
    CHECK(foundHanziOk);
    CHECK(foundNonHanziFail);
}

TEST_CASE_METHOD(BugFixture, "query vector overload", "[api][vector_input]") {
    std::vector<std::string> input = {u8"你", u8"好", u8"世", u8"界"};
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    CHECK(man.query(input, cfg).value().join() == u8"ni3 hao3 shi4 jie4");
}

TEST_CASE("G2pglobal isLetter", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isLetter(u'a') == true);
    CHECK(cpp_pinyin::v2::isLetter(u'z') == true);
    CHECK(cpp_pinyin::v2::isLetter(u'A') == true);
    CHECK(cpp_pinyin::v2::isLetter(u'Z') == true);
    CHECK(cpp_pinyin::v2::isLetter(u'0') == false);
    CHECK(cpp_pinyin::v2::isLetter(u' ') == false);
    CHECK(cpp_pinyin::v2::isLetter(u'\u4e00') == false);
}

TEST_CASE("G2pglobal isHanzi", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isHanzi(u'\u4e00') == true);
    CHECK(cpp_pinyin::v2::isHanzi(u'\u9fa5') == true);

    // FR-4.6's target range, in three parts: Extension A / the basic block / compatibility ideographs.
    // The old implementation used [0x4E00,0x9FA5], so every boundary below was false back then.
    CHECK(cpp_pinyin::v2::isHanzi(u'\u3400') == true);  // first of Extension A
    CHECK(cpp_pinyin::v2::isHanzi(u'\u4dbf') == true);  // last of Extension A
    CHECK(cpp_pinyin::v2::isHanzi(u'\u4dc0') == false);  // the 64 I Ching hexagram signs, not ideographs
    CHECK(cpp_pinyin::v2::isHanzi(u'\u4dff') == false);
    CHECK(cpp_pinyin::v2::isHanzi(u'\u9fa6') == true);  // added to the basic block after Unicode 4.1
    CHECK(cpp_pinyin::v2::isHanzi(u'\u9fff') == true);  // last of the basic block
    CHECK(cpp_pinyin::v2::isHanzi(u'\uf900') == true);  // first compatibility ideograph
    CHECK(cpp_pinyin::v2::isHanzi(u'\ufaff') == true);  // last compatibility ideograph
    CHECK(cpp_pinyin::v2::isHanzi(u'\ufb00') == false);
    CHECK(cpp_pinyin::v2::isHanzi(u'\u33ff') == false);  // the CJK compatibility (symbol) block
    CHECK(cpp_pinyin::v2::isHanzi(u'\u2fff') == false);

    CHECK(cpp_pinyin::v2::isHanzi(u'A') == false);
    CHECK(cpp_pinyin::v2::isHanzi(u'0') == false);
}

TEST_CASE("G2pglobal isKana", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isKana(u'\u3042') == true);
    CHECK(cpp_pinyin::v2::isKana(u'\u30A2') == true);
    CHECK(cpp_pinyin::v2::isKana(u'A') == false);
    CHECK(cpp_pinyin::v2::isKana(u'\u4e00') == false);
}

TEST_CASE("G2pglobal isDigit", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isDigit(u'0') == true);
    CHECK(cpp_pinyin::v2::isDigit(u'9') == true);
    CHECK(cpp_pinyin::v2::isDigit(u'a') == false);
}

TEST_CASE("G2pglobal isSpace", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isSpace(u' ') == true);
    CHECK(cpp_pinyin::v2::isSpace(u'a') == false);
    CHECK(cpp_pinyin::v2::isSpace(u'\t') == false);
}

TEST_CASE("G2pglobal isSpecialKana", "[g2pglobal][util]") {
    CHECK(cpp_pinyin::v2::isSpecialKana(u'\u30E3') == true);
    CHECK(cpp_pinyin::v2::isSpecialKana(u'\u30E5') == true);
    CHECK(cpp_pinyin::v2::isSpecialKana(u'\u30E7') == true);
    CHECK(cpp_pinyin::v2::isSpecialKana(u'\u3042') == false);
}

// K1 says "whitespace is skipped", but cpp_pinyin::v2::isSpace() is a public predicate about the ASCII
// space and answers false for '\t' / U+3000 (pinned by the test above). The tokenizer used to
// reuse that predicate, so every other whitespace character became a *failed row* instead of being
// skipped -- a silent divergence between the frozen contract and the implementation. Measured
// before the fix: "\u4f60\u597d\t\u4e16\u754c" produced 5 syllables, the tab one failed.
TEST_CASE_METHOD(BugFixture, "K1: whitespace characters are skipped, not emitted as failed rows", "[k1][whitespace]") {
    // ASCII space was already correct and must stay correct.
    CHECK(qm(man, u8"你好 世界") == "ni3 hao3 shi4 jie4");

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto sizeOf = [&](const std::string &text) { return man.query(text, cfg).value().size(); };

    CHECK(sizeOf(u8"你好\t世界") == 4);  // tab (C0)
    CHECK(sizeOf(u8"你好\v世界") == 4);  // vertical tab
    CHECK(sizeOf(u8"你好\f世界") == 4);  // form feed
    CHECK(sizeOf("\xe4\xbd\xa0\xe5\xa5\xbd\xe3\x80\x80\xe4\xb8\x96\xe7\x95\x8c") == 4);  // U+3000
    CHECK(sizeOf("\xe4\xbd\xa0\xe5\xa5\xbd\xc2\xa0\xe4\xb8\x96\xe7\x95\x8c") == 4);  // U+00A0
    CHECK(sizeOf("\xe4\xbd\xa0\xe5\xa5\xbd\xe2\x80\x89\xe4\xb8\x96\xe7\x95\x8c") == 4);  // U+2009
    CHECK(sizeOf("\xe4\xbd\xa0\xe5\xa5\xbd\xe2\x80\xa8\xe4\xb8\x96\xe7\x95\x8c") == 4);  // U+2028
    CHECK(sizeOf(u8"你好\r\n世界") == 4);  // the CRLF every Windows file has

    // U+200B is Unicode category Cf, not whitespace: it must stay a token of its own.
    CHECK(sizeOf("\xe4\xbd\xa0\xe5\xa5\xbd\xe2\x80\x8b\xe4\xb8\x96\xe7\x95\x8c") == 5);
}

// S-02: a dictionary file that opens but parses zero entries is malformed, not empty. It used to
// report success, so an engine whose shipped file had a wrong separator came up "ready" with no
// knowledge at all.
TEST_CASE_METHOD(BugFixture, "malformed dictionary is reported as a failure", "[loader][s02]") {
    const auto scratch = std::filesystem::temp_directory_path() / "cpp-pinyin2-empty-dict.txt";
    {
        std::ofstream out(scratch, std::ios::binary | std::ios::trunc);  // deliberately empty
        REQUIRE(out.is_open());
    }

    CHECK_FALSE(man.loadTraditionalDict(scratch));
    // The reason a load failed is read through lastError() today; with B2 removing that accessor, the
    // reason is to come back through a later slice's loading API (recorded as debt). The assertion that the
    // load FAILED is kept (the line above); only the reason check was dropped.

    // A file that does not exist is still a failure (unchanged behaviour).
    CHECK_FALSE(man.loadTraditionalDict(scratch.string() + ".missing"));

    std::error_code ec;
    std::filesystem::remove(scratch, ec);
}

// A broken user_dict.txt shipped next to the other dictionaries is loud: the engine comes up with
// isReady()==false and a lastError() naming the file. (The *absent* case stays silent and usable --
// see the "shipped user_dict.txt is optional" comment in EngineImpl::init().)
TEST_CASE("broken optional user_dict.txt makes the engine not ready", "[loader][s02]") {
    const auto root = std::filesystem::temp_directory_path() / "cpp-pinyin2-broken-dict";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    // copy() does not create missing parents: ENOENT here (measured once) unless root exists.
    std::filesystem::create_directories(root, ec);
    REQUIRE_FALSE(ec);
    std::filesystem::copy(std::filesystem::current_path() / "dict" / "mandarin", root / "mandarin",
                          std::filesystem::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    // Replace the file with a directory: ifstream cannot read it, so the load must fail.
    std::filesystem::remove(root / "mandarin" / "user_dict.txt", ec);
    std::filesystem::create_directory(root / "mandarin" / "user_dict.txt", ec);
    REQUIRE_FALSE(ec);

    // This construction is EXPECTED to fail (user_dict.txt is a directory, so the user dict cannot
    // load), so it goes through `Engine::open` directly: the fixture helper asserts success by taking
    // `value()`, and "the construction failed, and here is why" is the one thing this case is about.
    const cpp_pinyin::v2::Registry registry;
    auto broken = cpp_pinyin::v2::Engine::open(registry, root, "mandarin");
    CHECK_FALSE(broken);
    CHECK(broken.error().detail.find("user dict") != std::string::npos);

    std::filesystem::remove_all(root, ec);
}

// The thread contract is written down in Mandarin.h:25-32 (and Cantonese.h) but had NO test --
// docs/BACKLOG.md lists it as "无锁无测试". This pins the supported half: after
// construction, concurrent READ-ONLY calls (query/readings/toSimplified/isPolyphonic, the exact
// list the contract names) on a SHARED object must agree with single-threaded results.
//
// The unsupported half is deliberately NOT exercised: the contract states that loadUserDict /
// loadTraditionalDict / setDisambiguationCallback overlap a reader and corrupt the heap (measured),
// so a test racing on purpose would be flaky by construction -- it would report the contract, not
// check it. What this test cannot do is prove race-freedom (no TSan on this toolchain); it catches
// the realistic regression of a read path starting to WRITE shared state (a cache, a lazy index).
TEST_CASE("concurrent read-only calls on a shared engine agree with single-threaded results", "[api][thread_safety]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();
    cpp_pinyin::v2::Engine can = cpp_test::cantonese();

    cpp_pinyin::v2::EngineConfig mcfg;
    mcfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    mcfg.includeCandidates = false;
    cpp_pinyin::v2::EngineConfig ccfg;
    ccfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    ccfg.includeCandidates = false;

    const std::vector<std::string> inputs = {
        u8"你好世界", u8"银行行长",     u8"音乐快乐",       u8"重庆重逢",
        u8"国庆长假", u8"人民都有理想", u8"我不知道为什么", u8"曾几何时",
    };

    // Single-threaded baseline for every API the contract lists as read-only.
    std::vector<std::string> q, rd, ts, poly;
    for (const auto &s : inputs) {
        q.push_back(man.query(s, mcfg).value().join());
        rd.push_back(man.readings(s).value().empty() ? "" : "ok");
        ts.push_back(man.toSimplified(s).value_or_throw());
        poly.push_back(man.isPolyphonic(s) ? "1" : "0");
    }
    // Also the Cantonese engine, so the shared-instance claim is checked on both dialects.
    std::vector<std::string> cq;
    for (const auto &s : inputs)
        cq.push_back(can.query(s, ccfg).value().join());

    constexpr int kThreads = 4;
    constexpr int kIters = 150;
    std::atomic<int> mismatches{0};
    std::atomic<int> exceptions{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(
            [&, t]
            {
                for (int i = 0; i < kIters; ++i) {
                    const size_t k = static_cast<size_t>((i + t) % inputs.size());
                    try {
                        if (man.query(inputs[k], mcfg).value().join() != q[k])
                            ++mismatches;
                        if (man.readings(inputs[k]).value().empty() != rd[k].empty())
                            ++mismatches;
                        if (man.toSimplified(inputs[k]).value_or_throw() != ts[k])
                            ++mismatches;
                        if (man.isPolyphonic(inputs[k]) != (poly[k] == "1"))
                            ++mismatches;
                        if (can.query(inputs[k], ccfg).value().join() != cq[k])
                            ++mismatches;
                    }
                    catch (...) {
                        ++exceptions;
                    }
                }
            });
    }
    for (auto &th : threads)
        th.join();

    CHECK(mismatches.load() == 0);
    CHECK(exceptions.load() == 0);
    // The shared object must also still work afterwards (no damage left behind).
    CHECK(man.query(inputs[0], mcfg).value().join() == q[0]);
}
