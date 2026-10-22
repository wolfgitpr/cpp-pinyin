#include "PhraseIndex.h"

// Moved verbatim out of EngineImpl::rebuildPhraseIndex() (P2 split #1). The only change is that the
// member tables became reference parameters; the algorithm, the pass structure, the overflow checks
// and the resulting memory layout are untouched. The self-check that used to follow the placement
// deliberately stayed in EngineImpl: it calls findPhrase(), which is the hot-path inline reader and
// must not be pulled into this layer.

namespace cpp_pinyin::v2
{
    namespace detail
    {
        bool buildPhraseIndex(const std::unordered_map<uint64_t, std::vector<std::u16string>> &phrases,
                              std::vector<PhraseGate> &gates, std::vector<uint64_t> &keys,
                              std::vector<const std::vector<std::u16string> *> &values) {
            // Pass 1: count the bucket of every first character and collect its length mask and
            // second-character bitmap. `counts` is used instead of writing the bucket address
            // straight into the gate so the prefix sum below needs no second buffer over the same
            // 64K entries.
            std::vector<uint16_t> counts(CHAR16_TABLE_SIZE, 0);
            std::vector<PhraseGate> builtGates(CHAR16_TABLE_SIZE);
            bool ok = true;
            size_t total = 0;
            for (const auto &kv : phrases) {
                const size_t len = phraseKeyLength(kv.first);
                if (len == 0) {
                    // Cannot happen for a real key: refuse to index rather than guess.
                    ok = false;
                    break;
                }
                if (len < 2 || len > 4)
                    continue;  // the window never probes these, so they must not reach the index
                const uint16_t first = static_cast<uint16_t>(kv.first >> (16 * (len - 1)));
                if (counts[first] == 0xFFFF) {
                    ok = false;  // bucket would overflow the packed count
                    break;
                }
                counts[first]++;
                builtGates[first].desc |= (1u << (len - 2));
                builtGates[first].secondBits |= (1ull << (static_cast<uint16_t>(kv.first >> (16 * (len - 2))) & 63));
                total++;
            }
            if (total > 0xFFFF)
                ok = false;  // start field is 16 bits

            if (!ok)
                return false;

            uint32_t acc = 0;
            for (size_t c = 0; c < CHAR16_TABLE_SIZE; ++c) {
                if (counts[c] == 0)
                    continue;
                builtGates[c].desc |= (acc << 16) | (static_cast<uint32_t>(counts[c]) << 8);
                acc += counts[c];
            }

            // Pass 2: place every key in its bucket. Buckets are contiguous and each key is
            // stored whole, so the bucket scan is an exact membership test.
            std::vector<uint64_t> builtKeys(total);
            std::vector<const std::vector<std::u16string> *> builtValues(total);
            std::vector<uint16_t> cursor(CHAR16_TABLE_SIZE, 0);
            for (const auto &kv : phrases) {
                const size_t len = phraseKeyLength(kv.first);
                if (len < 2 || len > 4)
                    continue;
                const uint16_t first = static_cast<uint16_t>(kv.first >> (16 * (len - 1)));
                const uint32_t at = (builtGates[first].desc >> 16) + cursor[first]++;
                builtKeys[at] = kv.first;
                builtValues[at] = &kv.second;
            }

            gates.swap(builtGates);
            keys.swap(builtKeys);
            values.swap(builtValues);
            return true;
        }
    }  // namespace detail
}  // namespace cpp_pinyin::v2
