#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "dialect_fixtures.h"

namespace
{
    struct PinyinFixture {
        cpp_pinyin::v2::Engine man;

        PinyinFixture() : man(cpp_test::mandarin()) {
            // The engine is opened with .value(), which throws if it fails -- an "is it ready"
            // assertion here would be vacuous.
        }
    };
}  // namespace

static std::string q(cpp_pinyin::v2::Engine &man, const std::string &text,
                     cpp_pinyin::v2::MandarinStyle style = cpp_pinyin::v2::MandarinStyle::TONE, bool n5 = false,
                     bool v2u = false) {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = style;
    cfg.markNeutralAs5 = n5;
    cfg.vToU = v2u;
    return man.query(text, cfg).value().join();
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin NORMAL - basic sentences", "[mandarin][normal][golden]") {
    CHECK(q(man, u8"明月几时有", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"ming yue ji shi you");
    CHECK(q(man, u8"中华人民共和国", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"zhong hua ren min gong he guo");
    CHECK(q(man, u8"今天天气很好", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"jin tian tian qi hen hao");
    CHECK(q(man, u8"我是中国人", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"wo shi zhong guo ren");
    CHECK(q(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"ni hao shi jie");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin NORMAL - polyphonic words", "[mandarin][normal][polyphonic][golden]") {
    CHECK(q(man, u8"银行行长", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"yin hang hang zhang");
    CHECK(q(man, u8"音乐快乐", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"yin yue kuai le");
    CHECK(q(man, u8"重量很重", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"zhong liang hen zhong");
    CHECK(q(man, u8"长城很长", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"chang cheng hen chang");
    CHECK(q(man, u8"还是还给他", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"hai shi huan gei ta");
    CHECK(q(man, u8"大夫看大夫", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"dai fu kan dai fu");
    CHECK(q(man, u8"干净的干果", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"gan jing de gan guo");
    CHECK(q(man, u8"哪吒", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"ne zha");
    CHECK(q(man, u8"陟罚臧否", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"zhi fa zang pi");
    CHECK(q(man, u8"到了", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"dao le");
    CHECK(q(man, u8"汤汤", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"shang shang");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin NORMAL - mixed input", "[mandarin][normal][mixed][golden]") {
    CHECK(q(man, u8"hello世界", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"hello shi jie");
    CHECK(q(man, u8"好 xiu 的", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"hao xiu de");
    CHECK(q(man, u8"hao de", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"hao de");
    CHECK(q(man, u8"明月@1几32时有##一", cpp_pinyin::v2::MandarinStyle::NORMAL) ==
          u8"ming yue @ 1 ji 3 2 shi you # # yi");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin NORMAL - kana input", "[mandarin][normal][kana][golden]") {
    CHECK(q(man, u8"東京タワー", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"dong jing タ ワ ー");
    CHECK(q(man, u8"あいう", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"あ い う");
    CHECK(q(man, u8"举杯あャ坐ュ饮", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"ju bei あャ zuo ュ yin");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin NORMAL - edge cases", "[mandarin][normal][edge][golden]") {
    CHECK(q(man, u8"一", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"yi");
    CHECK(q(man, u8"a", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"a");
    CHECK(q(man, u8"123", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"1 2 3");
    CHECK(q(man, u8"@#$", cpp_pinyin::v2::MandarinStyle::NORMAL) == u8"@ # $");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE - basic sentences", "[mandarin][tone][golden]") {
    CHECK(q(man, u8"明月几时有", cpp_pinyin::v2::MandarinStyle::TONE) == u8"míng yuè jǐ shí yǒu");
    CHECK(q(man, u8"中华人民共和国", cpp_pinyin::v2::MandarinStyle::TONE) == u8"zhōng huá rén mín gòng hé guó");
    CHECK(q(man, u8"今天天气很好", cpp_pinyin::v2::MandarinStyle::TONE) == u8"jīn tiān tiān qì hěn hǎo");
    CHECK(q(man, u8"我是中国人", cpp_pinyin::v2::MandarinStyle::TONE) == u8"wǒ shì zhōng guó rén");
    CHECK(q(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::TONE) == u8"nǐ hǎo shì jiè");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE - polyphonic words", "[mandarin][tone][polyphonic][golden]") {
    CHECK(q(man, u8"银行行长", cpp_pinyin::v2::MandarinStyle::TONE) == u8"yín háng háng zhǎng");
    CHECK(q(man, u8"音乐快乐", cpp_pinyin::v2::MandarinStyle::TONE) == u8"yīn yuè kuài lè");
    CHECK(q(man, u8"重量很重", cpp_pinyin::v2::MandarinStyle::TONE) == u8"zhòng liàng hěn zhòng");
    CHECK(q(man, u8"长城很长", cpp_pinyin::v2::MandarinStyle::TONE) == u8"cháng chéng hěn cháng");
    CHECK(q(man, u8"还是还给他", cpp_pinyin::v2::MandarinStyle::TONE) == u8"hái shi huán gěi tā");
    CHECK(q(man, u8"大夫看大夫", cpp_pinyin::v2::MandarinStyle::TONE) == u8"dài fu kàn dài fu");
    CHECK(q(man, u8"干净的干果", cpp_pinyin::v2::MandarinStyle::TONE) == u8"gān jìng de gān guǒ");
    CHECK(q(man, u8"哪吒", cpp_pinyin::v2::MandarinStyle::TONE) == u8"né zhā");
    CHECK(q(man, u8"陟罚臧否", cpp_pinyin::v2::MandarinStyle::TONE) == u8"zhì fá záng pǐ");
    CHECK(q(man, u8"到了", cpp_pinyin::v2::MandarinStyle::TONE) == u8"dào lē");
    CHECK(q(man, u8"汤汤", cpp_pinyin::v2::MandarinStyle::TONE) == u8"shāng shāng");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE - mixed input", "[mandarin][tone][mixed][golden]") {
    CHECK(q(man, u8"hello世界", cpp_pinyin::v2::MandarinStyle::TONE) == u8"hello shì jiè");
    CHECK(q(man, u8"好 xiu 的", cpp_pinyin::v2::MandarinStyle::TONE) == u8"hǎo xiu de");
    CHECK(q(man, u8"hao de", cpp_pinyin::v2::MandarinStyle::TONE) == u8"hao de");
    CHECK(q(man, u8"明月@1几32时有##一", cpp_pinyin::v2::MandarinStyle::TONE) ==
          u8"míng yuè @ 1 jǐ 3 2 shí yǒu # # yī");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE - kana input", "[mandarin][tone][kana][golden]") {
    CHECK(q(man, u8"東京タワー", cpp_pinyin::v2::MandarinStyle::TONE) == u8"dōng jīng タ ワ ー");
    CHECK(q(man, u8"あいう", cpp_pinyin::v2::MandarinStyle::TONE) == u8"あ い う");
    CHECK(q(man, u8"举杯あャ坐ュ饮", cpp_pinyin::v2::MandarinStyle::TONE) == u8"jǔ bēi あャ zuò ュ yǐn");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE - edge cases", "[mandarin][tone][edge][golden]") {
    CHECK(q(man, u8"一", cpp_pinyin::v2::MandarinStyle::TONE) == u8"yī");
    CHECK(q(man, u8"a", cpp_pinyin::v2::MandarinStyle::TONE) == u8"a");
    CHECK(q(man, u8"123", cpp_pinyin::v2::MandarinStyle::TONE) == u8"1 2 3");
    CHECK(q(man, u8"@#$", cpp_pinyin::v2::MandarinStyle::TONE) == u8"@ # $");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE2 - basic sentences", "[mandarin][tone2][golden]") {
    CHECK(q(man, u8"明月几时有", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"mi2ng yue4 ji3 shi2 yo3u");
    CHECK(q(man, u8"中华人民共和国", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"zho1ng hua2 re2n mi2n go4ng he2 guo2");
    CHECK(q(man, u8"今天天气很好", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"ji1n tia1n tia1n qi4 he3n ha3o");
    CHECK(q(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"ni3 ha3o shi4 jie4");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE2 - polyphonic words", "[mandarin][tone2][polyphonic][golden]") {
    CHECK(q(man, u8"银行行长", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"yi2n ha2ng ha2ng zha3ng");
    CHECK(q(man, u8"音乐快乐", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"yi1n yue4 kua4i le4");
    CHECK(q(man, u8"重量很重", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"zho4ng lia4ng he3n zho4ng");
    CHECK(q(man, u8"哪吒", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"ne2 zha1");
    CHECK(q(man, u8"陟罚臧否", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"zhi4 fa2 za2ng pi3");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE2 - mixed and edge", "[mandarin][tone2][mixed][golden]") {
    CHECK(q(man, u8"hello世界", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"hello shi4 jie4");
    CHECK(q(man, u8"一", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"yi1");
    CHECK(q(man, u8"a", cpp_pinyin::v2::MandarinStyle::TONE2) == u8"a");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 - basic sentences", "[mandarin][tone3][golden]") {
    CHECK(q(man, u8"明月几时有", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"ming2 yue4 ji3 shi2 you3");
    CHECK(q(man, u8"中华人民共和国", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"zhong1 hua2 ren2 min2 gong4 he2 guo2");
    CHECK(q(man, u8"今天天气很好", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"jin1 tian1 tian1 qi4 hen3 hao3");
    CHECK(q(man, u8"我是中国人", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"wo3 shi4 zhong1 guo2 ren2");
    CHECK(q(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"ni3 hao3 shi4 jie4");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 - polyphonic words", "[mandarin][tone3][polyphonic][golden]") {
    CHECK(q(man, u8"银行行长", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"yin2 hang2 hang2 zhang3");
    CHECK(q(man, u8"音乐快乐", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"yin1 yue4 kuai4 le4");
    CHECK(q(man, u8"重量很重", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"zhong4 liang4 hen3 zhong4");
    CHECK(q(man, u8"长城很长", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"chang2 cheng2 hen3 chang2");
    CHECK(q(man, u8"还是还给他", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"hai2 shi huan2 gei3 ta1");
    CHECK(q(man, u8"大夫看大夫", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"dai4 fu kan4 dai4 fu");
    CHECK(q(man, u8"干净的干果", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"gan1 jing4 de gan1 guo3");
    CHECK(q(man, u8"哪吒", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"ne2 zha1");
    CHECK(q(man, u8"陟罚臧否", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"zhi4 fa2 zang2 pi3");
    CHECK(q(man, u8"到了", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"dao4 le1");
    CHECK(q(man, u8"汤汤", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"shang1 shang1");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 - mixed input", "[mandarin][tone3][mixed][golden]") {
    CHECK(q(man, u8"hello世界", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"hello shi4 jie4");
    CHECK(q(man, u8"好 xiu 的", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"hao3 xiu de");
    CHECK(q(man, u8"hao de", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"hao de");
    CHECK(q(man, u8"明月@1几32时有##一", cpp_pinyin::v2::MandarinStyle::TONE3) ==
          u8"ming2 yue4 @ 1 ji3 3 2 shi2 you3 # # yi1");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 - kana input", "[mandarin][tone3][kana][golden]") {
    CHECK(q(man, u8"東京タワー", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"dong1 jing1 タ ワ ー");
    CHECK(q(man, u8"あいう", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"あ い う");
    CHECK(q(man, u8"举杯あャ坐ュ饮", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"ju3 bei1 あャ zuo4 ュ yin3");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 - edge cases", "[mandarin][tone3][edge][golden]") {
    CHECK(q(man, u8"一", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"yi1");
    CHECK(q(man, u8"a", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"a");
    CHECK(q(man, u8"123", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"1 2 3");
    CHECK(q(man, u8"@#$", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"@ # $");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 neutral_tone_with_five", "[mandarin][tone3][n5][golden]") {
    auto n5 = [&](const std::string &text) { return q(man, text, cpp_pinyin::v2::MandarinStyle::TONE3, true); };
    CHECK(n5(u8"还是还给他") == u8"hai2 shi5 huan2 gei3 ta1");
    CHECK(n5(u8"大夫看大夫") == u8"dai4 fu5 kan4 dai4 fu5");
    CHECK(n5(u8"干净的干果") == u8"gan1 jing4 de5 gan1 guo3");
    CHECK(n5(u8"好 xiu 的") == u8"hao3 xiu de5");
    CHECK(n5(u8"明月几时有") == u8"ming2 yue4 ji3 shi2 you3");
    CHECK(n5(u8"中华人民共和国") == u8"zhong1 hua2 ren2 min2 gong4 he2 guo2");
    CHECK(n5(u8"你好世界") == u8"ni3 hao3 shi4 jie4");
    CHECK(n5(u8"银行行长") == u8"yin2 hang2 hang2 zhang3");
    CHECK(n5(u8"音乐快乐") == u8"yin1 yue4 kuai4 le4");
    CHECK(n5(u8"hello世界") == u8"hello shi4 jie4");
    CHECK(n5(u8"一") == u8"yi1");
}

TEST_CASE_METHOD(PinyinFixture, "Mandarin TONE3 v_to_u", "[mandarin][tone3][v_to_u][golden]") {
    CHECK(q(man, u8"虐", cpp_pinyin::v2::MandarinStyle::TONE3, false, false) == u8"nve4");
    CHECK(q(man, u8"虐", cpp_pinyin::v2::MandarinStyle::TONE3, false, true) == u8"nüe4");
    CHECK(q(man, u8"女", cpp_pinyin::v2::MandarinStyle::TONE3, false, false) == u8"nv3");
    CHECK(q(man, u8"女", cpp_pinyin::v2::MandarinStyle::TONE3, false, true) == u8"nv3");
    CHECK(q(man, u8"绿", cpp_pinyin::v2::MandarinStyle::TONE3, false, false) == u8"lv4");
    CHECK(q(man, u8"绿", cpp_pinyin::v2::MandarinStyle::TONE3, false, true) == u8"lv4");
}

TEST_CASE_METHOD(PinyinFixture, "getDefaultPinyin - candidates", "[mandarin][default_pinyin][golden]") {
    auto join = [](const std::vector<std::string> &v)
    {
        std::string r;
        for (size_t i = 0; i < v.size(); i++) {
            if (i > 0)
                r += ",";
            r += v[i];
        }
        return r;
    };
    auto rd = [&](const std::string &hanzi, cpp_pinyin::v2::MandarinStyle s)
    {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = s;
        return man.readings(hanzi, cfg).value();
    };

    CHECK(join(rd(u8"秀", cpp_pinyin::v2::MandarinStyle::TONE3)) == "xiu4");
    CHECK(join(rd(u8"了", cpp_pinyin::v2::MandarinStyle::TONE3)) == "le,liao3");
    CHECK(join(rd(u8"的", cpp_pinyin::v2::MandarinStyle::TONE3)) == "de,di4,di1,di2");
    CHECK(join(rd(u8"行", cpp_pinyin::v2::MandarinStyle::TONE3)) == "xing2,hang2,xing,heng2");
    CHECK(join(rd(u8"好", cpp_pinyin::v2::MandarinStyle::TONE3)) == "hao3,hao4,hao1");
    CHECK(join(rd(u8"行", cpp_pinyin::v2::MandarinStyle::NORMAL)) == "xing,hang,heng");
    CHECK(join(rd(u8"行", cpp_pinyin::v2::MandarinStyle::TONE)) == u8"xíng,háng,xing,héng");
    CHECK(join(rd(u8"行", cpp_pinyin::v2::MandarinStyle::TONE2)) == "xi2ng,ha2ng,xing,he2ng");
}

TEST_CASE_METHOD(PinyinFixture, "getDefaultPinyin - repeated call stability", "[mandarin][td002][golden]") {
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    for (int i = 0; i < 10; i++) {
        auto res = man.readings(u8"行", cfg).value();
        REQUIRE(res.size() == 4);
        CHECK(res[0] == "xing2");
        CHECK(res[1] == "hang2");
        CHECK(res[2] == "xing");
        CHECK(res[3] == "heng2");
    }
}

TEST_CASE_METHOD(PinyinFixture, "tradToSim", "[mandarin][trad_sim][golden]") {
    CHECK(man.toSimplified(u8"臺").value_or_throw() == u8"台");
    CHECK(man.toSimplified(u8"犬").value_or_throw() == u8"犬");
    CHECK(man.toSimplified(u8"國").value_or_throw() == u8"国");
    CHECK(man.toSimplified(u8"學").value_or_throw() == u8"学");
}

TEST_CASE_METHOD(PinyinFixture, "isPolyphonic", "[mandarin][polyphonic][golden]") {
    CHECK(man.isPolyphonic(u8"的") == true);
    CHECK(man.isPolyphonic(u8"犬") == false);
    CHECK(man.isPolyphonic(u8"行") == true);
    CHECK(man.isPolyphonic(u8"好") == true);
}

TEST_CASE_METHOD(PinyinFixture, "UTF-8 round trip via API", "[utf8][golden]") {
    CHECK(q(man, u8"一", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"yi1");
    CHECK(q(man, u8"a", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"a");
    CHECK(q(man, u8"你好世界", cpp_pinyin::v2::MandarinStyle::TONE3) == u8"ni3 hao3 shi4 jie4");
}
