#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "dialect_fixtures.h"

// Accuracy v2: the verified corpus (tests/verified/) with strict, per-position judging. It replaces
// test_batch_accuracy.cpp (deleted 2026-09-29), whose legacy sets were never verified sentence by sentence --
// their numbers mixed engine quality with annotation quality (the retired T-03/T-04/T-05 caveats, summarised in
// docs/HISTORY.md) -- and whose Cantonese sets were judged with std::find over the whole row, which
// overstates accuracy (T-01, T-02/T-11). Every row carries its own judging rule and a status marker; a position counts
// as verified only if two independent reviewers decided it (adjudicated when they differed) or the character has
// exactly ONE reading.
//   OK / ALT_OK / DATA_FIX count in the denominator; ENGINE_BUG / DICT_GAP / EXCLUDED are separate, listed in
//   docs/, and asserted as EXACT numbers that may only go down: a fixed defect must be re-frozen here.
// strict = pass / (all rows with a decided verified); algo = pass / (decided - DICT_GAP).

namespace
{

    struct SetSpec {
        const char *file;
        const char *id;
        bool cantonese;
        bool tone3;
    };

    struct FrozenCounts {
        const char *id;
        size_t decided;  // OK + ALT_OK + DATA_FIX + ENGINE_BUG + DICT_GAP
        size_t pass;  // engine matched the verified reading at every position
        size_t altOk;
        size_t dataFix;
        size_t engineBug;
        size_t dictGap;
        size_t excluded;
        uint64_t verdictFnv;  // per-row "<id>:<STATUS>:<P|F|X>\n" digest, see fnvUpdate below
    };

    // ---- Per-row verdict fingerprint ------------------------------------------------------------------
    // Aggregates cannot see a compensating change -- one row that starts passing while another starts failing leaves
    // every number in kFrozen[] untouched -- so the digest pins the VECTOR: for every data row, in file order, the
    // bytes "<id>:<STATUS>:<P|F|X>\n" (P = every position matched, F = not, X = the row takes part in no statistic).
    // tools/corpus/check_corpus.py computes the same bytes independently from the probe dump against one shared
    // definition; the frozen values came from the Python side and are verified here, so drift on either side fails.
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t fnvUpdate(uint64_t h, const std::string &text) {
        for (char ch : text) {
            h ^= static_cast<unsigned char>(ch);
            h *= kFnvPrime;
        }
        return h;
    }

    // The row id as the tooling writes it in the note field ("...;id=cm:123" / "id=cm:123;...").
    // Mirrors the pattern check_corpus.py uses, including the "?" fallback, so that a note without a
    // well-formed id produces the same bytes on both sides instead of a mystery diff.
    std::string rowId(const std::string &note) {
        for (size_t p = note.find("id="); p != std::string::npos; p = note.find("id=", p + 3)) {
            if (p != 0 && note[p - 1] != ';')
                continue;
            const size_t end = note.find(';', p);
            const std::string candidate = note.substr(p + 3, (end == std::string::npos ? note.size() : end) - (p + 3));
            const size_t colon = candidate.find(':');
            if (colon == std::string::npos || colon == 0)
                continue;
            bool ok = true;
            for (size_t i = 0; i < colon && ok; i++)
                if (candidate[i] < 'a' || candidate[i] > 'z')
                    ok = false;
            for (size_t i = colon + 1; i < candidate.size() && ok; i++)
                if (candidate[i] < '0' || candidate[i] > '9')
                    ok = false;
            if (ok && colon + 1 < candidate.size())
                return candidate;
        }
        return "?";
    }

    // Frozen from .tmp/corpus/check_corpus.py: exact values on purpose, because a threshold would let a regression
    // hide under it, and the counts may only go down -- rows that moved pass -> DICT_GAP/ENGINE_BUG were wrong in the
    // CORPUS (itemised in the ledgers), not regressions. Two independent routes reproduce every row: this test's live
    // run and tools/corpus/check_corpus.py --with-probe (identity=True for all four). The Mandarin rows come from a
    // regenerated dictionary; every reading change had to gain somewhere with no loss
    // anywhere across all four frozen corpora, so where no subset can do that the trade is PRICED, not avoided -- the
    // untone rows are spent deliberately, and SET_FIRST_READING changes only the preference ORDER within an
    // asserted-unchanged set. Rows, never wrong-position counts, are the judged unit (position totals can improve while
    // rows are lost), Cantonese stays byte-identical every round (the control), and the phrase table is deliberately
    // NOT frozen because clip_long_phrase() depends on default_pinyin[ch][0].
    // POLYPHONE ACCURACY -- the project's standard accuracy figure, the two languages never combined (different
    // dictionaries, corpora and syllable inventories: a merged number would average unrelated things). POLYPHONE =
    // the engine's own isPolyphonic(): a single-reading character is not a polyphone even when wrong. EXACT = the
    // reading equals the gold cell or a member of an "a/b" cell; SYLLABLE = the same after dropping a trailing tone
    // digit -- both are reported because they differ by thousands of positions on Mandarin, so a figure that does not
    // say which rule it used could not be reproduced. A gold cell holding hanzi means the CORPUS did not decide that
    // position: excluded here, while the row metric above counts it as never correct. Chosen over the strict row
    // figure because a row passes only if EVERY position matches, so one hard character counts the same as five.
    struct PolyFrozen {
        const char *id;  // the language, without the _tone suffix: "mandarin" / "cantonese"
        size_t total;  // positions whose character the dictionary reports as polyphonic
        size_t exact;  // engine reading equals the gold cell (or a member of an "a/b" cell)
        size_t syllable;  // the same after dropping a trailing tone digit
    };

    const PolyFrozen kPolyFrozen[] = {
        // language,  polyphone positions, exact, syllable
        {"mandarin", 107230, 103502, 105754},
        {"cantonese", 40639, 39710, 40366},
    };

    // Re-frozen after the Mandarin phrase work (round 12): PHRASE_OVERRIDES_MANDARIN (86 corrected entries) plus
    // APPEND_NEW_READINGS (the 6 readings those corrections removed). A phrase entry decides the reading of EVERY
    // character it spans, so it can hold an error no single-character change reaches -- `勃艮第` alone cost 168
    // polyphone positions. The 107230 denominator is unchanged on purpose: the corrections had left six characters
    // with ONE reading, cutting it to 105833 and making the percentage RISE while the absolute count FELL -- a number
    // that improves because the measurement shrank is a change of ruler, not an improvement. APPEND beats PREPEND
    // (96.59% vs 95.56%, prepending costs 699 tone rows): with no phrase match the engine reads the FIRST reading.
    // The 87th entry `大夫:dà fū` was excluded for 19 rows and 68 exact positions, the corpus holding only one of its
    // two correct readings -- the audit proposes, the golden sentences dispose. The Python front ends in .tmp/scratch
    // approximate this and are NOT the gate (they reported 1029 for mandarin_untone against this suite's 1028).
    const FrozenCounts kFrozen[] = {
        // id, decided, pass, ALT_OK, DATA_FIX, ENGINE_BUG, DICT_GAP, EXCLUDED, verdict fingerprint
        {"mandarin_tone", 9391, 6339, 636, 1006, 4894, 1434, 0, 0x73dabe15cec3ce3aull},
        {"mandarin_untone", 1138, 1029, 9, 19, 73, 2, 0, 0xdb4de16e2c53ee87ull},
        {"cantonese_tone", 6352, 5358, 524, 1613, 1172, 236, 2, 0x8e16cf719fda14a2ull},
        {"cantonese_untone", 6352, 6009, 221, 854, 454, 131, 2, 0xf279fcc768a302d1ull},
    };

    std::vector<std::string> readLines(const std::filesystem::path &path) {
        std::vector<std::string> lines;
        std::ifstream file(path);
        if (!file.is_open())
            return lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
        std::vector<std::string> tokens;
        if (delimiter.empty()) {
            for (char c : s)
                tokens.emplace_back(1, c);
            return tokens;
        }
        size_t start = 0, end = s.find(delimiter);
        while (end != std::string::npos) {
            tokens.push_back(s.substr(start, end - start));
            start = end + delimiter.size();
            end = s.find(delimiter, start);
        }
        tokens.push_back(s.substr(start));
        return tokens;
    }

    // The header is one JSON line; this test needs three fields of it and deliberately avoids
    // pulling a JSON library into the suite. Whitespace after the colon and \\-escapes are handled
    // because the header is written by json.dumps in the tooling.
    std::string metaField(const std::string &header, const std::string &key) {
        const std::string needle = "\"" + key + "\":";
        size_t p = header.find(needle);
        if (p == std::string::npos)
            return {};
        p += needle.size();
        while (p < header.size() && (header[p] == ' ' || header[p] == '\t'))
            p++;
        if (p < header.size() && header[p] == '"') {
            std::string out;
            for (size_t e = p + 1; e < header.size(); e++) {
                if (header[e] == '\\' && e + 1 < header.size()) {
                    out.push_back(header[e + 1]);
                    e++;
                    continue;
                }
                if (header[e] == '"')
                    break;
                out.push_back(header[e]);
            }
            return out;
        }
        size_t e = header.find_first_of(",}", p);
        if (e == std::string::npos)
            e = header.size();
        return header.substr(p, e - p);
    }

    struct Tally {
        size_t decided = 0, pass = 0, altOk = 0, dataFix = 0, engineBug = 0, dictGap = 0, excluded = 0;
        size_t posChecked = 0, posWrong = 0, countMismatch = 0;
        // Polyphone accuracy (see kPolyFrozen). Only counted for positions whose character the dictionary itself
        // reports as polyphonic AND whose gold cell is a real reading; a hanzi gold cell means the corpus never
        // decided, so it is not evidence about the engine and would otherwise sit in the denominator forever.
        size_t polyTotal = 0, polyExact = 0, polySyllable = 0;
        size_t charSplitFailures = 0;
        uint64_t verdictFnv = kFnvOffset;
        std::vector<std::string> examples;
        std::vector<std::string> polyExamples;

        // Fold one row's verdict into the fingerprint. Called for every data row, in file order,
        // including the ones that take part in no statistic.
        void fingerprint(const std::string &id, const std::string &status, char verdict) {
            verdictFnv = fnvUpdate(verdictFnv, id + ":" + status + ":" + verdict + "\n");
        }
    };

    // The position index counts CHARACTERS, not bytes: the engine emits one entry per character and the corpus's
    // gold cells are per character too, but the text is UTF-8, so a naive text[i] would desynchronise the moment a
    // sentence contains a character outside ASCII. Every position here starts a code point because the corpus text
    // is valid UTF-8, so scanning for non-continuation bytes is enough -- this does not need a full decoder.
    std::string charAt(const std::string &text, size_t index) {
        size_t seen = 0;
        for (size_t i = 0; i < text.size();) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = 1;
            if ((c & 0x80) == 0)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            if (seen == index)
                return text.substr(i, len);
            seen++;
            i += len;
        }
        return {};
    }

    // Drop a trailing tone digit, so that "dì" vs "di4" is not what this comparison is about. A NORMAL-style run
    // has no tone digit to drop and comes through unchanged, which is why the same helper serves both corpora.
    std::string withoutTone(const std::string &cell) {
        if (!cell.empty() && cell.back() >= '0' && cell.back() <= '9')
            return cell.substr(0, cell.size() - 1);
        return cell;
    }

    // A verified cell is either a syllable in this dialect's notation, or a character/token the engine echoes back
    // instead of reading (which is a defect, never a pass). Syllable bodies are ASCII letters PLUS non-ASCII bytes,
    // because notations need letters outside [a-z] (Teochew peng'im /ɛ/ as ê makes `zêg8` one syllable); requiring
    // one ASCII letter is what rejects the echo case. All Mandarin/Cantonese cells are pure ASCII, so this predicate
    // returns exactly what it returned before -- the frozen counts and fingerprints are the control for that.
    bool isSyllable(const std::string &cell, bool tone3) {
        if (cell.empty())
            return false;
        size_t i = 0;
        bool sawAsciiLetter = false;
        for (; i < cell.size(); i++) {
            const unsigned char c = static_cast<unsigned char>(cell[i]);
            if (c >= 'a' && c <= 'z') {
                sawAsciiLetter = true;
                continue;
            }
            if (c >= 0x80)
                continue;  // non-ASCII: an extended letter in the notation
            break;
        }
        if (!sawAsciiLetter)
            return false;  // no ASCII letter: not a syllable
        if (i == cell.size())
            return !tone3;  // NORMAL style: no tone digit
        if (i + 1 != cell.size())
            return false;  // trailing junk
        const char t = cell[i];
        if (t < '1' || t > (tone3 ? '6' : '5'))
            return false;
        return true;
    }

    bool isHanzi(const std::string &tok) {
        if (tok.size() < 3)
            return false;  // at least one 3-byte UTF-8 sequence
        const unsigned char b0 = static_cast<unsigned char>(tok[0]);
        const unsigned char b1 = static_cast<unsigned char>(tok[1]);
        if (b0 < 0xE0)
            return false;
        unsigned cp = 0;
        if (b0 < 0xF0)
            cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6);
        else if (b0 < 0xF8)
            cp = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12);
        else
            return false;
        if (tok.size() < 3)
            return false;
        const unsigned char b2 = static_cast<unsigned char>(tok[2]);
        cp |= (b2 & 0x3Fu);
        return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF);
    }

    void runSet(const SetSpec &spec, cpp_pinyin::v2::Engine &man, cpp_pinyin::v2::Engine &can, Tally &t) {
        const std::filesystem::path path = std::filesystem::path("verified") / spec.file;
        INFO("dataset " << spec.id);
        REQUIRE(std::filesystem::exists(path));
        auto lines = readLines(path);
        REQUIRE_FALSE(lines.empty());
        REQUIRE(lines[0].rfind("#!meta ", 0) == 0);  // missing header = FAIL (D16 rule 4)
        REQUIRE(metaField(lines[0], "datasetId") == spec.id);
        REQUIRE(metaField(lines[0], "source").size() > 0);  // provenance must be recorded
        REQUIRE(metaField(lines[0], "license").size() > 0);

        cpp_pinyin::v2::EngineConfig mcfg;
        mcfg.style = spec.tone3 ? cpp_pinyin::v2::MandarinStyle::TONE3 : cpp_pinyin::v2::MandarinStyle::NORMAL;
        mcfg.markNeutralAs5 = true;
        mcfg.includeCandidates = false;
        cpp_pinyin::v2::EngineConfig ccfg;
        ccfg.style = spec.tone3 ? cpp_pinyin::v2::CantoneseStyle::TONE3 : cpp_pinyin::v2::CantoneseStyle::NORMAL;
        ccfg.includeCandidates = false;

        size_t rows = 0;
        for (size_t li = 1; li < lines.size(); li++) {
            const std::string &line = lines[li];
            if (line.empty())
                continue;
            auto parts = split(line, "\t");
            REQUIRE(parts.size() == 4);
            rows++;
            const std::string &sent = parts[0];
            auto expected = split(parts[1], " ");
            const std::string &status = parts[2];
            const std::string rid = rowId(parts[3]);

            auto res =
                spec.cantonese ? can.query(sent, ccfg).value().toVector() : man.query(sent, mcfg).value().toVector();
            if (res.size() != expected.size()) {
                // A count disagreement is never silently absorbed (D16 rule 2): the row is unusable
                // as a verified row and must be EXCLUDED, not partially compared.
                t.countMismatch++;
                if (t.examples.size() < 8)
                    t.examples.push_back("count " + std::to_string(expected.size()) + " vs " +
                                         std::to_string(res.size()) + "  " + sent.substr(0, 20));
                t.fingerprint(rid, status, 'X');
                REQUIRE(status == "EXCLUDED");
                t.excluded++;
                continue;
            }
            if (status == "EXCLUDED") {
                // Excluded for a content defect of the corpus itself (a typo for another character,
                // a missing syllable). Excluded rows take part in no statistic at all, so they are
                // skipped before any position is compared -- the audit found one such row whose
                // syllable count happens to agree, which is why this check cannot live after the
                // position loop (it used to be there, and such a row was scored on the way past).
                t.fingerprint(rid, status, 'X');
                t.excluded++;
                continue;
            }
            bool ok = true;
            for (size_t i = 0; i < expected.size(); i++) {
                auto acceptable = split(expected[i], "/");
                t.posChecked++;
                bool unreadable = false;
                for (const auto &cell : acceptable) {
                    if (isSyllable(cell, spec.tone3))
                        continue;
                    if (isHanzi(cell))
                        unreadable = true;  // no reading available: a dictionary gap
                    // punctuation / Latin / digits: the engine echoes them, so they compare normally
                }
                if (unreadable) {
                    // The engine could not read a character it was asked to read. Echoing the
                    // character back is not a pass (D16 rule 2: missing data is a failure).
                    ok = false;
                    t.posWrong++;
                    if (t.examples.size() < 8)
                        t.examples.push_back(sent.substr(0, 16) + " #" + std::to_string(i) + " unreadable " +
                                             expected[i] + " [" + status + "]");
                    continue;
                }
                if (std::find(acceptable.begin(), acceptable.end(), res[i]) == acceptable.end()) {
                    ok = false;
                    t.posWrong++;
                    if (t.examples.size() < 8)
                        t.examples.push_back(sent.substr(0, 16) + " #" + std::to_string(i) + " want " + expected[i] +
                                             " got " + res[i] + " [" + status + "]");
                }
                // ---- polyphone accuracy, the project's standard figure ----
                // Only the tone corpus is counted: with NORMAL style the engine is not emitting tones at all, so
                // an "exact" comparison there would grade it on something it was never asked to produce.
                if (!spec.tone3)
                    continue;
                const std::string ch = charAt(sent, i);
                // A position that does not resolve to exactly one character would silently shift which character
                // the polyphone test is applied to for every later position in the row. That is the shape of a
                // whole family of bugs already hit here (a dropped tone made all 1138 untone rows read as wrong; a
                // mis-set style made a corpus score 0). Counted rather than asserted per position: a REQUIRE here
                // would add one assertion per polyphone (about 400000 across the corpora) and would report the
                // first failure with no indication of how widespread it is. The counter is checked once, after the
                // loop, where the total is actually meaningful.
                if (ch.empty()) {
                    t.charSplitFailures++;
                    continue;
                }
                // NOT isPolyphonic(): despite the name it does not mean "has several readings". It tests
                // m_phrasesMapBits -- "may a phrase start at this character" -- which is a lookup gate and is
                // deliberately permissive (the shipped map reports 234 for 儿, which starts no phrase at all).
                // Using it inflated the denominator from 107230 to 114119 and silently changed what was measured.
                // readings() answers the actual question this figure is about.
                const auto readings = spec.cantonese ? can.readings(ch).value() : man.readings(ch).value();
                if (readings.size() <= 1)
                    continue;
                t.polyTotal++;
                if (std::find(acceptable.begin(), acceptable.end(), res[i]) != acceptable.end()) {
                    t.polyExact++;
                    t.polySyllable++;  // an exact match is also a syllable match, by definition
                    continue;
                }
                const std::string gotSyl = withoutTone(res[i]);
                for (const auto &cell : acceptable) {
                    if (withoutTone(cell) != gotSyl)
                        continue;
                    t.polySyllable++;
                    if (t.polyExamples.size() < 8)
                        t.polyExamples.push_back(ch + " tone-only: want " + expected[i] + " got " + res[i] + " [" +
                                                 status + "]");
                    break;
                }
            }
            if (status == "ENGINE_BUG")
                t.engineBug++;
            else if (status == "DICT_GAP")
                t.dictGap++;
            else if (status == "ALT_OK")
                t.altOk++;
            else if (status == "DATA_FIX")
                t.dataFix++;
            else
                REQUIRE(status == "OK");
            t.fingerprint(rid, status, ok ? 'P' : 'F');
            // decided = every reviewed row with a verified reading: the rows the engine got wrong are
            // decided too, they are simply not passes (D16, docs/DECISIONS.md section 4.5).
            t.decided++;
            if (ok)
                t.pass++;
        }
        CHECK(rows > 0);
    }

    void report(const SetSpec &spec, const Tally &t) {
        const double strict = t.decided ? 100.0 * t.pass / t.decided : 0.0;
        const size_t algoDen = t.decided > t.dictGap ? t.decided - t.dictGap : 0;
        const double algo = algoDen ? 100.0 * t.pass / algoDen : 0.0;
        char digest[32];
        std::snprintf(digest, sizeof(digest), "0x%016llx", static_cast<unsigned long long>(t.verdictFnv));
        std::cout << "  verified[" << spec.id << "]: strict " << std::fixed << std::setprecision(2) << strict << "%  ("
                  << t.pass << "/" << t.decided << ")"
                  << ", algo " << algo << "% (" << algoDen << ")"
                  << ", ALT_OK=" << t.altOk << ", DATA_FIX=" << t.dataFix << ", ENGINE_BUG=" << t.engineBug
                  << ", DICT_GAP=" << t.dictGap << ", EXCLUDED=" << t.excluded << ", positions=" << t.posChecked
                  << " wrong=" << t.posWrong << ", verdict_fnv=" << digest << std::endl;
        for (const auto &e : t.examples)
            std::cout << "      " << e << std::endl;
    }

    const FrozenCounts *frozenFor(const char *id) {
        for (const auto &f : kFrozen)
            if (std::string(f.id) == id)
                return &f;
        return nullptr;
    }

    // "mandarin_tone" -> "mandarin". The polyphone table is keyed by language because that is the unit the figure
    // is meant to be read in; the corpus id is internal bookkeeping.
    std::string languageOf(const char *id) {
        std::string s(id);
        const size_t pos = s.find('_');
        return pos == std::string::npos ? s : s.substr(0, pos);
    }

    const PolyFrozen *polyFrozenFor(const std::string &lang) {
        for (const auto &f : kPolyFrozen)
            if (lang == f.id)
                return &f;
        return nullptr;
    }

    void reportPoly(const std::string &lang, const Tally &t) {
        const double ex = t.polyTotal ? 100.0 * t.polyExact / t.polyTotal : 0.0;
        const double sy = t.polyTotal ? 100.0 * t.polySyllable / t.polyTotal : 0.0;
        std::cout << "  POLYPHONE[" << lang << "]: exact " << std::fixed << std::setprecision(2) << ex << "%  ("
                  << t.polyExact << "/" << t.polyTotal << "), syllable " << sy << "% (" << t.polySyllable
                  << "), tone-only mistakes=" << (t.polySyllable - t.polyExact) << std::endl;
        for (const auto &e : t.polyExamples)
            std::cout << "      " << e << std::endl;
    }

}  // namespace

TEST_CASE("Verified accuracy: strict per-position judging on the verified set", "[corpus][accuracy]") {
    cpp_pinyin::v2::Engine man = cpp_test::mandarin();
    cpp_pinyin::v2::Engine can = cpp_test::cantonese();
    // The engine is opened with .value(), which throws if it fails -- an "is it ready" assertion
    // here would be vacuous.

    const SetSpec specs[] = {
        {"mandarin_tone.txt", "mandarin_tone", false, true},
        {"mandarin_untone.txt", "mandarin_untone", false, false},
        {"cantonese_tone.txt", "cantonese_tone", true, true},
        {"cantonese_untone.txt", "cantonese_untone", true, false},
    };
    for (const auto &spec : specs) {
        Tally t;
        runSet(spec, man, can, t);
        report(spec, t);
        const FrozenCounts *f = frozenFor(spec.id);
        REQUIRE(f != nullptr);
        // Exact counts on purpose: a threshold would let a regression hide under it, and these
        // numbers are the whole point of the exercise (the D16 discipline, docs/DECISIONS.md section 4.5).
        CHECK(t.decided == f->decided);
        CHECK(t.pass == f->pass);
        CHECK(t.altOk == f->altOk);
        CHECK(t.dataFix == f->dataFix);
        CHECK(t.engineBug == f->engineBug);
        CHECK(t.dictGap == f->dictGap);
        CHECK(t.excluded == f->excluded);
        CHECK(t.countMismatch == 0);
        // The fingerprint is what makes the counts above a statement about ROWS rather than about
        // totals: without it, one row stranding the counts as another leaves could pass unnoticed.
        CHECK(t.verdictFnv == f->verdictFnv);

        // ---- polyphone accuracy, the project's standard figure ----
        // Only the tone corpora carry it: with NORMAL style the engine emits no tones, so grading it there
        // would be grading it on something it was never asked to produce.
        if (spec.tone3) {
            reportPoly(languageOf(spec.id), t);
            const PolyFrozen *pf = polyFrozenFor(languageOf(spec.id));
            REQUIRE(pf != nullptr);
            // Exact counts again, for the same reason as kFrozen[]: a threshold would hide a regression
            // underneath it, and these numbers are the point of the exercise. No tolerance is allowed here
            // because a change that lowers polyphone accuracy is by definition the thing being guarded
            // against -- the row counts above are what carry any accepted trade.
            CHECK(t.polyTotal == pf->total);
            CHECK(t.polyExact == pf->exact);
            CHECK(t.polySyllable == pf->syllable);
            // The character-position invariant the count above depends on. Zero on every corpus.
            CHECK(t.charSplitFailures == 0);
        } else {
            // Guards the branch itself: if the tone3 flag ever stopped matching the corpus names, the figure
            // would silently become 0/0 and every CHECK above would be skipped rather than failing.
            CHECK(t.polyTotal == 0);
        }
    }
}
