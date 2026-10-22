#pragma once

// Phrase index construction (P2 split #1; the split ledger was retired in the 2026-10-09 documentation
// cut, see docs/HISTORY.md).
//
// This header deliberately depends on nothing but the standard library and the peer DictUtil.h:
// src/dict/** must not include tone/ or engine/ (AC-3), and the builder must be usable, and
// testable, without linking the engine (AC-4). The READ side stays in EngineImpl, because the
// window walk there is the query hot path.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "DictUtil.h"

namespace cpp_pinyin::v2
{
    namespace detail
    {
        // Number of the highest non-empty 16-bit group of a phrase key (1..4), i.e. the key
        // length, or 0 when the key is zero. Keys are built by encodePhrasesKey(), which shifts in
        // one non-zero char16 per character, and the loaders feed it the raw key text, so the
        // number of non-empty groups is exactly the number of characters.
        inline size_t phraseKeyLength(uint64_t key) {
            size_t len = 0;
            for (size_t group = 1; group <= 4; group++) {
                if ((key >> (16 * (group - 1))) != 0)
                    len = group;
            }
            return len;
        }

        /// The two gate words of one first character share a slot so that the window reads
        /// one cache line per character instead of one per word: the tables are sparse
        /// (1,896 of 65,536 rows are ever filled), so each touched row costs a whole line,
        /// and halving the number of touched arrays halves that cost. Both words are
        /// superset filters only -- a set bit means "scan the bucket", never a hit.
        struct PhraseGate {
            uint32_t desc;  ///< packed bucket address + length mask (see above)
            uint32_t reserved;  ///< keeps secondBits 8-byte aligned without a read-modify-write
            uint64_t secondBits;  ///< bit `second & 63` set when (first, second) starts a phrase
        };

        /// Builds the bucket table and the two parallel arrays from `phrases`.
        ///
        /// `gates` is indexed by the first character itself (CHAR16_TABLE_SIZE rows); `keys` holds every
        /// key whose length is 2..4 grouped into contiguous buckets, and `values[i]` points at the map
        /// node `keys[i]` came from -- node addresses are stable in an unordered_map, which is what makes
        /// those pointers safe.
        ///
        /// Returns false when the dictionary cannot be indexed (a zero key, a bucket overflowing the
        /// packed 16-bit count, or more than 0xFFFF keys in total), leaving the outputs unspecified; the
        /// caller clears its tables and disables the index. Nothing here is approximate: keys are stored
        /// whole, so a bucket scan is an exact membership test.
        bool buildPhraseIndex(const std::unordered_map<uint64_t, std::vector<std::u16string>> &phrases,
                              std::vector<PhraseGate> &gates, std::vector<uint64_t> &keys,
                              std::vector<const std::vector<std::u16string> *> &values);
    }  // namespace detail
}  // namespace cpp_pinyin::v2
