#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "dialect_fixtures.h"

namespace
{
    struct JyutpingFixture {
        cpp_pinyin::v2::Engine can;

        JyutpingFixture() : can(cpp_test::cantonese()) {
            // The engine is opened with .value(), which throws if it fails -- an "is it ready"
            // assertion here would be vacuous.
        }
    };
}  // namespace

static std::string q(cpp_pinyin::v2::Engine &can, const std::string &text,
                     cpp_pinyin::v2::CantoneseStyle style = cpp_pinyin::v2::CantoneseStyle::TONE3) {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = style;
    return can.query(text, cfg).value().join();
}

TEST_CASE_METHOD(JyutpingFixture, "Cantonese TONE3 - basic sentences", "[cantonese][tone3][golden]") {
    CHECK(q(can, u8"你好世界", cpp_pinyin::v2::CantoneseStyle::TONE3) == u8"nei5 hou2 sai3 gaai3");
    CHECK(q(can, u8"中华人民共和国", cpp_pinyin::v2::CantoneseStyle::TONE3) ==
          u8"zung1 waa4 jan4 man4 gung6 wo4 gwok3");
    CHECK(q(can, u8"今天天气很好", cpp_pinyin::v2::CantoneseStyle::TONE3) == u8"gam1 tin1 tin1 hei3 han2 hou2");
}

TEST_CASE_METHOD(JyutpingFixture, "Cantonese NORMAL - basic sentences", "[cantonese][normal][golden]") {
    CHECK(q(can, u8"你好世界", cpp_pinyin::v2::CantoneseStyle::NORMAL) == u8"nei hou sai gaai");
    CHECK(q(can, u8"中华人民共和国", cpp_pinyin::v2::CantoneseStyle::NORMAL) == u8"zung waa jan man gung wo gwok");
    CHECK(q(can, u8"今天天气很好", cpp_pinyin::v2::CantoneseStyle::NORMAL) == u8"gam tin tin hei han hou");
}

TEST_CASE_METHOD(JyutpingFixture, "Cantonese hanziToPinyin vector overload", "[cantonese][vector_input][golden]") {
    std::vector<std::string> input = {u8"你", u8"好"};
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    auto res = can.query(input, cfg).value();
    CHECK(res.join() == u8"nei5 hou2");
}

TEST_CASE_METHOD(JyutpingFixture, "Cantonese getDefaultPinyin", "[cantonese][default_pinyin][golden]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    auto res = can.readings(u8"好", cfg).value();
    REQUIRE(!res.empty());
    auto res2 = can.readings(u8"好", cfg).value();
    CHECK(res == res2);

    cfg.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;
    auto resN = can.readings(u8"好", cfg).value();
    REQUIRE(!resN.empty());
}
