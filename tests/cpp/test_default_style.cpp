// R-4 (the plan's C4/N27): an EMPTY config must mean the same thing in every layer.
//
// It used to mean three different things -- TONE through the Mandarin facade, TONE3 through the Cantonese
// one, NORMAL (0) through `Engine`, the C ABI and both shells -- so one sentence came out as different bytes
// depending on who asked. The merged config replaced that with ONE sentinel (`Style{}` == -1) resolved in one
// place; this file makes a silent return to the old state impossible: red if the default drifts back to 0, if
// a dialect's own default stops being honoured, or if a dialect declaring no styles stops falling back to the
// numbering every dialect shared before styles had names.
//
// The byte-level half lives where the bytes are: tests/c_api_smoke.c pins that a `cppp2_query_opts` straight
// out of `cppp2_query_opts_init` renders Mandarin's TONE ("nǐ hǎo"), and tools/dump_equivalence.cpp keeps
// its four explicit configs.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

TEST_CASE("an empty config asks for the dialect's own default", "[default_style][n27]") {
    // The sentinel is what an empty config carries -- and it is -1, not 0. Zero is a REAL style
    // (NORMAL), which is exactly why "unset" could not be spelled with it.
    CHECK(cpp_pinyin::v2::EngineConfig{}.style.alias == -1);

    // A dialect that declared a default gets it. The Mandarin number is 1 (TONE), not 0: this pair is
    // the assertion that was false before C4/N27, where the default was 0 and the sentence below
    // rendered as "zhong guo" with no tone at all.
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::Style{}, 1) == 1);
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::Style{}, 8) == 8);

    // An explicit style always wins, including 0 itself.
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::Style{0}, 1) == 0);
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::MandarinStyle::TONE3, 1) == 8);
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::CantoneseStyle::FIRST_LETTER, 8) == 3);

    // A dialect that marks no `isDefault` entry (`defaultStyleOf` == -1) keeps the numbering every
    // dialect shared before styles had names: 0, i.e. pass the reading through. Custom dialects do not
    // change behaviour, and `-1` never escapes as a style -- it would index outside the converter.
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::Style{}, -1) == 0);
    CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::Style{}, -1) != -1);
}

TEST_CASE("every built-in dialect's default is the one its table declares", "[default_style][n27]") {
    const cpp_pinyin::v2::Registry registry;

    for (const char *id : {"mandarin", "cantonese"}) {
        INFO("dialect: " << id);
        const auto dialect = registry.resolve(id);
        REQUIRE(dialect != nullptr);

        // The dialect's declared default is what an empty config resolves to -- the same expression
        // the query paths use (Engine.cpp, c_api_query.cpp), read here from the same table.
        const int declaredDefault = cpp_pinyin::v2::defaultStyleOf(dialect->styles);
        CHECK(cpp_pinyin::v2::resolveStyle(cpp_pinyin::v2::EngineConfig{}.style, declaredDefault) == declaredDefault);

        // …and that number is one of the styles the dialect actually declares, rather than a number
        // it can never emit. (test_style_table.cpp owns the deeper round-trip; this is the one line
        // that ties the table to the default a caller gets.)
        bool declared = false;
        for (const cpp_pinyin::v2::StyleDecl &style : dialect->styles)
            declared = declared || style.alias == declaredDefault;
        CHECK(declared);
    }
}
