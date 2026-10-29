// Proof that the tone model survives a dialect with MORE tones than either built-in has.
//
// A claim that had been argued from the code but never executed: nothing in the suite registered a dialect
// with a tone domain wider than Cantonese's six. This file is that execution.
//
// WHAT IS SYNTHESISED, and why here rather than under tests/data:
//   * a dialect whose converter accepts tones 1..7 and declares two styles (normal, and "digit" as its default);
//   * a dictionary written into the temp directory at run time -- four files, three records each. Nothing
//     third-language enters `data/dialects/`, so `DialectSetGate` and the cross-repository drift check are
//     untouched, and the fixture cannot rot into a real dialect by accident.
//
// WHY THE C ABI IS NOT PART OF THIS: `cppp2_open` takes a `const cppp2_registry *` but the C surface has no
// constructor for one, so a C caller can only pass NULL (the two built-ins). A custom dialect is reachable from
// C++ only. What the C face can be held to is the SHAPE of the reading it accepts, and that is pinned where the
// dialect can host it: tests/cpp/test_c_api.cpp converts a Cantonese tone-6 reading, which the narrow `0..5`
// range used to refuse.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using cpp_pinyin::v2::Dialect;
    using cpp_pinyin::v2::Engine;
    using cpp_pinyin::v2::EngineConfig;
    using cpp_pinyin::v2::StyleDecl;
    using cpp_pinyin::v2::ToneConverter;

    enum StyleNumbers : int {
        STYLE_PLAIN = 0,  // letters only, no tone
        STYLE_DIGIT = 1,  // the stored form: letters + one tone digit
    };

    /// A tone domain of SEVEN. Both slots merely reshape the reading, which is all a tone style has to do: the
    /// interesting part is that tone 7 survives the whole pipeline (`readings` reports it, `query` renders it,
    /// and nothing rejects the digit on the way).
    class SevenTone final : public ToneConverter {
    public:
        SevenTone() {
            m_converts[STYLE_PLAIN] = strip;
            m_converts[STYLE_DIGIT] = keep;
        }

    private:
        static std::u16string strip(const std::u16string &pinyin, bool, bool) {
            std::u16string out = pinyin;
            if (!out.empty() && out.back() >= u'1' && out.back() <= u'9')
                out.pop_back();
            return out;
        }
        static std::u16string keep(const std::u16string &pinyin, bool, bool) { return pinyin; }
    };

    /// Four files, each with at least one parseable record: `EngineImpl::init` treats an empty file as a load
    /// failure. `字:ta7,ta1` is the point -- a character whose SECOND reading is tone 7, so the fixture cannot
    /// pass by accident on a domain that stops at 6.
    fs::path writeSevenToneDict() {
        const fs::path root = fs::temp_directory_path() / "cpp_pinyin_seven_tone";
        const fs::path dir = root / "seven";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(dir);

        const auto put = [&dir](const char *name, const char *body)
        {
            std::ofstream out(dir / name, std::ios::binary);
            REQUIRE(out.good());
            out << body;
        };
        put("word.txt", u8"他:ta1,ta7\n");
        put("phrases_dict.txt", u8"他们:ta1,men2\n");
        put("phrases_map.txt", u8"他:2\n");
        put("trans_word.txt", u8"祂:他\n");
        return root;
    }

    Dialect sevenToneDialect() {
        Dialect dialect;
        dialect.id = "seven";
        dialect.dictSubDir = "seven";
        dialect.tone = SevenTone{};
        dialect.styles = {
            {"plain", STYLE_PLAIN, false},
            // The DIGIT style is the default: a dialect whose stored form is the readable one, like Cantonese.
            {"digit", STYLE_DIGIT, true},
        };
        return dialect;
    }
}  // namespace

TEST_CASE("A dialect with seven tones flows through the whole pipeline", "[dialect][tone]") {
    const fs::path root = writeSevenToneDict();
    Engine engine(sevenToneDialect(), root);

    // 1. STRUCTURED READING: tone 7 must arrive as 7 on the row, not clamped, not dropped, not 0.
    //    `Engine::readings()` answers RENDERED strings, so the structured side is read from the row itself
    //    with `includeReading` on -- that field is the one a binding reports as `cppp2_syllable.tone`.
    EngineConfig cfgR;
    cfgR.style = STYLE_DIGIT;
    cfgR.includeReading = true;
    const auto readRows = engine.query(u8"他", cfgR).value();
    REQUIRE(readRows.size() == 1);
    CHECK(readRows[0].reading.tone == 1);  // the first-stored reading is tone 1

    //    The tone SEVEN reading has to be reachable too: `readings()` lists every reading of the character, and
    //    one of them spells the digit that a six-tone domain could not have carried.
    const auto all = engine.readings(u8"他").value();
    REQUIRE(all.size() >= 1);
    bool sawSeven = false;
    for (const auto &spelling : all)
        sawSeven = sawSeven || spelling.find('7') != std::string::npos;
    CHECK(sawSeven);

    // 2. QUERY renders the dialect's DECLARED default, which is the digit style.
    EngineConfig cfg;
    cfg.style = STYLE_DIGIT;
    const auto result = engine.query(u8"他", cfg).value();
    REQUIRE(result.size() == 1);
    CHECK(result[0].rendered == u8"ta1");  // the first-stored reading, tone 1

    // 3. The PLAIN style strips the digit, proving the style slot is actually consulted.
    cfg.style = STYLE_PLAIN;
    CHECK(engine.query(u8"他", cfg).value()[0].rendered == u8"ta");

    // 4. A style this dialect does not declare passes the reading through unchanged rather than erroring.
    cfg.style = 42;
    CHECK(engine.query(u8"他", cfg).value()[0].rendered == u8"ta1");
}

TEST_CASE("The seven-tone dialect's style table is what it declared", "[dialect][tone]") {
    const fs::path root = writeSevenToneDict();
    const Dialect dialect = sevenToneDialect();

    REQUIRE(dialect.styles.size() == 2);
    CHECK(dialect.styles[0].alias == STYLE_PLAIN);
    CHECK_FALSE(dialect.styles[0].isDefault);
    CHECK(dialect.styles[1].alias == STYLE_DIGIT);
    CHECK(dialect.styles[1].isDefault);

    // `defaultStyleOf` -- the ONE place the -1 sentinel becomes a number -- must answer this dialect's default,
    // not Mandarin's. This is the same resolver `cppp2_tone_convert` now calls; before that fix the sentinel
    // reached the converter unresolved and the C entry returned the caller's string with CPPP2_OK.
    CHECK(cpp_pinyin::v2::defaultStyleOf(dialect.styles) == STYLE_DIGIT);

    // An unused Engine is still opened here so the fixture stays honest about the dict it writes: if the files
    // stop parsing, this test fails rather than quietly passing on a table nobody loaded.
    Engine engine(dialect, root);
    CHECK(engine.dialect().id == "seven");
}
