#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dialect_fixtures.h"

namespace
{

    struct Fixture {
        cpp_pinyin::v2::Engine man;
        cpp_pinyin::v2::Engine can;

        Fixture() : man(cpp_test::mandarin()), can(cpp_test::cantonese()) {
            // The engine is opened with .value(), which throws if it fails -- an "is it ready"
            // assertion here would be vacuous.
        }
    };

    static std::string qm(cpp_pinyin::v2::Engine &m, const std::string &text, cpp_pinyin::v2::MandarinStyle s) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = s;
        return m.query(text, cfg).value().join();
    }

    static std::string qc(cpp_pinyin::v2::Engine &c, const std::string &text, cpp_pinyin::v2::CantoneseStyle s) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = s;
        return c.query(text, cfg).value().join();
    }

}  // namespace

TEST_CASE_METHOD(Fixture, "Mandarin FIRST_LETTER style", "[style][first_letter]") {
    CHECK(qm(man, u8"中华人民共和国", cpp_pinyin::v2::MandarinStyle::FIRST_LETTER) == u8"z h r m g h g");
    CHECK(qm(man, u8"明月几时有", cpp_pinyin::v2::MandarinStyle::FIRST_LETTER) == u8"m y j s y");
    CHECK(qm(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::FIRST_LETTER) == u8"n h s j");
}

TEST_CASE_METHOD(Fixture, "Cantonese FIRST_LETTER style", "[style][first_letter][cantonese]") {
    CHECK(qc(can, u8"中华人民共和国", cpp_pinyin::v2::CantoneseStyle::FIRST_LETTER) == u8"z w j m g w g");
    CHECK(qc(can, u8"你好世界", cpp_pinyin::v2::CantoneseStyle::FIRST_LETTER) == u8"n h s g");
}

TEST_CASE_METHOD(Fixture, "Mandarin BOPOMOFO style", "[style][bopomofo]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::BOPOMOFO;
    auto res = man.query(u8"中文", cfg).value();
    CHECK_FALSE(res.empty());
}

TEST_CASE_METHOD(Fixture, "Error::Ignore with candidates", "[error][ignore][candidates]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.onError = cpp_pinyin::v2::ErrorHandling::Drop;
    cfg.includeCandidates = true;
    CHECK(man.query(u8"hello世界", cfg).value().join() == u8"shi4 jie4");
}

TEST_CASE_METHOD(Fixture, "loadCustomUserDict", "[dict][custom]") {
    auto tmpDir = std::filesystem::temp_directory_path();
    auto dictFile = tmpDir / "test_user_dict.txt";

    {
        std::ofstream ofs(dictFile);
        REQUIRE(ofs.is_open());
        ofs << u8"自定义  zi4 ding4 yi4" << std::endl;
        ofs << u8"测试用例  ce4 shi4 yong4 li4" << std::endl;
    }

    const auto loaded = man.loadUserDict(dictFile);
    CHECK(loaded.has_value());
    std::filesystem::remove(dictFile);
}

TEST_CASE_METHOD(Fixture, "loadCustomUserDict with nonexistent file", "[dict][custom][error]") {
    const auto loaded = man.loadUserDict("nonexistent_file.txt");
    CHECK_FALSE(loaded.has_value());
    // The new return type is only worth its cost if the CODE is asserted, not just the failure.
    CHECK(loaded.error().code == cpp_pinyin::v2::Code::Load);
}

TEST_CASE_METHOD(Fixture, "loadCustomFanJianDictionary", "[dict][fanjian]") {
    auto tmpDir = std::filesystem::temp_directory_path();
    auto dictFile = tmpDir / "test_fanjian_dict.txt";

    {
        std::ofstream ofs(dictFile);
        REQUIRE(ofs.is_open());
        // Separator must be ':' like data/dialects/*/trans_word.txt ("魚:鱼"). This used to be a space,
        // which parses to ZERO entries: the loader reported success anyway and the
        // CHECK below therefore asserted nothing at all.
        ofs << u8"臺:台" << std::endl;
        ofs << u8"國:国" << std::endl;
    }

    const auto loaded = man.loadTraditionalDict(dictFile);
    CHECK(loaded.has_value());
    // Prove the file had an effect instead of only proving that the call returned.
    CHECK(man.toSimplified(u8"臺").value_or_throw() == u8"台");
    std::filesystem::remove(dictFile);
}

TEST_CASE_METHOD(Fixture, "loadCustomFanJianDictionary with nonexistent file", "[dict][fanjian][error]") {
    const auto loaded = man.loadTraditionalDict("nonexistent_file.txt");
    CHECK_FALSE(loaded.has_value());
    CHECK(loaded.error().code == cpp_pinyin::v2::Code::Load);
}

TEST_CASE_METHOD(Fixture, "v_to_u extended", "[option][v_to_u]") {
    auto checkVtoU = [&](const std::string &hanzi, const std::string &expectedV, const std::string &expectedU)
    {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        cfg.vToU = false;
        CHECK(man.query(hanzi, cfg).value().join() == expectedV);
        cfg.vToU = true;
        CHECK(man.query(hanzi, cfg).value().join() == expectedU);
    };

    checkVtoU(u8"女", "nv3", "nv3");
    checkVtoU(u8"绿", "lv4", "lv4");
    checkVtoU(u8"略", "lve4", "lüe4");
}

TEST_CASE_METHOD(Fixture, "neutral_tone_with_five extended", "[option][n5]") {
    auto with5 = [&](const std::string &text)
    {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        cfg.markNeutralAs5 = true;
        return man.query(text, cfg).value().join();
    };
    CHECK(with5(u8"来了") == u8"lai2 le5");
    CHECK(with5(u8"好的") == u8"hao3 de5");
}

TEST_CASE_METHOD(Fixture, "Large input stress test", "[stress][large]") {
    std::string largeInput;
    largeInput.reserve(2000);
    for (int i = 0; i < 200; ++i) {
        largeInput += u8"中华人民共和国";
    }
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(largeInput, cfg).value();
    CHECK(res.size() == largeInput.size() / 3);
    // The tail is what a length regression breaks first: a truncated final row, or an off-by-one that drops the
    // last character silently. Both show up here and neither shows up in the count alone.
    REQUIRE_FALSE(res.empty());
    CHECK(res[res.size() - 1].text == u8"国");
    CHECK_FALSE(res[res.size() - 1].rendered.empty());

    // No cap is imposed (see c_api.h's "NO LENGTH CAP, BY DECISION"): 4x the size above must still answer with
    // one row per character rather than refusing or truncating.
    std::string bigger;
    bigger.reserve(8000);
    for (int i = 0; i < 800; ++i)
        bigger += u8"中华人民共和国";
    const auto bigRes = man.query(bigger, cfg).value();
    CHECK(bigRes.size() == bigger.size() / 3);
    CHECK(bigRes[bigRes.size() - 1].text == u8"国");
}

TEST_CASE_METHOD(Fixture, "Non-hanzi only input", "[edge][nonhanzi]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    CHECK(man.query(u8"abc123!@#", cfg).value().join() == u8"abc 1 2 3 ! @ #");

    cfg.onError = cpp_pinyin::v2::ErrorHandling::Drop;
    cfg.includeCandidates = false;
    CHECK(man.query(u8"abc123!@#", cfg).value().join().empty());
}

TEST_CASE_METHOD(Fixture, "Whitespace and empty input", "[edge][whitespace]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    CHECK(man.query(u8"", cfg).value().empty());
    CHECK(man.query(u8"   ", cfg).value().empty());
}

TEST_CASE_METHOD(Fixture, "EngineConfig all fields", "[new_api][mandarin_config][full]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.onError = cpp_pinyin::v2::ErrorHandling::Default;
    cfg.includeCandidates = false;
    cfg.vToU = false;
    cfg.markNeutralAs5 = false;
    CHECK(man.query(u8"中国", cfg).value().join() == "zhong1 guo2");

    cfg.markNeutralAs5 = true;
    CHECK(man.query(u8"好的", cfg).value().join() == "hao3 de5");
}

TEST_CASE_METHOD(Fixture, "EngineConfig candidates with polyphonic", "[new_api][candidates]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.includeCandidates = true;
    auto res = man.query(u8"行", cfg).value();
    REQUIRE(res.size() == 1);
    CHECK_FALSE(res[0].candidates.empty());
}

TEST_CASE_METHOD(Fixture, "Thread safety smoke test", "[thread][concurrent]") {
    std::atomic<size_t> errors{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(
            [&]()
            {
                cpp_pinyin::v2::EngineConfig mcfg;
                mcfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
                cpp_pinyin::v2::EngineConfig ccfg;
                ccfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;

                for (int i = 0; i < 100; ++i) {
                    if (man.query(u8"银行行长", mcfg).value().size() != 4)
                        errors++;
                    if (can.query(u8"中國銀行", ccfg).value().size() != 4)
                        errors++;
                }
            });
    }

    for (auto &th : threads)
        th.join();
    CHECK(errors == 0);
}

TEST_CASE_METHOD(Fixture, "Vector overload with EngineConfig all styles", "[new_api][vector]") {
    std::vector<std::string> input = {u8"你", u8"好", u8"世", u8"界"};

    for (auto style : {cpp_pinyin::v2::MandarinStyle::NORMAL, cpp_pinyin::v2::MandarinStyle::TONE,
                       cpp_pinyin::v2::MandarinStyle::TONE3}) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = style;
        auto res = man.query(input, cfg).value();
        CHECK(res.size() == 4);
    }
}

TEST_CASE_METHOD(Fixture, "EngineConfig all fields", "[new_api][jyutping_config][full]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    cfg.onError = cpp_pinyin::v2::ErrorHandling::Default;
    cfg.includeCandidates = false;
    CHECK(can.query(u8"你好世界", cfg).value().join() == "nei5 hou2 sai3 gaai3");

    cfg.includeCandidates = true;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;
    CHECK(can.query(u8"你好世界", cfg).value().join() == "nei hou sai gaai");
}

// The name here used to be "Cross-language config is rejected at type level", which is the OPPOSITE of
// the design: include/cpp-pinyin2/core/Types.h:105-108 says `Style` converts from BOTH enums implicitly and on purpose,
// so that one merged config can be dialect-agnostic. Nothing is rejected, and no assertion here ever tested a rejection
// -- it assigned a Mandarin style to a Mandarin engine and checked the query worked, which the case below still does.
// What the old name gestured at is a real hazard worth stating, so this case now records what actually happens when the
// spellings are crossed: the NUMBER travels, and the dialect's own style table decides what it means.
TEST_CASE_METHOD(Fixture, "EngineConfig takes either dialect's style spelling; the dialect decides the meaning",
                 "[type][safety]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    CHECK_FALSE(man.query(u8"中国", cfg).value().empty());
    const std::string asMandarin = man.query(u8"中国", cfg).value().join();

    // A Cantonese spelling on a Mandarin engine compiles (by design) and reaches the same engine: the
    // NUMBER is what travels, and `resolveStyle` hands it to the dialect. Measured 2026-10-27:
    // CantoneseStyle::TONE3 == MandarinStyle::TONE3 == 8, so the crossed spelling is accepted and answers
    // exactly what the right-dialect spelling answers. That is the hazard the old name was gesturing at,
    // and it is a hazard only in the other direction -- a number with no meaning in this dialect.
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    const auto crossed = man.query(u8"中国", cfg);
    REQUIRE(crossed.has_value());
    CHECK(crossed.value().join() == asMandarin);
}

TEST_CASE_METHOD(Fixture, "NODISCARD attribute compiles", "[attribute][nodiscard]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res1 = man.query(u8"中国", cfg).value();
    CHECK_FALSE(res1.empty());

    auto res2 = man.readings(u8"行", cfg).value();
    CHECK_FALSE(res2.empty());
}

TEST_CASE_METHOD(Fixture, "Cantonese FIRST_LETTER through config", "[style][first_letter][cantonese]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::FIRST_LETTER;
    CHECK(can.query(u8"你好世界", cfg).value().join() == u8"n h s g");
}
