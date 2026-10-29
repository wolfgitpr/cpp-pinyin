// S5: the structured syllable's producer and its renderer, checked against the shipped data.
// `include/cpp-pinyin2/core/Syllable.h` states the contract (tone from the table's own notation; both neutral-tone
// spellings one surface; Cantonese stays 1..6) but declares no producer; `src/toneUtil/ReadingCodec.{h,cpp}`
// is that producer plus the renderer, and this suite holds the pair to the data: the producer on the
// contract's shapes; the renderer byte for byte against `ManTone`/`CanTone` (every style, v_to_u and
// both neutral options); a sweep of every reading of every shipped table (五 tables, 69 865 cells)
// naming the non-invertible readings so a new one fails here. The codec is library-internal, its
// symbols hidden in shared builds, so this target compiles its TU directly (DictSelfCheck's idiom);
// `tests/CMakeLists.txt` holds the easy-to-lose half: source list, include dirs, `ManToneUtil.cpp`.

#include <catch2/catch_test_macros.hpp>
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cpp-pinyin2/core/Syllable.h>
#include <cpp-pinyin2/core/U16Str.h>
#include <cpp-pinyin2/dialects/CanTone.h>
#include <cpp-pinyin2/dialects/ManTone.h>

#include "toneUtil/ReadingCodec.h"

using cpp_pinyin::v2::CantoneseStyle;
using cpp_pinyin::v2::MandarinStyle;
using cpp_pinyin::v2::ReadingSource;
using cpp_pinyin::v2::Syllable;
using cpp_pinyin::v2::ToneKind;
using cpp_pinyin::v2::detail::ToneNotation;

namespace
{

    const ToneNotation kMandarin = ToneNotation::toneMarked;
    const ToneNotation kCantonese = ToneNotation::trailingDigit;

    Syllable parse(const char16_t *reading, ToneNotation notation, ReadingSource source = ReadingSource::dictionary) {
        return cpp_pinyin::v2::detail::parseReading(reading, notation, source);
    }

    /// One `key: a,b`-style table, read the way the loader reads it. `sep` differs by file:
    /// `word.txt`/`phrases_dict.txt` are comma separated, the Mandarin `user_dict.txt` is space
    /// separated (one entry per *word*, not per character).
    std::vector<std::pair<std::string, std::string>> readTable(const std::filesystem::path &path, char sep) {
        std::vector<std::pair<std::string, std::string>> out;
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.is_open());
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            const std::string key = line.substr(0, colon);
            std::istringstream cells(line.substr(colon + 1));
            std::string cell;
            while (std::getline(cells, cell, sep)) {
                const auto first = cell.find_first_not_of(" \t");
                if (first == std::string::npos)
                    continue;
                const auto last = cell.find_last_not_of(" \t");
                out.emplace_back(key, cell.substr(first, last - first + 1));
            }
        }
        return out;
    }

    struct SweepResult {
        std::size_t readings = 0;
        std::size_t combinations = 0;
        std::set<std::string> notInvertible;
        std::vector<std::string> mismatches;  // first few, for the failure message
    };

    /// Every reading x every style x flag pair, rendered twice: by the codec and by the dialect's converter.
    /// A surface the structure cannot rebuild may differ only if `knownNotInvertible` names it -- exact: a
    /// reading that stops being rebuildable, and one that starts, both fail. `normalize` is the user-dictionary
    /// case (tone3 tables like `zhi4 fa2`, but the engine stores the tone-marked form via
    /// `EngineImpl::init` -> `loadUserDict` -> `ToneConverter::normalizeReading`), so the same rewriting is asked.
    void sweep(const std::filesystem::path &dictRoot, const char *dialect, const char *table, char sep,
               cpp_pinyin::v2::ToneConverter &converter, ToneNotation notation, const std::vector<int> &styles,
               bool normalize, std::set<std::string> &notInvertible, std::size_t &readings, std::size_t &combinations,
               std::vector<std::string> &mismatches) {
        const auto entries = readTable(dictRoot / dialect / table, sep);
        readings += entries.size();
        for (const auto &entry : entries) {
            const std::u16string raw = cpp_pinyin::v2::utf8strToU16str(entry.second);
            const std::u16string stored = normalize ? converter.normalizeReading(raw) : raw;
            const Syllable syllable = parse(stored.c_str(), notation);
            // Invertible = the structure rebuilds this very reading, byte for byte. That is the
            // criterion `readReading` itself applies before it trusts the structure, so a reading that
            // fails it is one the query path renders from its own string.
            const bool invertible = cpp_pinyin::v2::detail::surfaceOf(syllable, notation) == stored;
            if (!invertible)
                notInvertible.insert(entry.second);

            for (const int style : styles) {
                for (const bool v_to_u : {false, true}) {
                    for (const bool neutral5 : {false, true}) {
                        ++combinations;
                        const std::u16string want = converter.convert(stored, style, v_to_u, neutral5);
                        const std::u16string got = cpp_pinyin::v2::detail::renderReading(syllable, notation, converter,
                                                                                         style, v_to_u, neutral5);
                        if (got == want)
                            continue;
                        if (!invertible)
                            continue;  // covered by the not-invertible set below
                        if (mismatches.size() < 8) {
                            mismatches.push_back(std::string(dialect) + "/" + table + " " + entry.second + " (" +
                                                 entry.first + ") style=" + std::to_string(style) +
                                                 " v=" + std::to_string(v_to_u) + " n5=" + std::to_string(neutral5) +
                                                 " want=" + cpp_pinyin::v2::u16strToUtf8str(want) +
                                                 " got=" + cpp_pinyin::v2::u16strToUtf8str(got));
                        }
                    }
                }
            }
        }
    }

}  // namespace

// 1. The producer
TEST_CASE("the producer reads the tone from the table's own notation", "[syllable][producer]") {
    SECTION("Mandarin: the diacritic is the tone, and it is read, not re-derived") {
        const Syllable zhong = parse(u"zhōng", kMandarin);
        CHECK(zhong.initial == "zh");
        CHECK(zhong.fin == "ong");
        CHECK(zhong.tone == 1);
        CHECK(zhong.tone_kind == ToneKind::base);
        CHECK(zhong.source == ReadingSource::dictionary);

        CHECK(parse(u"zhòng", kMandarin).tone == 4);
        CHECK(parse(u"liǎo", kMandarin).tone == 3);
        CHECK(parse(u"shí", kMandarin).tone == 2);
    }

    SECTION("Mandarin: the two neutral spellings are two entries and one surface") {
        // `de` is the table's plain form (word.txt: 640 readings carry no diacritic); `de5` is what an external
        // table writes and a caller may hand the codec. Through the engine they are one entry: `normalizeReading`
        // rewrites `de5` -> `de` (measured: tone3ToTone drops 5 and 0; shipped user_dict.txt writes `de5` on 8
        // rows), and the output's 5 is markNeutralAs5 -- the renderer's business. The digit branch keeps the
        // other half: a literal `de5` in a table's own notation is data, not a render option, so it reads as tone 5.
        const Syllable plain = parse(u"de", kMandarin);
        CHECK(plain.initial == "d");
        CHECK(plain.fin == "e");
        CHECK(plain.tone == 0);

        const Syllable five = parse(u"de5", kMandarin);
        CHECK(five.fin == "e");
        CHECK(five.tone == 5);
    }

    SECTION("Mandarin: ü is stored as v, and the zero-initial rules are the beta face's") {
        CHECK(parse(u"lüè", kMandarin).fin == "ve");
        CHECK(parse(u"lǜ", kMandarin).fin == "v");
        CHECK(parse(u"nǚ", kMandarin).tone == 3);
        CHECK(parse(u"guī", kMandarin).fin == "uei");  // the canonical final, not the spelling

        // 零声母: y/w are not initials (「阿」-> ""、「衣」-> ""、「知」-> "zh").
        const Syllable yi = parse(u"yī", kMandarin);
        CHECK(yi.initial.empty());
        CHECK(yi.fin == "i");
        CHECK(parse(u"yuè", kMandarin).fin == "ve");
        CHECK(parse(u"wén", kMandarin).fin == "uen");
        CHECK(parse(u"ā", kMandarin).initial.empty());
        CHECK(parse(u"zhī", kMandarin).initial == "zh");
    }

    SECTION("Cantonese: 1..6 straight from the data, entering tones add no phase") {
        for (int tone = 1; tone <= 6; ++tone) {
            const auto raw = std::u16string(u"zung") + static_cast<char16_t>(u'0' + tone);
            const Syllable syllable = parse(raw.c_str(), kCantonese);
            INFO("Cantonese tone " << tone);
            CHECK(syllable.initial == "z");
            CHECK(syllable.fin == "ung");
            CHECK(syllable.tone == tone);
            CHECK(syllable.tone_kind == ToneKind::base);
        }
        // An entering tone is a 1/3/6 reading with a -p/-t/-k coda, not a 7/8/9 phase.
        const Syllable aat = parse(u"aat3", kCantonese);
        CHECK(aat.initial.empty());  // 壓 aat3: a zero-initial syllable
        CHECK(aat.fin == "aat");
        CHECK(aat.tone == 3);
        const Syllable cin = parse(u"cin4", kCantonese);
        CHECK(cin.initial == "c");
        CHECK(cin.fin == "in");
        // ng- and m- initials are parts of the syllable's own inventory, not extra tones.
        CHECK(parse(u"ngaa5", kCantonese).initial == "ng");
        CHECK(parse(u"m4", kCantonese).initial == "m");
        CHECK(parse(u"m4", kCantonese).fin.empty());
    }

    SECTION("an empty reading describes nothing") {
        const Syllable nothing = parse(u"", kMandarin);
        CHECK(nothing.tone == 0);
        CHECK(nothing.tone_kind == ToneKind::unspecified);
        CHECK(nothing.initial.empty());
        CHECK(nothing.fin.empty());
    }
}

// 2. The renderer, against the dialects' own converters
TEST_CASE("the renderer reproduces the converter's own bytes", "[syllable][renderer]") {
    cpp_pinyin::v2::ManTone man;
    cpp_pinyin::v2::CanTone can;

    // Readings chosen for the shapes the inverse has to undo: the iou/uei/uen contraction, the y/w
    // rewrite, a ü the tone mark does not land on, both neutral spellings, and a phrase-table
    // apostrophe-free multi-syllable form.
    const char16_t *const mandarin[] = {u"guī", u"guì",  u"zhōng", u"liǎo", u"jiǔ", u"wén", u"wǔ",
                                        u"yī",  u"yīng", u"yǒu",   u"yuè",  u"yùn", u"lǜ",  u"lüè",
                                        u"nǚ",  u"ē",    u"ér",    u"de",   u"de5", u"zhi", u"ê"};
    const char16_t *const cantonese[] = {u"zung1", u"zung2",  u"zung3", u"zung4", u"zung5", u"zung6",
                                         u"aat3",  u"gwong2", u"ngaa5", u"m4",    u"jyu4",  u"syu2"};

    SECTION("Mandarin: nine styles x v_to_u x markNeutralAs5") {
        for (const char16_t *raw : mandarin) {
            const Syllable syllable = parse(raw, kMandarin);
            for (int style = 0; style < cpp_pinyin::v2::ToneConverter::MAX_STYLE; ++style) {
                for (const bool v_to_u : {false, true}) {
                    for (const bool neutral5 : {false, true}) {
                        const auto want = man.convert(raw, style, v_to_u, neutral5);
                        const auto got =
                            cpp_pinyin::v2::detail::renderReading(syllable, kMandarin, man, style, v_to_u, neutral5);
                        INFO("raw=" << cpp_pinyin::v2::u16strToUtf8str(raw) << " style=" << style << " v=" << v_to_u
                                    << " n5=" << neutral5);
                        // A reading the structure cannot rebuild renders as nothing here; the query
                        // path keeps its own string for those, which is what the sweep below pins.
                        if (got.empty() && !want.empty())
                            CHECK(cpp_pinyin::v2::detail::surfaceOf(syllable, kMandarin).empty());
                        else
                            CHECK(got == want);
                    }
                }
            }
        }
    }

    SECTION("Cantonese: three styles, tone digit included") {
        for (const char16_t *raw : cantonese) {
            const Syllable syllable = parse(raw, kCantonese);
            for (const auto style : {CantoneseStyle::NORMAL, CantoneseStyle::FIRST_LETTER, CantoneseStyle::TONE3}) {
                for (const bool neutral5 : {false, true}) {
                    const auto want = can.convert(raw, static_cast<int>(style), false, neutral5);
                    const auto got = cpp_pinyin::v2::detail::renderReading(syllable, kCantonese, can,
                                                                           static_cast<int>(style), false, neutral5);
                    INFO("raw=" << cpp_pinyin::v2::u16strToUtf8str(raw) << " style=" << static_cast<int>(style));
                    CHECK(got == want);
                }
            }
        }
    }
}

// 3. The sweep: every reading of every shipped table
TEST_CASE("every shipped reading renders back to the same bytes", "[syllable][corpus]") {
    const std::filesystem::path root = std::filesystem::current_path() / "dict";
    cpp_pinyin::v2::ManTone man;
    cpp_pinyin::v2::CanTone can;

    // Derived from the engines' own `Dialect::styles` (the executable authority: `src/Mandarin.cpp`/
    // `src/Cantonese.cpp` build it from `ManTone::Style`/`CantoneseStyle`), not by hand -- a hand-copied
    // list would stay green while a *new* style was added to the dialect and this table, the sweep
    // covering less. Red proof 2026-10: dropping one entry there is caught by
    // tests/cpp/test_style_table.cpp:70, NOT here (floors readings > 65000 are too coarse for one style).
    const cpp_pinyin::v2::Registry registry;
    const auto stylesOf = [&registry, &root](const char *id)
    {
        const cpp_pinyin::v2::Engine engine = cpp_pinyin::v2::Engine::open(registry, root, id).value();
        std::vector<int> aliases;
        for (const cpp_pinyin::v2::StyleDecl &decl : engine.dialect().styles)
            aliases.push_back(decl.alias);
        REQUIRE(!aliases.empty());  // a dialect with no styles would make the sweep vacuous
        return aliases;
    };
    const std::vector<int> mandarinStyles = stylesOf("mandarin");
    const std::vector<int> cantoneseStyles = stylesOf("cantonese");

    std::set<std::string> notInvertible;
    std::vector<std::string> mismatches;
    std::size_t readings = 0, combinations = 0;

    sweep(root, "mandarin", "word.txt", ',', man, kMandarin, mandarinStyles, false, notInvertible, readings,
          combinations, mismatches);
    sweep(root, "mandarin", "phrases_dict.txt", ',', man, kMandarin, mandarinStyles, false, notInvertible, readings,
          combinations, mismatches);
    sweep(root, "mandarin", "user_dict.txt", ' ', man, kMandarin, mandarinStyles, true, notInvertible, readings,
          combinations, mismatches);
    sweep(root, "cantonese", "word.txt", ',', can, kCantonese, cantoneseStyles, false, notInvertible, readings,
          combinations, mismatches);
    sweep(root, "cantonese", "phrases_dict.txt", ',', can, kCantonese, cantoneseStyles, false, notInvertible, readings,
          combinations, mismatches);

    // A sweep that loaded nothing would pass every check above, so the load itself is a requirement.
    // (Measured 2026-10-04, reading *cells*: 18 722 + 17 632 + 248 + 29 941 + 3 322 = 69 865. The
    // Mandarin user_dict contributes 248 readings over 101 entries.) The floor is deliberately coarse
    // -- it is here to catch an empty or truncated table, not to freeze the data.
    INFO("readings swept = " << readings << ", combinations = " << combinations);
    for (const auto &m : mismatches)
        INFO("mismatch: " << m);
    REQUIRE(readings > 65000);
    REQUIRE(combinations > 1000000);
    CHECK(mismatches.empty());

    // The readings whose (initial, fin) projection is not invertible, exactly. Asked for one by one
    // rather than counted, so the failure message names the reading:
    //   ǹ / ḿ      -- the syllabic nasals: no entry of the final table, so no surface to rebuild
    //   hm         -- a non-pinyin interjection (噷): the split finds an initial and no final
    //   o / yō, yo -- the loose retry, which can eat the y of "yo": `o` alone is 哦/噢, `yo` is 哟/唷
    const std::set<std::string> knownNotInvertible = {"hm", "\u01F9", "\u1E3F", "yo", "y\u014D", "\u00F2", "\u014D"};
    CHECK(notInvertible == knownNotInvertible);
}

// 4. The load-time form (S6)
TEST_CASE("the load-time form is what the query path renders", "[syllable][prepared]") {
    using cpp_pinyin::v2::detail::PreparedReading;
    using cpp_pinyin::v2::detail::prepareReading;
    using cpp_pinyin::v2::detail::ReadingTable;

    SECTION("an invertible reading: the surface is the structure's own output") {
        const PreparedReading prepared = prepareReading(u"gu\u012B", kMandarin, ReadingSource::phrase);
        CHECK(prepared.rebuilt);
        CHECK(prepared.surface == u"gu\u012B");  // i.e. the table's own spelling, byte for byte
        CHECK(prepared.data.initial == "g");
        CHECK(prepared.data.fin == "uei");  // the canonical final, not the spelling
        CHECK(prepared.data.tone == 1);
        CHECK(prepared.data.source == ReadingSource::phrase);
    }

    SECTION("a reading whose projection loses information keeps the table's spelling") {
        // `yo` is one of the seven the corpus sweep above names. The row must still render, so the
        // fallback is the stored string itself -- which is why `surface == stored` holds for every
        // reading, invertible or not, and why the query path can render without asking the codec.
        const PreparedReading prepared = prepareReading(u"yo", kMandarin, ReadingSource::dictionary);
        CHECK_FALSE(prepared.rebuilt);
        CHECK(prepared.surface == u"yo");
        CHECK(cpp_pinyin::v2::detail::surfaceOf(prepared.data, kMandarin).empty());
    }

    SECTION("the table has one entry per (reading, source), and interning is by reference") {
        ReadingTable table(kMandarin);
        const PreparedReading &first = table.prepare(u"zh\u014Dng", ReadingSource::dictionary);
        const PreparedReading &again = table.prepare(u"zh\u014Dng", ReadingSource::dictionary);
        CHECK(&first == &again);  // interned, not re-parsed
        CHECK(table.size() == 1);
        CHECK(table.rebuiltCount() == 1);
        CHECK(table.notRebuiltCount() == 0);

        // The same spelling read from the phrase table is a DIFFERENT entry: `source` is part of the
        // structure and part of the key, so the two cannot overwrite each other's provenance.
        const PreparedReading &asPhrase = table.prepare(u"zh\u014Dng", ReadingSource::phrase);
        CHECK(&asPhrase != &first);
        CHECK(table.size() == 2);
        CHECK(first.data.source == ReadingSource::dictionary);
        CHECK(asPhrase.data.source == ReadingSource::phrase);

        table.prepare(u"yo", ReadingSource::dictionary);
        CHECK(table.rebuiltCount() == 2);
        CHECK(table.notRebuiltCount() == 1);
    }
}

TEST_CASE("every shipped reading's prepared surface is the table's own spelling", "[syllable][prepared][corpus]") {
    // The engine builds this at load and then renders the stored strings directly (EngineImpl::
    // applyTone). That is only sound if, for EVERY reading of every table, the prepared surface is
    // the stored spelling -- so this walks the same five tables the sweep above walks and checks it
    // cell by cell, together with the agreement between `prepareReading` and its two halves.
    const std::filesystem::path root = std::filesystem::current_path() / "dict";
    cpp_pinyin::v2::ManTone man;
    cpp_pinyin::v2::CanTone can;

    struct Table {
        const char *dialect;
        const char *file;
        char sep;
        bool normalize;  // user dictionaries are written in tone3
        ToneNotation notation;
        cpp_pinyin::v2::ToneConverter *converter;
    };
    const Table tables[] = {
        {"mandarin", "word.txt", ',', false, kMandarin, &man},
        {"mandarin", "phrases_dict.txt", ',', false, kMandarin, &man},
        {"mandarin", "user_dict.txt", ' ', true, kMandarin, &man},
        {"cantonese", "word.txt", ',', false, kCantonese, &can},
        {"cantonese", "phrases_dict.txt", ',', false, kCantonese, &can},
    };

    std::size_t cells = 0, notRebuiltCells = 0;
    for (const Table &table : tables) {
        for (const auto &entry : readTable(root / table.dialect / table.file, table.sep)) {
            const std::u16string raw = cpp_pinyin::v2::utf8strToU16str(entry.second);
            const std::u16string stored = table.normalize ? table.converter->normalizeReading(raw) : raw;
            const cpp_pinyin::v2::detail::PreparedReading prepared =
                cpp_pinyin::v2::detail::prepareReading(stored, table.notation, ReadingSource::dictionary);
            ++cells;
            if (!prepared.rebuilt)
                ++notRebuiltCells;
            if (prepared.surface != stored) {
                INFO("surface != stored: " << table.dialect << "/" << table.file << " " << entry.second);
                CHECK(prepared.surface == stored);
            }
            // `rebuilt` must agree with the definition the whole design rests on, per cell.
            const bool invertible = cpp_pinyin::v2::detail::surfaceOf(prepared.data, table.notation) == stored;
            CHECK(prepared.rebuilt == invertible);
            CHECK(prepared.data.source == ReadingSource::dictionary);
        }
    }

    INFO("cells = " << cells << ", cells whose projection is not invertible = " << notRebuiltCells);
    REQUIRE(cells > 65000);
    // The fallback branch must be *exercised* by the data, or the sweep above proves nothing about
    // it; and it must stay a handful, or "surface == stored" would be vacuous.
    CHECK(notRebuiltCells > 0);
    CHECK(notRebuiltCells < cells / 100);
}
