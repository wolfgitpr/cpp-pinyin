// AC-4 driver: the dict layer must compile AND run with nothing but itself linked.
//
// This TU is built into the DictSelfCheck target, which compiles src/dict/*.cpp directly and does
// NOT link the cpp-pinyin2 library. That is the whole point: if the dict layer ever reaches back into
// the tone or engine layers (AC-3), this target stops building instead of the dependency hiding
// inside the library. No Catch2, no fixtures -- a plain driver, so no test framework is needed to
// run it.
//
// It also pins the behaviour the engine relies on: keys shorter than 2 are skipped, buckets hold
// whole keys, the gate packs the bucket start and length mask, and an unindexable dictionary is
// refused rather than guessed at.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "dict/DictManifest.h"
#include "dict/PhraseIndex.h"

namespace
{

    uint64_t keyOf(const char16_t *chars, size_t len) { return cpp_pinyin::v2::encodePhrasesKey(chars, len); }

    int fail(const char *what) {
        std::fprintf(stderr, "DictSelfCheck FAILED: %s\n", what);
        return 1;
    }

}  // namespace

int main() {
    using cpp_pinyin::v2::detail::PhraseGate;

    std::unordered_map<uint64_t, std::vector<std::u16string>> phrases;
    const uint64_t one = keyOf(u"一", 1);  // length 1: the window never probes it
    const uint64_t two = keyOf(u"一下", 2);  // length 2
    const uint64_t three = keyOf(u"一下儿", 3);  // length 3, same first character as `two`
    phrases[one] = {u"yi1"};
    phrases[two] = {u"yi1", u"xia4"};
    phrases[three] = {u"yi1", u"xia4", u"er2"};

    std::vector<PhraseGate> gates;
    std::vector<uint64_t> keys;
    std::vector<const std::vector<std::u16string> *> values;
    if (!cpp_pinyin::v2::detail::buildPhraseIndex(phrases, gates, keys, values))
        return fail("a healthy dictionary must be indexable");
    if (keys.size() != 2 || values.size() != 2)
        return fail("only the 2- and 3-character keys belong in the index");

    const auto &gate = gates[static_cast<uint16_t>(u'一')];
    const uint32_t start = gate.desc >> 16;
    const uint32_t count = (gate.desc >> 8) & 0xFF;
    if (count != 2)
        return fail("both 2- and 3-character keys share the bucket of their first character");
    if ((gate.desc & 0x3u) != 0x3u)
        return fail("the length mask must advertise lengths 2 and 3");
    if (start != 0)
        return fail("the only non-empty bucket starts at 0");

    // Buckets hold whole keys, so a scan is an exact membership test, and each value pointer must
    // still refer to the map node its key came from. BOTH keys are required: checking one of them
    // with an early return would pass even if the bucket silently dropped the other, which is the
    // kind of self-check that looks green and proves nothing.
    bool sawTwo = false;
    bool sawThree = false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = start + i;
        if (keys[at] == two) {
            if (values[at] != &phrases.at(two))
                return fail("the 2-character key's value points at the wrong node");
            sawTwo = true;
        } else if (keys[at] == three) {
            if (values[at] != &phrases.at(three))
                return fail("the 3-character key's value points at the wrong node");
            sawThree = true;
        } else {
            return fail("the bucket holds a key that was never indexed");
        }
    }
    if (!sawTwo || !sawThree)
        return fail("a whole key went missing from its bucket");

    // 256 keys behind ONE first character. The gate packs the bucket count into 8 bits (desc >> 8 &
    // 0xFF) and the start into 16, so the 256th entry would wrap the count into the start field and
    // corrupt both numbers with nothing to show for it -- which is exactly what the guard in
    // buildPhraseIndex refuses. The guard used to test 0xFFFF, the width of the counts ELEMENT rather
    // than of the field it is packed into, and nothing pinned that: a regression to the
    // old value would have restored the silent corruption without a single test going red. 255 keys
    // must still index, so this checks both sides of the boundary rather than only the refusal.
    {
        std::unordered_map<uint64_t, std::vector<std::u16string>> crowded;
        const char16_t first = u'一';
        for (uint16_t i = 0; i < 255; ++i) {
            const char16_t pair[2] = {first, static_cast<char16_t>(0x4E00 + i)};
            crowded[keyOf(pair, 2)] = {u"yi1"};
        }
        std::vector<PhraseGate> crowdedGates;
        std::vector<uint64_t> crowdedKeys;
        std::vector<const std::vector<std::u16string> *> crowdedValues;
        if (!cpp_pinyin::v2::detail::buildPhraseIndex(crowded, crowdedGates, crowdedKeys, crowdedValues))
            return fail("a bucket of exactly 255 must still be indexable");
        if (crowdedKeys.size() != 255)
            return fail("a full bucket must hold all 255 keys");
        if (((crowdedGates[static_cast<uint16_t>(first)].desc >> 8) & 0xFF) != 255)
            return fail("a full bucket must report its count as 255");

        const char16_t extra[2] = {first, static_cast<char16_t>(0x4E00 + 255)};
        crowded[keyOf(extra, 2)] = {u"yi1"};
        if (cpp_pinyin::v2::detail::buildPhraseIndex(crowded, crowdedGates, crowdedKeys, crowdedValues))
            return fail("256 keys in one bucket must be refused, not silently truncated");
    }

    // THE MANIFEST CHECK'S TWO UNREACHABLE-FROM-OUTSIDE GUARDS. Both were added to catch a real defect
    // and neither could be tested through `Engine::open`: the zero-byte table rule
    // because the shipped data never has one, and the library-version rule because the
    // version string comes from the library's own build, so a test can only hand it a broken one here.
    {
        namespace fs = std::filesystem;
        const fs::path scratch = fs::temp_directory_path() / "cpp-pinyin2-selfcheck-manifest";
        std::error_code ec;
        fs::remove_all(scratch, ec);
        fs::create_directories(scratch / "mandarin", ec);
        std::ofstream(scratch / "mandarin" / "format.txt") << "1\n";

        // 1. zero-byte word.txt: the marker says the generation is complete, so an empty table is
        //    truncation. A directory with NO word.txt at all is a different case and is not refused
        //    here (the table layer reports it), so this checks the distinction rather than the absence.
        std::ofstream(scratch / "mandarin" / "word.txt", std::ios::binary | std::ios::trunc);
        auto empty = cpp_pinyin::v2::detail::checkDictManifest(scratch, "mandarin", 1, "2.0.0");
        if (empty.code != cpp_pinyin::v2::Code::Load || empty.detail.find("zero-byte") == std::string::npos)
            return fail("a zero-byte word.txt must be refused, naming the empty table");

        // 2. a library version that is not a version. An EMPTY one still skips (documented: a build that
        //    did not define it does not guess), a NON-empty broken one must not skip the comparison.
        std::ofstream(scratch / "mandarin" / "word.txt") << u8"一:yi1\n";
        std::ofstream(scratch / "mandarin" / "dialect.json")
            << "{\n    \"id\":  \"mandarin\",\n    \"min_library\":  \"2.0.0\"\n}\n";
        const auto skipped = cpp_pinyin::v2::detail::checkDictManifest(scratch, "mandarin", 1, "");
        if (!skipped.detail.empty())
            return fail("an undefined library version must skip the floor check, not fail it");
        const auto broken = cpp_pinyin::v2::detail::checkDictManifest(scratch, "mandarin", 1, "2.0.0-rc1");
        if (broken.code != cpp_pinyin::v2::Code::Load || broken.detail.find("not a version") == std::string::npos)
            return fail("a library version that is not a version must be refused, not skipped");
        const auto newer = cpp_pinyin::v2::detail::checkDictManifest(scratch, "mandarin", 1, "1.9.0");
        if (newer.code != cpp_pinyin::v2::Code::DictIncompatible)
            return fail("a floor above this library must be its own code, not a load failure");
        const auto fine = cpp_pinyin::v2::detail::checkDictManifest(scratch, "mandarin", 1, "2.0.0");
        if (!fine.detail.empty())
            return fail("a manifest this library satisfies must be accepted");

        fs::remove_all(scratch, ec);
    }

    std::puts("DictSelfCheck OK");
    return 0;
}
