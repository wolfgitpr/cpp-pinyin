// R-6 (the plan's N3, decided as X2): a dialect's style table must round-trip.
//
// The table is the single authority for "what can this dialect emit, and what is each of those
// called", and it is the data `cppp2_style_list` (N29) will hand to the shells. Three things can rot
// in it and NONE of them would fail a rendering test -- a name that resolves to a different number
// than it declares, a default that is not in the table at all, and a number outside the shared
// 0..MAX_STYLE domain, which would index past `ToneConverter::m_converts`. So this file does not ask
// the engine to render anything: it reads the tables the way a shell will read them.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("built-in dialects declare a self-consistent style table", "[style_table][n3]") {
    const cpp_pinyin::v2::Registry registry;

    for (const char *id : {"mandarin", "cantonese"}) {
        INFO("dialect: " << id);
        const auto dialect = registry.resolve(id);
        REQUIRE(dialect != nullptr);
        REQUIRE_FALSE(dialect->styles.empty());

        std::size_t defaults = 0;
        for (const cpp_pinyin::v2::StyleDecl &decl : dialect->styles) {
            INFO("style: " << decl.name);
            CHECK_FALSE(decl.name.empty());

            // (c) The alias is inside the one shared domain. A number outside it is not merely
            // odd -- it is an out-of-range index into the converter's own table.
            CHECK(decl.alias >= 0);
            CHECK(decl.alias < cpp_pinyin::v2::ToneConverter::MAX_STYLE);

            // (a) The name resolves back to the SAME alias. This is the assertion that fails when a
            // name is duplicated, and it is why `findStyle` returning the first match is enough.
            const cpp_pinyin::v2::StyleDecl *found = cpp_pinyin::v2::findStyle(dialect->styles, decl.name);
            REQUIRE(found != nullptr);
            CHECK(found->alias == decl.alias);

            if (decl.isDefault)
                ++defaults;
        }

        // (b) Exactly one default, and `defaultStyleOf` names an entry that exists --
        // a fallback pointing at a number no entry declares would be a silent hole for a caller
        // that asks for no particular style.
        CHECK(defaults == 1);
        bool defaultIsPresent = false;
        for (const cpp_pinyin::v2::StyleDecl &decl : dialect->styles)
            defaultIsPresent = defaultIsPresent || decl.alias == cpp_pinyin::v2::defaultStyleOf(dialect->styles);
        CHECK(defaultIsPresent);
        CHECK_FALSE(cpp_pinyin::v2::findStyle(dialect->styles, "no-such-style") != nullptr);
    }
}

TEST_CASE("the two built-ins declare exactly what they can emit", "[style_table][n3]") {
    const cpp_pinyin::v2::Registry registry;

    // Mandarin fills all nine numbers of the domain, and its default is TONE (1) -- the same default
    // `EngineConfig` used to have, so a caller that says nothing keeps getting the same bytes.
    const auto mandarin = registry.resolve("mandarin");
    REQUIRE(mandarin != nullptr);
    CHECK(mandarin->styles.size() == 9);
    CHECK(cpp_pinyin::v2::defaultStyleOf(mandarin->styles) == static_cast<int>(cpp_pinyin::v2::MandarinStyle::TONE));

    // Cantonese declares three of the nine and occupies 0 / 3 / 8 -- the sparse part of the shared
    // domain. `findStyle` must NOT find Mandarin's names here: "tone" is the name a shell would most
    // plausibly reach for, and resolving it to Cantonese's `first-letter` (3) would be a silent
    // wrong answer rather than a missing one.
    const auto cantonese = registry.resolve("cantonese");
    REQUIRE(cantonese != nullptr);
    CHECK(cantonese->styles.size() == 3);
    CHECK(cpp_pinyin::v2::defaultStyleOf(cantonese->styles) == static_cast<int>(cpp_pinyin::v2::CantoneseStyle::TONE3));
    CHECK(cpp_pinyin::v2::findStyle(cantonese->styles, "tone") == nullptr);
    CHECK(cpp_pinyin::v2::findStyle(cantonese->styles, "tone2") == nullptr);
    CHECK(cpp_pinyin::v2::findStyle(cantonese->styles, "bopomofo") == nullptr);

    const cpp_pinyin::v2::StyleDecl *tone3 = cpp_pinyin::v2::findStyle(cantonese->styles, "tone3");
    REQUIRE(tone3 != nullptr);
    CHECK(tone3->alias == static_cast<int>(cpp_pinyin::v2::CantoneseStyle::TONE3));
    CHECK(tone3->isDefault);
}
