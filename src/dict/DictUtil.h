#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>

namespace cpp_pinyin::v2
{
    // Encode 2-4 char16_t values into a single uint64_t key.
    // Max phrase length is 4, so 4 × 16 = 64 bits fits exactly.
    inline uint64_t encodePhrasesKey(const char16_t *data, size_t len) {
        uint64_t key = 0;
        for (size_t i = 0; i < len; ++i)
            key = (key << 16) | static_cast<uint16_t>(data[i]);
        return key;
    }

    inline uint64_t encodePhrasesKey(const std::u16string &s) { return encodePhrasesKey(s.data(), s.size()); }

    // Size of every `char16_t`-keyed direct-indexed table in this library: the loaders below, the engine's
    // phrase gates and its character tables all index by the code unit itself, so the size is exactly the
    // number of distinct `char16_t` values.
    //
    // Two nearby 16-bit constants look alike and are not: `U16Str.cpp`'s 0x10000 is the first code point
    // past the BMP (surrogate arithmetic), and the 0xFFFF saturation in the phrase index is the largest
    // value a uint16_t holds. This one is a count, that one is a boundary, the other a maximum.
    inline constexpr size_t CHAR16_TABLE_SIZE = 65536;

    // --- Direct-indexed array loaders (for char16_t-keyed dicts) ---

    // trans_word: char16_t → char16_t, stored in flat array[CHAR16_TABLE_SIZE], sentinel = 0
    bool loadDict(const std::filesystem::path &dict_dir, std::array<char16_t, CHAR16_TABLE_SIZE> &table,
                  const char &sep1 = ':');

    // phrases_map: char16_t → presence bit, stored in bitset<CHAR16_TABLE_SIZE>
    bool loadDict(const std::filesystem::path &dict_dir, std::bitset<CHAR16_TABLE_SIZE> &bits, const char &sep1 = ':');

    // word: char16_t → index into dense vector, stored in array[CHAR16_TABLE_SIZE] + vector
    bool loadDict(const std::filesystem::path &dict_dir, std::array<uint16_t, CHAR16_TABLE_SIZE> &index,
                  std::vector<std::vector<std::u16string>> &values, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    // --- uint64_t-keyed loaders (for phrase dicts with 2-4 char keys) ---

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<uint64_t, std::vector<std::u16string>> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    /// Loads a phrase dictionary. When `phraseStartOut` is given, every character that begins a key is
    /// recorded in it -- the caller uses that bitmap to decide whether the sliding window must open at a
    /// character, and deriving it here keeps the index and the map contents from ever disagreeing.
    ///
    /// `normalizeReading` is MANDATORY: it maps a reading as written in the file onto the form this
    /// dialect stores internally (Mandarin `"zhi4"` -> `"zhì"`; Cantonese identity). It used to default to
    /// the Mandarin function, which silently rewrote Cantonese user dictionaries (`jyut6` -> `jyu6t6`).
    bool loadAdditionalDict(const std::filesystem::path &dict_dir,
                            std::unordered_map<uint64_t, std::vector<std::u16string>> &resultMap,
                            const std::function<std::u16string(const std::u16string &pinyin)> &normalizeReading,
                            const char &sep1 = ':', const std::string &sep2 = " ",
                            std::bitset<CHAR16_TABLE_SIZE> *phraseStartOut = nullptr);
}  // namespace cpp_pinyin::v2
