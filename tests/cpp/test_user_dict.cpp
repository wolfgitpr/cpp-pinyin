// Regression tests for user-dictionary loading.
//
// Background: res/dict/cantonese/user_dict.txt ships empty (0 bytes), so nothing in the fleet
// of golden tests ever exercised the user-dictionary path for Cantonese. Two real defects hid
// behind that:
//
//   * the loader normalized every reading with the MANDARIN tone3 -> tone-marked converter, so a
//     Cantonese user entry came back rewritten in Mandarin diacritics (measured before the fix:
//     `jyut6 jyu5` -> `jyu6t6 jyu`, `gwong2 zau1` -> `gwóng zāu`);
//   * loadUserDict(path, /*clear=*/false) cleared the merged phrase table and skipped reloading
//     phrases_dict.txt, so ALL built-in phrases were lost (measured: 银行 yin2 hang2 -> yin2 xing2).
//
// Also pins the moved-from contract, which used to be a null dereference.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <utility>

#include "dialect_fixtures.h"

namespace
{

    struct UserDictFixture {
        cpp_pinyin::v2::Engine man;
        cpp_pinyin::v2::Engine can;
        std::filesystem::path scratch;

        UserDictFixture() :
            man(cpp_test::mandarin()), can(cpp_test::cantonese()),
            scratch(std::filesystem::temp_directory_path() / "cpp-pinyin2-userdict-test.txt") {}

        ~UserDictFixture() {
            std::error_code ec;
            std::filesystem::remove(scratch, ec);
        }

        void writeScratch(const char *lines) const {
            std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << lines;
            out.close();
            REQUIRE(std::filesystem::exists(scratch));
        }

        static std::string tone3(const cpp_pinyin::v2::Engine &e, const char *text) {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
            return e.query(text, cfg).value().join();
        }

        static std::string tone(const cpp_pinyin::v2::Engine &e, const char *text) {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE;
            return e.query(text, cfg).value().join();
        }

        static std::string canTone3(const cpp_pinyin::v2::Engine &e, const char *text) {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
            return e.query(text, cfg).value().join();
        }

        static std::string canNormal(const cpp_pinyin::v2::Engine &e, const char *text) {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;
            return e.query(text, cfg).value().join();
        }
    };

}  // namespace

TEST_CASE_METHOD(UserDictFixture, "Cantonese user dict keeps jyutping readings", "[userdict][jyutping]") {
    // Readings exactly as a Cantonese user dictionary writes them: tone digits, never diacritics.
    writeScratch(u8"你好:nei5 hou2\n粵語:jyut6 jyu5\n廣州:gwong2 zau1\n");
    REQUIRE(can.loadUserDict(scratch));

    // Tone3 style is pass-through for Cantonese (CanTone registers no TONE3 converter), so the
    // stored reading must be the file's text verbatim -- not a Mandarin diacritic rewriting.
    CHECK(canTone3(can, u8"粵語") == "jyut6 jyu5");
    CHECK(canTone3(can, u8"廣州") == "gwong2 zau1");
    CHECK(canTone3(can, u8"你好") == "nei5 hou2");

    // NORMAL style strips the tone digit; it must never eat a real letter (the old bug produced
    // `jyu6t` / `gwón` here, i.e. the last *digit or letter* was cut blindly).
    CHECK(canNormal(can, u8"粵語") == "jyut jyu");
    CHECK(canNormal(can, u8"廣州") == "gwong zau");
    CHECK(canNormal(can, u8"你好") == "nei hou");
}

TEST_CASE_METHOD(UserDictFixture, "Cantonese user dict readings become candidates", "[userdict][jyutping]") {
    writeScratch(u8"粵語:jyut6 jyu5\n");
    REQUIRE(can.loadUserDict(scratch));

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;
    cfg.includeCandidates = true;
    const auto res = can.query(u8"粵語", cfg).value();
    REQUIRE(res.size() == 2);
    CHECK(res[0].rendered == "jyut6");
    CHECK(res[1].rendered == "jyu5");
    // K9: on a successful row the reading is one of the candidates.
    CHECK(std::find(res[0].candidates.begin(), res[0].candidates.end(), res[0].rendered) != res[0].candidates.end());
}

TEST_CASE_METHOD(UserDictFixture, "Mandarin user dict still converts tone3 to tone-marked", "[userdict][mandarin]") {
    // The dialect hook for Mandarin must keep doing tone3 -> diacritics; the Cantonese fix must
    // not have flattened it into the identity map for everyone.
    writeScratch(u8"你好:ni3 hao3\n仄声:ze4 sheng1\n");
    REQUIRE(man.loadUserDict(scratch));

    CHECK(tone3(man, u8"你好") == "ni3 hao3");
    CHECK(tone(man, u8"你好") == u8"nǐ hǎo");
    CHECK(tone3(man, u8"仄声") == "ze4 sheng1");
    CHECK(tone(man, u8"仄声") == u8"zè shēng");
}

TEST_CASE_METHOD(UserDictFixture, "loadUserDict(clear=false) keeps the built-in phrase layer", "[userdict][mandarin]") {
    // Built-in phrase from phrases_dict.txt: 银行 reads yin2 hang2 (not xing2).
    CHECK(tone3(man, u8"银行") == "yin2 hang2");

    writeScratch(u8"一旦:yi2 dan4\n");
    REQUIRE(man.loadUserDict(scratch, /*clear=*/false));

    CHECK(tone3(man, u8"银行") == "yin2 hang2");  // built-in layer survived the merge
    CHECK(tone3(man, u8"一旦") == "yi2 dan4");  // and the user layer was applied on top

    // Re-loading the shipped Mandarin user dictionary must not shrink anything either.
    const auto shipped = std::filesystem::current_path() / "dict" / "mandarin" / "user_dict.txt";
    REQUIRE(std::filesystem::exists(shipped));
    REQUIRE(man.loadUserDict(shipped, /*clear=*/false));
    CHECK(tone3(man, u8"银行") == "yin2 hang2");
    CHECK(tone3(man, u8"一旦") == "yi2 dan4");
    CHECK(tone3(man, u8"汤汤") == "shang1 shang1");  // entry of the shipped user dictionary
}

TEST_CASE_METHOD(UserDictFixture, "loadUserDict(clear=true) drops the previous user layer", "[userdict][mandarin]") {
    writeScratch(u8"一旦:yi2 dan4\n");
    REQUIRE(man.loadUserDict(scratch, /*clear=*/true));
    CHECK(tone3(man, u8"一旦") == "yi2 dan4");

    writeScratch(u8"仄声:ze4 sheng1\n");
    REQUIRE(man.loadUserDict(scratch, /*clear=*/true));
    CHECK(tone3(man, u8"仄声") == "ze4 sheng1");
    CHECK(tone3(man, u8"一旦") == "yi1 dan4");  // user entry gone, built-in reading back
}

TEST_CASE_METHOD(UserDictFixture, "missing user dict reports failure and keeps phrases", "[userdict][error]") {
    const auto missing = std::filesystem::temp_directory_path() / "cpp-pinyin2-does-not-exist.txt";
    const auto loaded = man.loadUserDict(missing);
    CHECK_FALSE(loaded.has_value());
    // The reason is part of the contract again (2026-10-19: `Outcome` carries the library's own
    // detail, which `lastError()` used to hide behind a private accessor). `Code::Load` -- not
    // `NotReady` -- is the answer here: the engine is fine, the path is not.
    CHECK(loaded.error().code == cpp_pinyin::v2::Code::Load);
    CHECK_FALSE(loaded.error().detail.empty());
    CHECK(tone3(man, u8"银行") == "yin2 hang2");  // still usable
}

TEST_CASE_METHOD(UserDictFixture, "traditional-keyed user entry matches its simplified input", "[userdict][c04]") {
    // C-04/D8: the hot path matches phrases on SIMPLIFIED characters (assemble() hands toSimp()'d
    // characters to queryRawInto), so a key written with characters this dialect maps used to be
    // unreachable from the simplified spelling -- a silent failure for anyone who typed the
    // traditional one. An alias per key is registered at load time, and its first character is added
    // to the window gate as well; without that second half this test would fail the way the C-03
    // gate defect did.
    //
    // The characters matter: 學 IS in trans_word.txt (學:学) while 咤 is NOT (it is a variant, not a
    // mapped traditional form), so a "哪咤" fixture would prove nothing -- toSimp() leaves it alone,
    // the original key is what gets registered, and that is the behaviour being changed here.
    // The reading is deliberately NOT the built-in one for 学校: an alias has to override the
    // built-in entry, because the user layer is documented to win for duplicate keys.
    writeScratch(u8"學校:xue2 xiao1\n");
    REQUIRE(man.loadUserDict(scratch.string()));
    CHECK(tone3(man, u8"學校") == "xue2 xiao1");  // original (traditional) spelling
    CHECK(tone3(man, u8"学校") == "xue2 xiao1");  // simplified spelling, via the alias
}

TEST_CASE("alias collisions resolve the same way regardless of dictionary order", "[userdict][c04]") {
    // 發 and 髮 both simplify to 发 (trans_word.txt has 180 such groups), so 發生 and 髮生 collapse
    // onto the SAME alias key. Before the sort in rebuildPhrases() the winner was decided by
    // unordered_map iteration order, so identical dictionary content could answer differently
    // depending on the order the lines happened to be written in -- and differently again after a
    // reload. Two engines fed the same two entries in OPPOSITE order must agree; loading the same
    // file twice would not discriminate, because one insertion sequence keeps one iteration order.
    const std::filesystem::path dir = std::filesystem::current_path() / "dict";
    const std::filesystem::path fileA = std::filesystem::temp_directory_path() / "cpp-pinyin2-c04-a.txt";
    const std::filesystem::path fileB = std::filesystem::temp_directory_path() / "cpp-pinyin2-c04-b.txt";
    const auto write = [](const std::filesystem::path &p, const char *lines)
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << lines;
    };
    write(fileA, u8"發生:fa1 sheng1\n髮生:fa4 sheng1\n");
    write(fileB, u8"髮生:fa4 sheng1\n發生:fa1 sheng1\n");

    const auto tone3 = [](const cpp_pinyin::v2::Engine &e, const char *text)
    {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        return e.query(text, cfg).value().join();
    };
    cpp_pinyin::v2::Engine engineA(cpp_test::openDialect("mandarin", dir));
    cpp_pinyin::v2::Engine engineB(cpp_test::openDialect("mandarin", dir));
    REQUIRE(engineA.loadUserDict(fileA.string()));
    REQUIRE(engineB.loadUserDict(fileB.string()));
    const std::string fromA = tone3(engineA, u8"发生");  // the collapsed simplified key
    CHECK(fromA == tone3(engineB, u8"发生"));
    CHECK(fromA == tone3(engineA, u8"發生"));  // both original spellings must agree with it too
    CHECK(fromA == tone3(engineA, u8"髮生"));

    std::error_code ec;
    std::filesystem::remove(fileA, ec);
    std::filesystem::remove(fileB, ec);
}

TEST_CASE_METHOD(UserDictFixture, "an alias beats a built-in entry but not an explicit user key", "[userdict][c04]") {
    // The riskiest half of C-04/D8, and the one an adversarial review found entirely uncovered:
    // 头发 IS a built-in phrase (phrases_dict.txt has "头发:tóu,fa"), so the alias for 頭髮 has to
    // OVERRIDE it -- aliases are merged before the user layer precisely because the user layer is
    // documented to win over the built-in layer. The comment deliberately does not pin the old
    // built-in reading: what matters is that the user's reading reaches both spellings.
    writeScratch(u8"頭髮:tou2 fa4\n");
    REQUIRE(man.loadUserDict(scratch.string()));
    CHECK(tone3(man, u8"头发") == "tou2 fa4");
    CHECK(tone3(man, u8"頭髮") == "tou2 fa4");
}

TEST_CASE("a later user dict wins for a colliding alias key", "[userdict][c04]") {
    // docs/API_zh.md: loadUserDict(path, false) merges on top and the newly loaded file wins for
    // duplicate keys. Aliases must obey that too -- while they were stored in the user layer, a
    // second load could not overwrite one, so the second file silently lost and the first kept its
    // reading forever. 頭發 and 頭髮 collapse onto the same simplified key, which is exactly the
    // collision the mapping table produces ~170-180 times.
    const std::filesystem::path dir = std::filesystem::current_path() / "dict";
    const std::filesystem::path first = std::filesystem::temp_directory_path() / "cpp-pinyin2-c04-1st.txt";
    const std::filesystem::path second = std::filesystem::temp_directory_path() / "cpp-pinyin2-c04-2nd.txt";
    const auto write = [](const std::filesystem::path &p, const char *lines)
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << lines;
    };
    write(first, u8"頭發:tou2 fa1\n");
    write(second, u8"頭髮:tou2 fa4\n");

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    const auto reading =
        [&cfg](cpp_pinyin::v2::Engine &e, const std::filesystem::path &a, const std::filesystem::path &b)
    {
        CHECK(e.loadUserDict(a.string(), false));
        CHECK(e.loadUserDict(b.string(), false));
        return e.query(u8"头发", cfg).value().join();
    };
    // Two DIFFERENT keys that collapse onto one alias key. The user asked for both, so there is no
    // "the later one wins" here -- API_zh.md promises that only for the SAME key. What must hold is
    // that the answer is deterministic and independent of the order the files were loaded in; before
    // the tie-break it was decided by unordered_map iteration order, and while aliases lived in the
    // user layer a second load silently lost instead. Deliberately no assertion on WHICH reading
    // wins: that tie-break is an implementation choice, documented next to the sort itself.
    cpp_pinyin::v2::Engine engineAB(cpp_test::openDialect("mandarin", dir));
    cpp_pinyin::v2::Engine engineBA(cpp_test::openDialect("mandarin", dir));
    CHECK(reading(engineAB, first, second) == reading(engineBA, second, first));

    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
}

TEST_CASE("moved-from engine answers NotReady instead of crashing", "[userdict][move]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();
    REQUIRE(man.query(u8"行", {}));  // an engine that opened can be queried: that IS the readiness probe

    cpp_pinyin::v2::Engine moved(std::move(man));
    REQUIRE(moved.query(u8"行", {}));  // the moved-INTO side works as usual
    // The moved-FROM side is an empty object: a query must return NotReady rather than dereference a null
    // pointer (this used to read out of bounds). Using the result's error code as the probe is stronger
    // than the boolean check it replaced -- it proves both that nothing crashed and that the error channel
    // gives the right reason.
    const auto fromMoved = man.query(u8"行", {});
    REQUIRE_FALSE(fromMoved);
    CHECK(fromMoved.error().code == cpp_pinyin::v2::Code::NotReady);
    CHECK(fromMoved.error().detail.empty());  // "never opened" is not a load failure, so detail must be empty

    cpp_pinyin::v2::Engine can = cpp_test::cantonese();
    REQUIRE(can.query(u8"行", {}));
    cpp_pinyin::v2::Engine canMoved(std::move(can));
    REQUIRE(canMoved.query(u8"行", {}));
    const auto canFromMoved = can.query(u8"行", {});
    REQUIRE_FALSE(canFromMoved);
    CHECK(canFromMoved.error().code == cpp_pinyin::v2::Code::NotReady);
    CHECK(canFromMoved.error().detail.empty());
}
