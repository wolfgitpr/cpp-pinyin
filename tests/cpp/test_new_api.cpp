#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include "dialect_fixtures.h"

namespace
{
    struct NewApiFixture {
        cpp_pinyin::v2::Engine man;
        cpp_pinyin::v2::Engine can;

        NewApiFixture() : man(cpp_test::mandarin()), can(cpp_test::cantonese()) {}
    };

    // The four columns the `includeReading` switch may not touch, compared row by row. `reading`
    // itself is deliberately absent -- it is the one column the switch exists for, and "changing only
    // that one" is exactly what these cases are here to pin.
    bool sameExceptReading(const cpp_pinyin::v2::Result &a, const cpp_pinyin::v2::Result &b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].text != b[i].text || a[i].rendered != b[i].rendered || a[i].candidates != b[i].candidates ||
                a[i].failure_kind != b[i].failure_kind)
                return false;
        }
        return true;
    }
}  // namespace

TEST_CASE_METHOD(NewApiFixture, "EngineConfig-style conversion", "[new_api][mandarin_config]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"中国人民", cfg).value();
    CHECK(res.join() == "zhong1 guo2 ren2 min2");

    // vToU
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.vToU = false;
    CHECK(man.query(u8"虐", cfg).value().join() == "nve4");
    cfg.vToU = true;
    CHECK(man.query(u8"虐", cfg).value().join() == "nüe4");

    // markNeutralAs5
    cfg.vToU = false;
    cfg.markNeutralAs5 = false;
    CHECK(man.query(u8"还是还给他", cfg).value().join() == "hai2 shi huan2 gei3 ta1");
    cfg.markNeutralAs5 = true;
    CHECK(man.query(u8"还是还给他", cfg).value().join() == "hai2 shi5 huan2 gei3 ta1");
}

TEST_CASE_METHOD(NewApiFixture, "EngineConfig vector overload", "[new_api][mandarin_config]") {
    std::vector<std::string> input = {u8"你", u8"好", u8"世", u8"界"};
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(input, cfg).value();
    CHECK(res.join() == "ni3 hao3 shi4 jie4");
}

TEST_CASE_METHOD(NewApiFixture, "EngineConfig readings", "[new_api][mandarin_config]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.readings(u8"行", cfg).value();
    REQUIRE(res.size() == 4);
    CHECK(res[0] == "xing2");
    CHECK(res[1] == "hang2");
}

TEST_CASE_METHOD(NewApiFixture, "EngineConfig conversion", "[new_api][jyutping_config]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    auto res = can.query(u8"你好世界", cfg).value();
    CHECK(res.join() == "nei5 hou2 sai3 gaai3");

    cfg.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;
    auto normalRes = can.query(u8"中华人民共和国", cfg).value();
    CHECK(normalRes.join() == "zung waa jan man gung wo gwok");
}

TEST_CASE_METHOD(NewApiFixture, "EngineConfig vector overload", "[new_api][jyutping_config]") {
    std::vector<std::string> input = {u8"你", u8"好"};
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    auto res = can.query(input, cfg).value();
    CHECK(res.join() == "nei5 hou2");
}

TEST_CASE_METHOD(NewApiFixture, "Result member functions", "[new_api][result]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"你好世界", cfg).value();

    auto vec = res.toVector();
    REQUIRE(vec.size() == 4);
    CHECK(vec[0] == "ni3");

    CHECK(res.join() == "ni3 hao3 shi4 jie4");
    CHECK(res.join("-") == "ni3-hao3-shi4-jie4");
    CHECK(res.join("") == "ni3hao3shi4jie4");
}

TEST_CASE_METHOD(NewApiFixture, "EngineConfig candidates=false", "[new_api][mandarin_config]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.includeCandidates = false;
    auto res = man.query(u8"行", cfg).value();
    REQUIRE(res.size() == 1);
    CHECK(res[0].candidates.empty());
}

TEST_CASE_METHOD(NewApiFixture, "iterate over syllables", "[new_api][iterate]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res = man.query(u8"行", cfg).value();
    REQUIRE(res.size() == 1);
    CHECK(res[0].text == u8"行");
    CHECK_FALSE(res[0].candidates.empty());
}

TEST_CASE_METHOD(NewApiFixture, "ErrorHandling::Drop with candidates", "[new_api][ignore]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.onError = cpp_pinyin::v2::ErrorHandling::Drop;
    cfg.includeCandidates = true;
    auto res = man.query(u8"hello世界", cfg).value();
    CHECK(res.join() == "shi4 jie4");
}

// `includeReading` (S8): the structured reading is opt-in, and opting in changes only itself.

TEST_CASE_METHOD(NewApiFixture, "includeReading defaults to false in all three configs", "[new_api][reading]") {
    // The same rule `includeCandidates` follows, for the same reason: the extra data is not an input
    // to rendering, so the default is the one that does not charge every row for it. A default of
    // `true` here is what put the query path outside its performance gate in S7.
    CHECK_FALSE(cpp_pinyin::v2::EngineConfig{}.includeReading);
    CHECK_FALSE(cpp_pinyin::v2::EngineConfig{}.includeReading);
    CHECK_FALSE(cpp_pinyin::v2::EngineConfig{}.includeReading);
}

TEST_CASE_METHOD(NewApiFixture, "includeReading=false leaves every reading default-constructed", "[new_api][reading]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    cfg.includeReading = false;
    // A character row, a phrase-transposed row, a failed row and an ASCII token: every way a row is
    // decided is in here.
    const auto res = man.query(u8"你好，hello 银行 123", cfg).value();
    REQUIRE(res.size() >= 7);

    for (const auto &row : res) {
        INFO("row text = " << row.text);
        CHECK(row.reading.initial.empty());
        CHECK(row.reading.fin.empty());
        CHECK(row.reading.tone == 0);
        CHECK(row.reading.tone_kind == cpp_pinyin::v2::ToneKind::unspecified);
        CHECK(row.reading.source == cpp_pinyin::v2::ReadingSource::dictionary);
    }

    // Nothing a caller can read went missing: the reading is in `rendered`, and "no reading" is still
    // `failure_kind`, never "is `reading` empty".
    CHECK(res[0].text == u8"你");
    CHECK(res[0].rendered == "ni3");
    CHECK(res[0].failure_kind == cpp_pinyin::v2::Code::Ok);
}

TEST_CASE_METHOD(NewApiFixture, "includeReading=true fills reading and moves no other column", "[new_api][reading]") {
    cpp_pinyin::v2::EngineConfig off;
    cpp_pinyin::v2::EngineConfig on;
    off.style = on.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    off.includeReading = false;
    on.includeReading = true;

    const std::string input = u8"你好，hello 银行 123 一个不去的中国人";
    const auto without = man.query(input, off).value();
    const auto with = man.query(input, on).value();

    REQUIRE_FALSE(without.empty());
    REQUIRE(with.size() == without.size());
    // The whole point of the switch: one column differs, nothing else does. A row-by-row comparison
    // rather than a spot check, because "the structure is data, not a rendering input" is a claim
    // about every row.
    CHECK(sameExceptReading(without, with));

    // 你 -> nǐ: the plain word-table path. `tone` is read from the table's own notation, `fin` is the
    // canonical final ("i", not "ni"), and the source says which table it came from.
    REQUIRE(with[0].text == u8"你");
    CHECK(with[0].reading.initial == "n");
    CHECK(with[0].reading.fin == "i");
    CHECK(with[0].reading.tone == 3);
    CHECK(with[0].reading.tone_kind == cpp_pinyin::v2::ToneKind::base);
    CHECK(with[0].reading.source == cpp_pinyin::v2::ReadingSource::dictionary);

    // 行 in 银行 comes from the PHRASE layer, and its reading is not candidates[0] (xing2) -- the row
    // whose structure is worth having at all. Found by text so the row index is not what is being
    // tested here.
    const cpp_pinyin::v2::Segment *hang = nullptr;
    for (const auto &row : with) {
        if (row.text == u8"行")
            hang = &row;
    }
    REQUIRE(hang != nullptr);
    CHECK(hang->rendered == "hang2");
    CHECK(hang->reading.initial == "h");
    CHECK(hang->reading.fin == "ang");
    CHECK(hang->reading.tone == 2);
    CHECK(hang->reading.source == cpp_pinyin::v2::ReadingSource::phrase);

    // A failed row keeps the default structure under BOTH settings: it has no reading to describe,
    // which is what `failure_kind` says instead.
    for (const auto &row : with) {
        if (row.failure_kind != cpp_pinyin::v2::Code::Ok) {
            CHECK(row.reading.initial.empty());
            CHECK(row.reading.fin.empty());
            CHECK(row.reading.tone == 0);
        }
    }
}

TEST_CASE_METHOD(NewApiFixture, "includeReading=true works through the generic Engine face too",
                 "[new_api][reading][engine]") {
    // The `Engine` face carries the switch through `EngineConfig`, whose seven fields are mostly
    // bools -- a swap between any two of them compiles. This case is what makes that wiring visible.
    const cpp_pinyin::v2::Registry registry;
    cpp_pinyin::v2::Engine engine =
        cpp_pinyin::v2::Engine::open(registry, std::filesystem::current_path() / "dict", "mandarin").value();
    // The engine is opened with .value(), which throws if it fails -- an "is it ready"
    // assertion here would be vacuous.

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = static_cast<int>(cpp_pinyin::v2::MandarinStyle::TONE3);
    cfg.includeReading = true;
    const auto res = engine.query(u8"你好", cfg).value();
    REQUIRE(res.size() == 2);
    CHECK(res[0].rendered == "ni3");
    CHECK(res[0].reading.initial == "n");
    CHECK(res[0].reading.fin == "i");
    CHECK(res[0].reading.tone == 3);

    // ... and the same engine with the switch off, so the difference is the switch and not the face.
    cfg.includeReading = false;
    const auto plain = engine.query(u8"你好", cfg).value();
    REQUIRE(plain.size() == 2);
    CHECK(sameExceptReading(plain, res));
    CHECK(plain[0].reading.fin.empty());
}

TEST_CASE_METHOD(NewApiFixture, "Cantonese fills its own tone categories when asked", "[new_api][reading]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    cfg.includeReading = true;
    const auto res = can.query(u8"你好", cfg).value();
    REQUIRE(res.size() == 2);
    CHECK(res[0].rendered == "nei5");
    // 1..6 are the Cantonese categories, and the entering tones are 1/3/6 with a -p/-t/-k coda --
    // never 7/8/9 (the contract's fourth sentence, tests/cpp/test_syllable_contract.cpp).
    CHECK(res[0].reading.tone == 5);
    CHECK(res[0].reading.initial == "n");
    // 好 is hou2: the category comes from the Cantonese table, not from any digit parsed out of a
    // rendered string, and the range check below is the contract's "1..6, never 7/8/9" rule.
    CHECK(res[1].rendered == "hou2");
    CHECK(res[1].reading.tone == 2);
    CHECK(res[1].reading.tone >= 1);
    CHECK(res[1].reading.tone <= 6);
}
