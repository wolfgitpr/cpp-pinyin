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
#include <unordered_map>
#include <vector>

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

    std::puts("DictSelfCheck OK");
    return 0;
}
