// Contract tests for candidate readings (`Segment::candidates`).
//
// These cases pin the MEASURED semantics of the candidate feature; the authority is docs/REQUIREMENTS.md
// section 1 K9, and the conclusion of a 3.6M-line full-corpus scan (docs/HISTORY.md).
// Wherever the behaviour disagrees with intuition the reason is written out -- these are DELIBERATE layered-dictionary
// semantics, not bugs waiting to be fixed:
//   * for a row that hit the phrase or user dictionary, `reading` comes from the phrase table and may differ
//     from `candidates[0]`, or not appear in `candidates` at all;
//   * `candidates` is always the character's own full reading list (= `Mandarin::readings(character, cfg)`);
//   * with `includeCandidates=false` a Hanzi row has no candidates, with the FAILED rows as the exception
//     (they are always `{character}`).
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "dialect_fixtures.h"

namespace
{

    std::vector<std::string> sentencesOf(const std::filesystem::path &path, const size_t limit) {
        std::vector<std::string> out;
        std::ifstream file(path);
        if (!file.is_open())
            return out;
        std::string line;
        while (out.size() < limit && std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            // The verified tables are `sentence<TAB>cells<TAB>status<TAB>notes`; the legacy corpora in
            // tests/verified/source/ used `▁` or `|` between the sentence and its readings. Both are
            // accepted, and `#` (the `#!meta` header, comments) is skipped.
            if (line.empty() || line[0] == '#')
                continue;
            size_t sep = line.find('\t');
            if (sep == std::string::npos)
                sep = line.find(u8"▁");
            if (sep == std::string::npos)
                sep = line.find('|');
            if (sep == std::string::npos)
                continue;
            out.push_back(line.substr(0, sep));
        }
        return out;
    }


    bool contains(const std::vector<std::string> &haystack, const std::string &needle) {
        return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
    }

    cpp_pinyin::v2::EngineConfig manConfig(const cpp_pinyin::v2::MandarinStyle style) {
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = style;
        return cfg;
    }

}  // namespace

TEST_CASE("Candidates: 多音字的候选顺序 = word.txt 行序（去重在转换之后）", "[candidates][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    // The dictionary row is 行:xíng,háng,xing,héng. Deduplication happens AFTER conversion and keeps
    // the first occurrence, so NORMAL leaves only xing,hang,heng.
    const auto tone3 = man.query(u8"行", manConfig(cpp_pinyin::v2::MandarinStyle::TONE3)).value();
    REQUIRE(tone3.size() == 1);
    REQUIRE(tone3[0].rendered == "xing2");
    REQUIRE(tone3[0].candidates == std::vector<std::string>({"xing2", "hang2", "xing", "heng2"}));
    REQUIRE(tone3[0].rendered == tone3[0].candidates.front());
    REQUIRE_FALSE((tone3[0].failure_kind != cpp_pinyin::v2::Code::Ok));

    const auto tone = man.query(u8"行", manConfig(cpp_pinyin::v2::MandarinStyle::TONE)).value();
    REQUIRE(tone[0].rendered == u8"xíng");
    REQUIRE(tone[0].candidates == std::vector<std::string>({u8"xíng", u8"háng", "xing", u8"héng"}));

    const auto normal = man.query(u8"行", manConfig(cpp_pinyin::v2::MandarinStyle::NORMAL)).value();
    REQUIRE(normal[0].candidates == std::vector<std::string>({"xing", "hang", "heng"}));

    const auto first = man.query(u8"行", manConfig(cpp_pinyin::v2::MandarinStyle::FIRST_LETTER)).value();
    REQUIRE(first[0].candidates == std::vector<std::string>({"x", "h"}));
}

TEST_CASE("Candidates: 词组命中行 reading 来自词组表，candidates 仍为该字的全部读音", "[candidates][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    // In the phrase table 银行 -> 行 reads háng, while `candidates` is 行's own reading list -- hence
    // `reading != candidates[0]`. This is pypinyin's phrase-dict-overrides-heteronym semantics, see
    // docs/REQUIREMENTS.md section 1 K9.3.
    const auto bank = man.query(u8"银行", manConfig(cpp_pinyin::v2::MandarinStyle::TONE3)).value();
    REQUIRE(bank.size() == 2);
    REQUIRE(bank[0].rendered == "yin2");
    REQUIRE(bank[1].rendered == "hang2");
    REQUIRE(bank[1].candidates == std::vector<std::string>({"xing2", "hang2", "xing", "heng2"}));
    REQUIRE(bank[1].candidates.front() != bank[1].rendered);
    REQUIRE(contains(bank[1].candidates, bank[1].rendered));

    // A user dictionary may carry a reading the character table does not have: 汤汤:shang1 shang1, whose
    // `reading` is not in `candidates`. That is an inherent result of the layered dictionary (K9.3), not
    // a defect.
    const auto soup = man.query(u8"汤汤", manConfig(cpp_pinyin::v2::MandarinStyle::TONE3)).value();
    REQUIRE(soup.size() == 2);
    REQUIRE(soup[0].rendered == "shang1");
    REQUIRE(soup[0].candidates == std::vector<std::string>({"tang1", "tang2"}));
    REQUIRE_FALSE(contains(soup[0].candidates, soup[0].rendered));
}

TEST_CASE("Candidates: 汉字行的候选恒等于 readings(character)", "[candidates][mandarin][contract]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    // The full-corpus scan (3.6M lines) already established I1; this re-checks sampled sentences row by
    // row so that candidate construction cannot diverge later. Assertions are aggregated per row, because
    // a CHECK per line would make the suite's assertion totals meaningless.
    const auto sentences = sentencesOf("verified/mandarin_tone.txt", 400);
    if (sentences.empty()) {
        WARN("SKIP: verified/mandarin_tone.txt not found");
        return;
    }

    for (const auto style : {cpp_pinyin::v2::MandarinStyle::TONE3, cpp_pinyin::v2::MandarinStyle::TONE}) {
        const auto cfg = manConfig(style);
        size_t rows = 0;
        size_t mismatches = 0;
        std::string firstBad;
        for (const auto &sentence : sentences) {
            const auto result = man.query(sentence, cfg).value();
            for (const auto &syl : result) {
                if ((syl.failure_kind != cpp_pinyin::v2::Code::Ok))
                    continue;
                ++rows;
                if (syl.candidates != man.readings(syl.text, cfg).value()) {
                    if (mismatches == 0)
                        firstBad = syl.text;
                    ++mismatches;
                }
            }
        }
        INFO("style=" << static_cast<int>(style) << " first mismatch char=" << firstBad);
        CHECK(rows > 0);
        CHECK(mismatches == 0);
    }
}

TEST_CASE("Candidates: 失败行恒为 {character}，includeCandidates=false 不改变 reading/failed",
          "[candidates][mandarin][contract]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    auto withCands = manConfig(cpp_pinyin::v2::MandarinStyle::TONE3);
    withCands.includeCandidates = true;
    auto withoutCands = withCands;
    withoutCands.includeCandidates = false;

    // Failed rows (punctuation, ASCII, digits), line by line: `reading == character`, and with
    // `includeCandidates=true` the candidates are always `{character}` (K8: an entry with no reading keeps
    // the original text).
    const auto failed = man.query(u8"，abc1", withCands).value();
    REQUIRE_FALSE(failed.empty());
    for (const auto &syl : failed) {
        REQUIRE((syl.failure_kind != cpp_pinyin::v2::Code::Ok));
        REQUIRE(syl.rendered == syl.text);
        REQUIRE(syl.candidates == std::vector<std::string>({syl.text}));
    }

    // A failed row keeps `{character}` even with `includeCandidates=false`: it comes from the same rule as
    // `reading == character`, and changing it would break the frozen dump convention -- so K9.2 records it
    // as existing behaviour, deliberately, rather than as an oversight.
    const auto failedNoCands = man.query(u8"，abc1", withoutCands).value();
    REQUIRE(failedNoCands.size() == failed.size());
    for (const auto &syl : failedNoCands) {
        REQUIRE((syl.failure_kind != cpp_pinyin::v2::Code::Ok));
        REQUIRE(syl.candidates == std::vector<std::string>({syl.text}));
    }

    // A Hanzi row with `includeCandidates=false` gets a plain empty vector.
    for (const char *text : {u8"你好", u8"行", u8"银行", u8"女"}) {
        const auto with = man.query(text, withCands).value();
        const auto without = man.query(text, withoutCands).value();
        REQUIRE(with.size() == without.size());
        for (size_t i = 0; i < with.size(); ++i) {
            CHECK_FALSE(with[i].candidates.empty());
            CHECK(without[i].candidates.empty());
            // Turning candidates off must not change the selected reading or the failure flag.
            CHECK(without[i].rendered == with[i].rendered);
            CHECK((without[i].failure_kind != cpp_pinyin::v2::Code::Ok) ==
                  (with[i].failure_kind != cpp_pinyin::v2::Code::Ok));
            CHECK(without[i].text == with[i].text);
        }
    }
}

TEST_CASE("Candidates: 配置透传到候选（markNeutralAs5 / vToU）", "[candidates][mandarin]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    auto cfg = manConfig(cpp_pinyin::v2::MandarinStyle::TONE3);
    cfg.markNeutralAs5 = true;
    const auto neutral = man.query(u8"一个半", cfg).value();
    REQUIRE(neutral.size() == 3);
    REQUIRE(neutral[1].rendered == "ge5");
    REQUIRE(neutral[1].candidates == std::vector<std::string>({"ge4", "ge5", "ge3"}));

    // The dictionary row for 女 is `女:nǚ,nü`: the first reading uses the TONED ǚ (table lookup maps it to
    // 'v', vToU does not affect it), the second uses ü ITSELF (with vToU=true the 'ü' is kept). Each
    // candidate is therefore converted by the same rule -- the existing contract pinned by
    // test_golden_pinyin.cpp "Mandarin TONE3 v_to_u". This case's cfg sets markNeutralAs5=true, so the
    // toneless second candidate carries a 5.
    cfg.vToU = true;
    const auto umlaut = man.query(u8"女", cfg).value();
    REQUIRE(umlaut.size() == 1);
    REQUIRE(umlaut[0].rendered == "nv3");
    REQUIRE(umlaut[0].candidates == std::vector<std::string>({"nv3", u8"nü5"}));

    cfg.vToU = false;
    cfg.markNeutralAs5 = false;
    const auto asciiV = man.query(u8"女", cfg).value();
    REQUIRE(asciiV[0].rendered == "nv3");
    REQUIRE(asciiV[0].candidates == std::vector<std::string>({"nv3", "nv"}));
}

TEST_CASE("Candidates: readings() 只取首字，且不受 includeCandidates 影响", "[candidates][mandarin][api]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();

    auto cfg = manConfig(cpp_pinyin::v2::MandarinStyle::TONE3);
    cfg.includeCandidates = false;

    // The meaning of readings() is "every reading of this character" and has nothing to do with whether a
    // query wants candidates; for a multi-character argument only the first character is read.
    const auto one = man.readings(u8"行", cfg).value();
    REQUIRE(one == std::vector<std::string>({"xing2", "hang2", "xing", "heng2"}));
    const auto multi = man.readings(u8"银行", cfg).value();
    REQUIRE(multi == std::vector<std::string>({"yin2"}));
}

TEST_CASE("Candidates: 粤语同一套语义", "[candidates][cantonese]") {
    cpp_pinyin::v2::Engine can = cpp_test::cantonese();
    // The fixture opens the dialect with .value(), which throws if it cannot -- so an "is it ready"
    // assertion here would be vacuous, and was removed.

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;

    // The first reading of 行 is haang4, not hang4. That was not an arbitrary edit: in the measured corpus
    // haang4 occurs 67 times and hang4 50 times, while the engine ranked hang4 first -- so it was wrong at
    // all 67 positions where the corpus wants haang4 and right at the 50 that want hang4. Promoting haang4
    // is a net +13 polyphonic positions (see .tmp/SHIP.md round 10 and data/can_overrides.py). The reading
    // SET is unchanged: what this case asserts is the ORDER, while `reading == candidates.front()` below is
    // the invariant it really guards -- the selected reading and the first candidate must be the same, or
    // the query result and the candidate table would disagree.
    const auto hang = can.query(u8"行", cfg).value();
    REQUIRE(hang.size() == 1);
    REQUIRE(hang[0].rendered == "haang4");
    REQUIRE(hang[0].candidates == std::vector<std::string>({"haang4", "hang4", "hong4", "hang6", "hong2"}));
    REQUIRE(hang[0].rendered == hang[0].candidates.front());
    REQUIRE(hang[0].candidates == can.readings(u8"行", cfg).value());

    // A traditional-character phrase key hits, and the candidates come from the traditional glyph itself
    // (glyph-dependent: the candidate source follows the glyph, and the retired K9.3 label said the same).
    const auto bank = can.query(u8"銀行", cfg).value();
    REQUIRE(bank.size() == 2);
    REQUIRE(bank[1].text == u8"行");
    REQUIRE(bank[1].rendered == "hong4");
    // This line used to read "hang4", 行's old first reading. Once the phrase 銀行 hits, `reading` comes
    // from the phrase table (hong4), while `candidates`
    // still comes from word.txt's full reading list, so it follows that file's first reading -- the two must
    // be changed together, or one will be right and the other wrong.
    REQUIRE(bank[1].candidates.front() == "haang4");

    cfg.includeCandidates = false;
    const auto noCands = can.query(u8"行", cfg).value();
    REQUIRE(noCands[0].candidates.empty());
    REQUIRE(noCands[0].rendered == "haang4");

    const auto failed = can.query("abc", cfg).value();
    REQUIRE(failed.size() == 1);
    REQUIRE((failed[0].failure_kind != cpp_pinyin::v2::Code::Ok));
    REQUIRE(failed[0].candidates == std::vector<std::string>({"abc"}));

    // The Cantonese re-check of I1, sampled the same way as Mandarin and with assertions aggregated per row.
    const auto sentences = sentencesOf("verified/cantonese_tone.txt", 400);
    if (sentences.empty()) {
        WARN("SKIP: verified/cantonese_tone.txt not found");
        return;
    }
    cpp_pinyin::v2::EngineConfig cands;
    size_t rows = 0;
    size_t mismatches = 0;
    std::string firstBad;
    for (const auto &sentence : sentences) {
        // The query result must be bound to a named object before iterating: query() returns an Outcome and
        // .value() yields a REFERENCE into that temporary Outcome, whose lifetime extension does not reach
        // through it -- writing it straight into a range-for iterates destroyed storage (the B1 migration
        // introduced exactly this dangling range).
        const auto rendered = can.query(sentence, cands).value();
        for (const auto &syl : rendered) {
            if ((syl.failure_kind != cpp_pinyin::v2::Code::Ok))
                continue;
            ++rows;
            if (syl.candidates != can.readings(syl.text, cands).value()) {
                if (mismatches == 0)
                    firstBad = syl.text;
                ++mismatches;
            }
        }
    }
    INFO("first mismatch char=" << firstBad);
    CHECK(rows > 0);
    CHECK(mismatches == 0);
}
