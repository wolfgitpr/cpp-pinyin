// The 2.0 face's `Syllable` is DATA: `tone` is the dialect's own tone-category number, read from the
// dictionary surface, and nothing about it may be recovered from a rendered string. That rule and its three
// companions are stated once in the header's comment (wording authority: the 2.0 plan's section E, retired
// in the 2026-10-09 documentation cut; that plan is not in this tree). This suite pins both halves:
//
//   * the shape -- default values, distinct enum values, and a value-semantics round trip of the tone
//     numbers each dialect actually uses (Cantonese 1..6, Mandarin 0/5);
//   * the text -- the four sentences are still in the header: they ARE the contract, so if one is dropped
//     or reworded the header stops being the authority and nothing else in the build notices.
//
// The header path arrives as a compile definition (the idiom tests/CMakeLists.txt already uses for
// examples/minimal_usage.cpp): this test runs from the build tree, so a relative path back to the source
// tree would depend on the working directory.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

#include <cpp-pinyin2/core/Syllable.h>

#ifndef CPP_PINYIN_SYLLABLE_HEADER_SOURCE
#error "CPP_PINYIN_SYLLABLE_HEADER_SOURCE must be defined by tests/CMakeLists.txt"
#endif

namespace
{

    // The four anchors are the load-bearing phrases of the four contract sentences, one per sentence:
    //   1 -> 词表原始注音 "the table's original notation" (the tone's single authority is the dictionary's own
    //   notation) 2 -> 允许全 "all-unspecified permitted" (all-`unspecified` is permitted, and the API may not act on
    //   it) 3 -> 同一表层 "one surface" (de / de5 are one surface, told apart only by markNeutralAs5) 4 -> 不得引入
    //   7/8/9 "must not introduce 7/8/9" (Cantonese keeps 1..6; checked entering tones add no phase)
    // Matching phrase-level anchors rather than whole sentences keeps a reflow or a whitespace edit
    // from failing the suite while still failing when a rule is actually removed or rewritten.
    const char *const kContractAnchors[] = {
        "词表原始注音",
        "允许全",
        "同一表层",
        "不得引入 7/8/9",
    };

    std::string readWholeFile(const char *path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.is_open());
        using Iterator = std::istreambuf_iterator<char>;
        return std::string(Iterator(in), Iterator());
    }

}  // namespace

TEST_CASE("Syllable defaults: tone 0 and tone_kind unspecified", "[syllable][contract]") {
    const cpp_pinyin::v2::Syllable fresh;

    // `tone == 0` is not "no tone yet" in a dialect-neutral sense: it is the value the contract calls
    // out for Mandarin 轻声 when `markNeutralAs5 = false`. The default therefore says "nothing was read
    // yet", and `tone_kind` says the same thing in its own domain.
    CHECK(fresh.tone == 0);
    CHECK(fresh.tone_kind == cpp_pinyin::v2::ToneKind::unspecified);

    // The two string members default to empty and the source to the dictionary, so a default-constructed
    // Syllable never claims to come from a table it was not read from.
    CHECK(fresh.initial.empty());
    CHECK(fresh.fin.empty());
    CHECK(fresh.source == cpp_pinyin::v2::ReadingSource::dictionary);
}

TEST_CASE("ToneKind and ReadingSource values stay mutually distinct", "[syllable][contract]") {
    // The point of this case is future edits: these enums are mirrored by the C face, so an edit that
    // gives two names the same number (or renumbers one onto another) would silently make two
    // different meanings indistinguishable -- and the compiler cannot see it. Each pair is compared.
    const cpp_pinyin::v2::ToneKind kinds[] = {
        cpp_pinyin::v2::ToneKind::unspecified,
        cpp_pinyin::v2::ToneKind::base,
        cpp_pinyin::v2::ToneKind::sandhi,
    };
    const std::size_t kindCount = sizeof(kinds) / sizeof(kinds[0]);
    REQUIRE(kindCount == 3);
    for (std::size_t i = 0; i < kindCount; ++i) {
        for (std::size_t j = i + 1; j < kindCount; ++j) {
            INFO("ToneKind #" << i << " and #" << j << " must differ");
            CHECK(kinds[i] != kinds[j]);
        }
    }

    const cpp_pinyin::v2::ReadingSource sources[] = {
        cpp_pinyin::v2::ReadingSource::dictionary,
        cpp_pinyin::v2::ReadingSource::phrase,
        cpp_pinyin::v2::ReadingSource::user_dict,
        cpp_pinyin::v2::ReadingSource::plugin,
    };
    const std::size_t sourceCount = sizeof(sources) / sizeof(sources[0]);
    REQUIRE(sourceCount == 4);
    for (std::size_t i = 0; i < sourceCount; ++i) {
        for (std::size_t j = i + 1; j < sourceCount; ++j) {
            INFO("ReadingSource #" << i << " and #" << j << " must differ");
            CHECK(sources[i] != sources[j]);
        }
    }
}

TEST_CASE("Syllable round-trips the tone numbers both dialects use", "[syllable][contract]") {
    // Cantonese 1..6 as a whole set, and Mandarin's two neutral-tone encodings (0 = no tone mark,
    // 5 = markNeutralAs5). A value round trip is what the contract needs here: 7/8/9 must have nowhere
    // to come from (Cantonese entering tones are 1/3/6 plus a -p/-t/-k coda, not extra phases), and the
    // struct must not normalise, clamp or renumber what it was given.
    for (int tone = 1; tone <= 6; ++tone) {
        cpp_pinyin::v2::Syllable cantonese;
        cantonese.initial = "z";
        cantonese.fin = "ung";
        cantonese.tone = tone;
        cantonese.source = cpp_pinyin::v2::ReadingSource::phrase;

        const cpp_pinyin::v2::Syllable copy = cantonese;  // value semantics, not a reference
        INFO("Cantonese tone " << tone);
        CHECK(copy.tone == tone);
        CHECK(copy.initial == "z");
        CHECK(copy.fin == "ung");
        CHECK(copy.source == cpp_pinyin::v2::ReadingSource::phrase);
        CHECK(copy.tone_kind == cpp_pinyin::v2::ToneKind::unspecified);
    }

    const int neutralEncodings[] = {0, 5};
    for (const int tone : neutralEncodings) {
        cpp_pinyin::v2::Syllable mandarin;
        mandarin.initial = "d";
        mandarin.fin = "e";
        mandarin.tone = tone;

        const cpp_pinyin::v2::Syllable copy = mandarin;
        INFO("Mandarin neutral encoding " << tone);
        CHECK(copy.tone == tone);
    }
}

TEST_CASE("the header still carries the four authoritative contract sentences", "[syllable][contract][source]") {
    const std::string text = readWholeFile(CPP_PINYIN_SYLLABLE_HEADER_SOURCE);

    // A read that silently came back empty would make every anchor check below vacuous, so it is its
    // own failure -- "0 anchors found" must never be reachable by reading nothing at all.
    INFO("source = " << CPP_PINYIN_SYLLABLE_HEADER_SOURCE);
    INFO("bytes read = " << text.size());
    REQUIRE_FALSE(text.empty());

    for (const char *anchor : kContractAnchors) {
        INFO("missing contract anchor: " << anchor << " in " << CPP_PINYIN_SYLLABLE_HEADER_SOURCE);
        CHECK(text.find(anchor) != std::string::npos);
    }
}
