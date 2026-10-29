#pragma once

// The phrase layer of one dialect, regrouped out of EngineImpl.
//
// This is a pure regrouping: the members keep their meaning, their types and the memory layout they
// had as EngineImpl members, and the behaviour is bit-for-bit unchanged. The point is that the
// load-time code -- which never runs in the query hot path -- can then live in the dict layer, where
// it is testable without the engine (AC-4) and cannot reach back into tone/engine (AC-3).
//
// What is deliberately NOT in here: m_transTable. toSimp() reads it (EngineImpl.h) and toSimp() is
// called from the query hot path (assemble()), so the table belongs to the engine; the alias
// computation below takes toSimp as a callable instead, exactly the way loadAdditionalDict already
// takes its reading normalizer. That keeps this header free of any tone or engine dependency.
//
// m_wordIndex / m_wordValues (the character table) are a separate cluster and are not touched here.

#include <bitset>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "DictUtil.h"
#include "PhraseIndex.h"

namespace cpp_pinyin::v2
{
    namespace detail
    {
        struct PhraseLayers {
            /// First characters of a built-in phrase key: the window gate for the built-in layer.
            /// (was EngineImpl::m_phrasesMapBits)
            std::bitset<CHAR16_TABLE_SIZE> builtinStart;

            /// First characters of a USER phrase key, kept separately so clearing the user layer can
            /// reset exactly this half of the gate. (was EngineImpl::m_userPhraseStart)
            std::bitset<CHAR16_TABLE_SIZE> userStart;

            /// Merged table: built-in rows plus the user layer plus the derived aliases.
            /// (was EngineImpl::m_phrasesDict)
            std::unordered_map<uint64_t, std::vector<std::u16string>> merged;

            /// The user layer alone, exactly as the caller supplied it: this is the authoritative input
            /// for every later rebuild. (was EngineImpl::m_userPhrases)
            std::unordered_map<uint64_t, std::vector<std::u16string>> user;

            /// Derived index: gates indexed by first character, keys and values in contiguous buckets.
            /// (were EngineImpl::m_phraseGates / m_phraseKeys / m_phraseValues)
            std::vector<PhraseGate> gates;
            std::vector<uint64_t> keys;
            std::vector<const std::vector<std::u16string> *> values;

            /// Whether the index above matches `merged`, and how many keys the last build verified.
            /// (were EngineImpl::m_phraseIndexReady / m_phraseIndexChecked)
            bool indexReady = false;
            size_t indexChecked = 0;
        };

        /// Rebuilds `phrases.merged` from the built-in file, then the user layer, then the aliases.
        ///
        /// Two things come from the engine rather than being reached for, which keeps this header free of
        /// tone/engine dependencies (AC-3): the dictionary directory, and `toSimp` (the traditional-to-
        /// simplified mapping used to compute the alias keys). `toSimp` cannot be owned here because the
        /// query hot path reads the same mapping.
        ///
        /// Returns the result of loading the BUILT-IN file (the user layer and the aliases never fail the
        /// whole rebuild). The caller re-derives the index afterwards.
        bool rebuildPhrases(PhraseLayers &phrases, const std::filesystem::path &dictDir,
                            const std::function<char16_t(char16_t)> &toSimp);

        /// Merges one user dictionary file (or clears the user layer first, when `clear` is true) into
        /// `phrases.user`, and turns on the user half of the window gate.
        ///
        /// `normalizeReading` and `error` are the engine's: the reading normalizer is dialect-specific
        /// (a hardcoded Mandarin one used to mangle Cantonese user dictionaries), and the error slot is
        /// the engine's lastError(). Returns false only when the file itself could not be loaded.
        bool loadUserDictLayer(PhraseLayers &phrases, const std::filesystem::path &path, bool clear,
                               const std::function<std::u16string(const std::u16string &)> &normalizeReading,
                               std::string &error);
    }  // namespace detail
}  // namespace cpp_pinyin::v2
