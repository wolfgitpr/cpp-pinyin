// Tests for Mandarin tone sandhi (`applyMandarinToneSandhi`).
//
// The expectations here come from the RULES themselves (《现代汉语》's third-tone runs and the 不/一
// changes), not from pypinyin's output: pypinyin's contrib.tone_sandhi only applies a third-tone run when
// it ends exactly at that segment, and it does not implement the "一 + numeral/一 -> first tone"
// exception -- so it disagrees with the rules on 老鼠/一天/我很好 and the like (measured:
// lazy_pinyin('老鼠', TONE3, tone_sandhi=True) == ['lao3','shu3']). The differences are stated at the top
// of include/cpp-pinyin2/core/ToneSandhi.h.
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cpp-pinyin2/core/ToneSandhi.h>
#include "dialect_fixtures.h"

namespace
{

    std::string readingsOf(const cpp_pinyin::v2::Result &result) {
        std::string out;
        for (size_t i = 0; i < result.size(); ++i)
            out += (i ? " " : "") + result[i].rendered;
        return out;
    }

    cpp_pinyin::v2::Result queryTone3(const cpp_pinyin::v2::Engine &man, const char *text) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        cfg.markNeutralAs5 = true;
        return man.query(text, cfg).value();
    }

}  // namespace

TEST_CASE("Tone sandhi: 三声连读", "[sandhi][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    struct Case {
        const char *text;
        const char *raw;
        const char *sandhi;
        size_t changed;
    };
    const Case cases[] = {
        {u8"你好", "ni3 hao3", "ni2 hao3", 1},
        {u8"老鼠", "lao3 shu3", "lao2 shu3", 1},
        // Three third tones in a row: every one but the last becomes second (pypinyin does not apply here;
        // this follows the rule).
        {u8"我很好", "wo3 hen3 hao3", "wo2 hen2 hao3", 2},
        {u8"保险", "bao3 xian3", "bao2 xian3", 1},
        // A non-third tone breaks the run.
        {u8"小心", "xiao3 xin1", "xiao3 xin1", 0},
    };

    for (const auto &c : cases) {
        auto result = queryTone3(man, c.text);
        INFO("text=" << c.text);
        CHECK(readingsOf(result) == c.raw);
        const size_t changed = cpp_pinyin::v2::applyMandarinToneSandhi(result);
        CHECK(changed == c.changed);
        CHECK(readingsOf(result) == c.sandhi);
    }
}

TEST_CASE("Tone sandhi: 不 / 一", "[sandhi][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    struct Case {
        const char *text;
        const char *raw;
        const char *sandhi;
        size_t changed;
    };
    const Case cases[] = {
        // 不 + fourth tone -> bú
        {u8"不是", "bu4 shi4", "bu2 shi4", 1},
        // 不 + anything else -> bù (already fourth tone, so no change)
        {u8"不好", "bu4 hao3", "bu4 hao3", 0},
        // 一 + fourth tone -> yí
        {u8"一定", "yi1 ding4", "yi2 ding4", 1},
        {u8"一样", "yi1 yang4", "yi2 yang4", 1},
        // 一 + first/second/third tone -> yì
        {u8"一天", "yi1 tian1", "yi4 tian1", 1},
        // A trailing 一 reads first tone (万一 is wàn yī)
        {u8"万一", "wan4 yi1", "wan4 yi1", 0},
        // Before a numeral or another 一 it reads first tone (一一 is yī yī; pypinyin does not implement this
        // exception)
        {u8"一一", "yi1 yi1", "yi1 yi1", 0},
        // A following neutral tone (here the neutral reading of 个) does not count as a fourth tone -> yì
        {u8"一个样", "yi1 ge5 yang4", "yi4 ge5 yang4", 1},
    };

    for (const auto &c : cases) {
        auto result = queryTone3(man, c.text);
        INFO("text=" << c.text);
        CHECK(readingsOf(result) == c.raw);
        const size_t changed = cpp_pinyin::v2::applyMandarinToneSandhi(result);
        CHECK(changed == c.changed);
        CHECK(readingsOf(result) == c.sandhi);
    }
}

TEST_CASE("Tone sandhi: 失败行是边界", "[sandhi][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    // Punctuation and non-Hanzi break the run: a sentence-final 一 reads first tone and 不 reads fourth, and
    // neither carries across punctuation.
    auto result = queryTone3(man, u8"一，你好");
    REQUIRE(result.size() == 4);
    CHECK((result[1].failure_kind != cpp_pinyin::v2::Code::Ok));
    const size_t changed = cpp_pinyin::v2::applyMandarinToneSandhi(result);
    CHECK(readingsOf(result) == "yi1 ， ni2 hao3");
    CHECK(changed == 1);

    auto lone = queryTone3(man, u8"一");
    CHECK(cpp_pinyin::v2::applyMandarinToneSandhi(lone) == 0);
    CHECK(readingsOf(lone) == "yi1");

    auto loneBu = queryTone3(man, u8"不");
    CHECK(cpp_pinyin::v2::applyMandarinToneSandhi(loneBu) == 0);
    CHECK(readingsOf(loneBu) == "bu4");
}

TEST_CASE("Tone sandhi: 只改 reading，不动其它字段；非 TONE3 风格原样保留", "[sandhi][mandarin][contract]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();
    // The engine is opened with .value(), which throws if it fails -- an "is it ready" assertion here would
    // be vacuous.

    auto result = queryTone3(man, u8"你好，abc");
    const auto before = result;
    const size_t changed = cpp_pinyin::v2::applyMandarinToneSandhi(result);
    CHECK(changed == 1);
    REQUIRE(result.size() == before.size());
    for (size_t i = 0; i < result.size(); ++i) {
        CHECK(result[i].text == before[i].text);
        CHECK((result[i].failure_kind != cpp_pinyin::v2::Code::Ok) ==
              (before[i].failure_kind != cpp_pinyin::v2::Code::Ok));
        CHECK(result[i].candidates == before[i].candidates);
    }
    CHECK(readingsOf(result) == "ni2 hao3 ， abc");

    // Turning every rule off means no change at all.
    auto untouched = queryTone3(man, u8"你好");
    cpp_pinyin::v2::ToneSandhiOptions off;
    off.thirdTone = off.bu = off.yi = false;
    CHECK(cpp_pinyin::v2::applyMandarinToneSandhi(untouched, off) == 0);
    CHECK(readingsOf(untouched) == "ni3 hao3");

    // Styles that carry no readable tone (tone marks / plain / bopomofo) cannot be sandhi'd reliably, so the
    // value comes back unchanged.
    for (const auto style : {cpp_pinyin::v2::MandarinStyle::NORMAL, cpp_pinyin::v2::MandarinStyle::TONE,
                             cpp_pinyin::v2::MandarinStyle::TONE2, cpp_pinyin::v2::MandarinStyle::BOPOMOFO}) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = style;
        auto styled = man.query(u8"你好", cfg).value();
        const auto raw = readingsOf(styled);
        INFO("style=" << static_cast<int>(style));
        CHECK(cpp_pinyin::v2::applyMandarinToneSandhi(styled) == 0);
        CHECK(readingsOf(styled) == raw);
    }

    // FINALS_TONE3 has the same "trailing digit" shape, so it can be sandhi'd too.
    cpp_pinyin::v2::EngineConfig finals;
    finals.style = cpp_pinyin::v2::MandarinStyle::FINALS_TONE3;
    auto asFinals = man.query(u8"你好", finals).value();
    CHECK(cpp_pinyin::v2::applyMandarinToneSandhi(asFinals) == 1);
    CHECK(readingsOf(asFinals) == "i2 ao3");
}
