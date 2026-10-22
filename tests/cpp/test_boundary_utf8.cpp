#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "dialect_fixtures.h"

// Boundary regression tests for text decoding (plan defects C-16 / C-17).
//
// Why these exist: every case below used to either (a) abort the process by throwing
// std::invalid_argument out of query(), or (b) silently change the number of result
// entries. Both are fatal for callers: (a) is a crash on real-world text containing
// an emoji, and (b) breaks positional alignment, which is what the accuracy datasets
// rely on. The invariant asserted here is:
//
//     one input code point  ->  exactly one result entry
//
// with a malformed sequence counting as exactly one code point (U+FFFD).
namespace
{

    // 一 (U+4E00) and 二 (U+4E8C): one entry each, so a well-formed
    // "一 <something> 二" must always produce exactly three entries.
    const std::string A = "\xE4\xB8\x80";
    const std::string B = "\xE4\xBA\x8C";
    const std::string REPLACEMENT = "\xEF\xBF\xBD";  // U+FFFD

    struct Utf8Fixture {
        cpp_pinyin::v2::Engine man;

        Utf8Fixture() : man(cpp_test::mandarin()) {
            // The engine is opened with .value(), which throws if it fails -- an "is it ready"
            // assertion here would be vacuous.
        }
    };

    /// Query with candidates disabled so entries map 1:1 to input code points.
    cpp_pinyin::v2::Result run(cpp_pinyin::v2::Engine &m, const std::string &text) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        cfg.includeCandidates = false;
        return m.query(text, cfg).value();
    }

    /// TONE3 form of U+4E00 / U+4E8C.
    bool isYi(const std::string &s) { return s == u8"yi1"; }
    bool isEr(const std::string &s) { return s == u8"er4"; }

    /// Structure check that never depends on a reading being "correct" or on a
    /// character being registered: entry 0 is 一, everything malformed in the middle
    /// collapsed into ONE entry, last is 二. The `failed` flag is deliberately not
    /// asserted here -- it varies by whether the character happens to be registered.
    void checkSandwich(cpp_pinyin::v2::Engine &m, const std::string &mid, const char *what) {
        const std::string input = A + mid + B;
        const cpp_pinyin::v2::Result r = run(m, input);
        INFO("case: " << what);
        REQUIRE(r.size() == 3);  // A, one replacement, B
        CHECK(r[0].text == A);
        CHECK(isYi(r[0].rendered));
        CHECK(r[1].text == REPLACEMENT);
        CHECK(r[2].text == B);
        CHECK(isEr(r[2].rendered));
    }
}  // namespace

TEST_CASE_METHOD(Utf8Fixture, "UTF8-001: legal astral characters do not abort and take one entry",
                 "[utf8][boundary][c16]") {
    SECTION("U+1F600 grinning face") {
        const cpp_pinyin::v2::Result r = run(man, A + std::string("\xF0\x9F\x98\x80", 4) + B);
        REQUIRE(r.size() == 3);
        CHECK(r[0].text == A);
        // The astral character itself survives as the original bytes, not as two
        // halves. It is not in the dictionary, so failed=true is correct here --
        // same as any other unregistered character.
        CHECK(r[1].text == std::string("\xF0\x9F\x98\x80", 4));
        CHECK((r[1].failure_kind != cpp_pinyin::v2::Code::Ok));
        CHECK(r[2].text == B);
    }

    SECTION("U+20000 (first code point of CJK Ext-B)") {
        const std::string extB = "\xF0\xA0\x80\x80";
        const cpp_pinyin::v2::Result r = run(man, A + extB + B);
        REQUIRE(r.size() == 3);
        // One entry, and the original bytes -- not two surrogate halves.
        CHECK(r[1].text == extB);
    }

    SECTION("U+10FFFF (maximum code point is legal, not a replacement)") {
        const std::string maxCp = "\xF4\x8F\xBF\xBF";
        const cpp_pinyin::v2::Result r = run(man, A + maxCp + B);
        REQUIRE(r.size() == 3);
        CHECK(r[0].text == A);
        CHECK(r[1].text == maxCp);  // kept as itself
        CHECK(r[2].text == B);
    }
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-002: truncated and lone-continuation input never throws", "[utf8][boundary][c16]") {
    checkSandwich(man, std::string("\xC2", 1), "truncated 2-byte");
    checkSandwich(man, std::string("\xE4\xB8", 2), "truncated 3-byte");
    checkSandwich(man, std::string("\xF0\x9F\x98", 3), "truncated 4-byte");
    checkSandwich(man, std::string("\x80", 1), "lone continuation byte");

    SECTION("a lone continuation byte alone yields one replacement entry") {
        const cpp_pinyin::v2::Result r = run(man, std::string("\x80", 1));
        REQUIRE(r.size() == 1);
        CHECK(r[0].text == REPLACEMENT);
        CHECK((r[0].failure_kind != cpp_pinyin::v2::Code::Ok));
    }
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-003: overlong encodings are rejected as one entry", "[utf8][boundary][c17]") {
    checkSandwich(man, std::string("\xC0\x80", 2), "overlong NUL");
    checkSandwich(man, std::string("\xC0\xAF", 2), "overlong solidus");
    checkSandwich(man, std::string("\xE0\x80\x80", 3), "overlong 3-byte NUL");
    checkSandwich(man, std::string("\xF0\x80\x80\x80", 4), "overlong 4-byte NUL");

    SECTION("an overlong NUL must not be decoded as a real NUL byte") {
        const cpp_pinyin::v2::Result r = run(man, A + std::string("\xC0\x80", 2) + B);
        REQUIRE(r.size() == 3);
        CHECK(r[1].text == REPLACEMENT);
        CHECK(r[1].text != std::string("\x00", 1));
    }
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-004: UTF-8 encoded surrogates are rejected", "[utf8][boundary][c16][c17]") {
    checkSandwich(man, std::string("\xED\xA0\x80", 3), "encoded high surrogate");
    checkSandwich(man, std::string("\xED\xB0\x80", 3), "encoded low surrogate");
    checkSandwich(man, std::string("\xED\xBF\xBF", 3), "encoded U+DFFF");
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-005: code points above U+10FFFF are rejected", "[utf8][boundary][c17]") {
    checkSandwich(man, std::string("\xF7\xBF\xBF\xBF", 4), "F7 lead");
    checkSandwich(man, std::string("\xF5\x80\x80\x80", 4), "F5 lead");
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-006: decoding resumes after a malformed sequence", "[utf8][boundary][c16]") {
    // The suffix must NOT be swallowed: the buggy version aborted the rest of the
    // string at the first malformed byte, so 二 disappeared entirely.
    SECTION("malformed in the middle, both neighbours intact") {
        const cpp_pinyin::v2::Result r = run(man, A + std::string("\xC0\x80", 2) + B);
        REQUIRE(r.size() == 3);
        CHECK(r[2].text == B);  // the suffix must not be swallowed
    }

    SECTION("two malformed sequences each take exactly one entry") {
        const cpp_pinyin::v2::Result r = run(man, A + std::string("\x80", 1) + B + std::string("\x80", 1));
        REQUIRE(r.size() == 4);
        CHECK(r[0].text == A);
        CHECK(r[1].text == REPLACEMENT);
        CHECK(r[2].text == B);
        CHECK(r[3].text == REPLACEMENT);
    }
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-007: NUL, combining marks and empty input", "[utf8][boundary]") {
    SECTION("embedded NUL is preserved as its own entry") {
        const cpp_pinyin::v2::Result r = run(man, A + std::string("\x00", 1) + B);
        REQUIRE(r.size() == 3);
        CHECK(r[1].text == std::string("\x00", 1));
    }

    SECTION("combining mark is its own entry (no grapheme clustering)") {
        const std::string combining = "\xCC\x81";  // U+0301
        const cpp_pinyin::v2::Result r = run(man, A + combining + B);
        REQUIRE(r.size() == 3);
        CHECK(r[1].text == combining);
    }

    SECTION("empty input yields no entries") { CHECK(run(man, "").empty()); }
}

TEST_CASE_METHOD(Utf8Fixture, "UTF8-008: an unregistered single character is one failed entry", "[utf8][boundary]") {
    // This is the contract that makes positional alignment meaningful: an unknown
    // character occupies exactly one slot, is marked failed, and its "reading" is the
    // character itself. Verified for BMP, ASCII, the replacement character and an
    // astral character.
    const std::string cases[] = {
        std::string("\xE2\x96\xA1", 3),  // □ U+25A1, BMP, not in dict
        std::string("A", 1),  // ASCII
        std::string("\xEF\xBF\xBD", 3),  // U+FFFD
        std::string("\xF0\x9F\x98\x80", 4),  // U+1F600 emoji
    };
    for (const std::string &s : cases) {
        INFO("input bytes: " << s);
        const cpp_pinyin::v2::Result r = run(man, s);
        REQUIRE(r.size() == 1);
        CHECK(r[0].text == s);
        CHECK(r[0].rendered == s);
        CHECK((r[0].failure_kind != cpp_pinyin::v2::Code::Ok));
    }
}
