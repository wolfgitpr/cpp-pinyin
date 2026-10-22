// AC-1's two halves, as one test: the six-line sample has to be real code (it compiles and runs against
// the shipped dictionaries) and it has to stay six lines -- the count in the file is what the criterion
// measures. The source is read instead of only counted by hand because the first half used to be satisfied
// by pointing at examples/mandarin_example.cpp, a 63-effective-line walkthrough: "the common path is
// <= 6 lines" then rested on a reader finding six lines inside a longer file that nothing stopped from
// growing. The six lines now live in examples/minimal_usage.cpp, numbered by hand with trailing `// 1` ..
// `// 6`, and this test fails if the count, the order or the shape changes; the file is also a CMake target
// (MinimalUsage) and a CTest entry, so "it compiles and runs" is not a claim either. The compile
// definition below is how the test finds the file: the test runs from the build tree, where a relative path
// back to the source tree would depend on the working directory.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef CPP_PINYIN_MINIMAL_USAGE_SOURCE
#error "CPP_PINYIN_MINIMAL_USAGE_SOURCE must be defined by tests/CMakeLists.txt"
#endif

namespace
{

    // The six lines, in order, as they appear after stripping the trailing `// N` marker. Matching the
    // substance (not the whitespace) keeps the test from failing over reformatting while still failing
    // when a line stops doing its job.
    //
    // S13: the table follows the 2.0 face. `Engine::open` (S11a) answers with an `Outcome`, so the
    // sample's construction is guarded by the outcome instead of by a `isReady()` check that had to be
    // repeated at the query. The same two capabilities are asserted, on the channel the API has.
    const std::array<const char *, 6> kExpectedLines = {
        "cpp_pinyin::v2::Engine::open(registry, std::filesystem::path{argv[1]}, \"mandarin\")",
        "if (!built)",
        "built.value().query(",
        "const cpp_pinyin::v2::Result result = answer.value();",
        "if (result.empty())",
        "for (const auto &item : result)",
    };

    struct Numbered {
        unsigned marker = 0;
        std::string body;
    };

    std::vector<Numbered> numberedLines(const std::filesystem::path &source) {
        std::ifstream in(source);
        REQUIRE(in.is_open());

        std::vector<Numbered> found;
        std::string line;
        while (std::getline(in, line)) {
            const auto markerAt = line.rfind("// ");
            if (markerAt == std::string::npos) {
                continue;
            }
            // The marker is `// <digit>` at the end of the line, optionally followed by prose on the
            // same line (`// 1  dialect id + dictionary root`). Five guards, each one paid for: only the
            // first character after `// ` is the marker (requiring "exactly one character" found ZERO
            // markers in a file that plainly had six); the digit must be followed by end-of-line or
            // whitespace, so `// 2026-10-03` cannot masquerade as a line marker; the digit range is 1..9,
            // NOT 1..6 -- capping it at 6 made the gate blind to the exact thing it exists to catch (a
            // seventh line was parsed, rejected by the range check and counted as "no marker", so `== 6`
            // still passed; the `// 7` negative control exposed it); LEADING WHITESPACE after `// ` is
            // skipped, since marker-aligning padding (`declaration; //` + `    call(); // 1`) otherwise
            // drops the line; and the body to the LEFT of the marker must carry non-whitespace (the S13
            // red, second round: the skip made a prose continuation look like a marker).
            const std::string digitAt = line.substr(markerAt + 3);
            const auto firstDigit = digitAt.find_first_not_of(" \t");
            if (firstDigit == std::string::npos || digitAt[firstDigit] < '1' || digitAt[firstDigit] > '9') {
                continue;
            }
            std::string afterDigit = digitAt.substr(firstDigit + 1);
            if (!afterDigit.empty() && afterDigit[0] != ' ' && afterDigit[0] != '\t') {
                continue;
            }
            std::string body = line.substr(0, markerAt);
            const auto bodyStart = body.find_first_not_of(" \t");
            if (bodyStart == std::string::npos) {
                continue;
            }
            Numbered n;
            n.marker = static_cast<unsigned>(digitAt[firstDigit] - '0');
            // Both ends are trimmed: the left because indentation is not part of the statement, the
            // right because the padding between the statement and its marker is not either.
            n.body = body.substr(bodyStart, body.find_last_not_of(" \t") - bodyStart + 1);
            found.push_back(n);
        }
        return found;
    }

}  // namespace

TEST_CASE("AC-1: the minimal sample has exactly six numbered lines, in order", "[ac1][sample][source]") {
    const auto found = numberedLines(std::filesystem::path(CPP_PINYIN_MINIMAL_USAGE_SOURCE));

    // Both the path and the count are printed on failure. The first version of this test reported
    // "0 == 6" with no indication of which file it had read, and later a range-guard bug reported
    // nothing at all while a seventh marker sat in the file -- each cost a detour to diagnose.
    INFO("source = " << CPP_PINYIN_MINIMAL_USAGE_SOURCE);
    INFO("markers found = " << found.size());
    for (const auto &n : found) {
        INFO("marker " << n.marker << ": " << n.body);
    }

    REQUIRE(found.size() == 6);
    for (size_t i = 0; i < found.size(); ++i) {
        INFO("line " << (i + 1) << ": " << found[i].body);
        CHECK(found[i].marker == i + 1);
        const std::string want = kExpectedLines[i];
        CHECK(found[i].body.find(want) != std::string::npos);
    }
}

TEST_CASE("AC-1: the minimal sample still satisfies the construct+query+iterate shape", "[ac1][sample][source]") {
    const auto found = numberedLines(std::filesystem::path(CPP_PINYIN_MINIMAL_USAGE_SOURCE));
    REQUIRE(found.size() == 6);

    // Named checks rather than only "matches the table": if someone rewrites a line, the table above
    // says it changed, and this says what capability was lost.
    INFO("line 1 must construct with the dialect id and the dictionary root");
    CHECK(found[0].body.find("\"mandarin\"") != std::string::npos);
    CHECK(found[0].body.find("std::filesystem::path{argv[1]}") != std::string::npos);
    INFO("the second line must check the outcome of `open` -- without it a dialect id that did not "
         "resolve, or a dictionary that did not load, reads as a usable engine (S11a)");
    CHECK(found[1].body.find("!built") != std::string::npos);
    INFO("line 3 must query, and must reach the engine through the outcome it just checked -- a query "
         "on an unguarded value is the check above being decorative");
    CHECK(found[2].body.find("query(") != std::string::npos);
    CHECK(found[2].body.find("built.value()") != std::string::npos);
    INFO("line 4 must unwrap the query outcome with the thing that can actually fail; S11a made a "
         "failed query an error, not an empty result, so the unwrap is where that face is exercised");
    CHECK(found[3].body.find("answer.value()") != std::string::npos);
    INFO("the fifth line must not read an empty result as success");
    CHECK(found[4].body.find("result.empty()") != std::string::npos);
    INFO("the sixth line must be the per-item loop; iterating is one of the three capabilities");
    CHECK(found[5].body.find("for (const auto &item : result)") != std::string::npos);
    // The third capability is joining, and it lives in the statement under the loop (S13). It is not a
    // numbered line of its own: the six-line budget is spent on the construct/query/iterate path, and
    // `join` is asserted here so that dropping it still fails this test. What is NOT asserted is the
    // exact statement, because the loop body sits between it and the delimiter.
    std::ifstream source(CPP_PINYIN_MINIMAL_USAGE_SOURCE);
    REQUIRE(source.is_open());
    const std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    INFO("the sample must still show `join` -- the third capability, and the third argument form");
    CHECK(text.find("result.join()") != std::string::npos);
}

TEST_CASE("AC-1: the marker parser reads the text it is given, and reports what it skips", "[ac1][sample][parser]") {
    // SELF-PROOF, not a description (S13). The parser above only READS a file, so the day it stops
    // recognizing a shape it recognizes today, the count assertion can still pass while a numbered
    // line goes missing -- a green run for the wrong reason. `required path` in this file
    // (the 2.0 plan ledger, retired in the 2026-10-09 documentation cut) is exactly that class of defect, and
    // this test is the negative
    // control for it: it plants the shapes the guards were written for, on input it controls, so a
    // regression shows up here as a failed CHECK instead of as a silent skip.
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "cpp_pinyin_minimal_usage_marker_parser.txt";

    const auto parseScratch = [&scratch](const std::string &content)
    {
        {
            std::ofstream out(scratch, std::ios::trunc);
            REQUIRE(out.is_open());
            out << content;  // flushed and closed by the destructor at the end of this block
        }
        return numberedLines(scratch);
    };

    SECTION("a marker that a trailing `//` pushed onto whitespace is still FOUND") {
        // The first draft of the sample wrote `declaration; //` with the next line carrying the marker,
        // and this shape is what the whitespace skip keeps correct: `rfind` lands on the LAST `// `, so
        // the text after it starts with spaces. Without the skip the line is dropped, and the six
        // markers come from six OTHER lines -- a count that is 6 while the thing it counts changed.
        const auto planted = parseScratch("const auto built = cpp_pinyin::v2::Engine::open(...); //\n"
                                          "    call(); // 1  the marker\n"
                                          "next(); // 2  another\n");
        REQUIRE(planted.size() == 2);
        CHECK(planted[0].marker == 1);
        CHECK(planted[0].body == "call();");
        CHECK(planted[1].marker == 2);
        CHECK(planted[1].body == "next();");
    }

    SECTION("a trailing `//` with no digit after it is NOT a marker") {
        const auto planted = parseScratch("one(); //\n"
                                          "two(); // 1  the only marker\n");
        REQUIRE(planted.size() == 1);
        CHECK(planted[0].marker == 1);
        CHECK(planted[0].body == "two();");
    }

    SECTION("a comment that starts with a digit is NOT a marker when nothing precedes it") {
        // Measured during S13: skipping whitespace after `// ` makes prose written as
        // `// 2  <explanation>` look exactly like a marker -- and this file's own header comments are
        // written that way, so the sample reported 8 markers until the body guard below rejected them.
        // A marker marks a STATEMENT, so the text to its left is never empty; that is the difference.
        const auto planted = parseScratch("// 1  prose that must not count\n"
                                          "call(); // 2  the only marker\n");
        REQUIRE(planted.size() == 1);
        CHECK(planted[0].marker == 2);
        CHECK(planted[0].body == "call();");
    }
}
