#include "ManToneUtil.h"

#include <algorithm>
#include <array>
#include <vector>

namespace cpp_pinyin::v2
{
    // Direct lookup: (vowel, tone_digit) -> tone diacritic char16_t.
    // Encodes vowel index (a=0,e=1,i=2,o=3,u=4,v=5) * 4 + (digit-1) into a flat array of 24 entries.
    static constexpr size_t VOWEL_TONE_TABLE_SIZE = 24;

    static constexpr auto buildVowelToneTable() {
        std::array<char16_t, VOWEL_TONE_TABLE_SIZE> t{};
        // a: ā á ǎ à
        t[0 * 4 + 0] = 0x0101;
        t[0 * 4 + 1] = 0x00E1;
        t[0 * 4 + 2] = 0x01CE;
        t[0 * 4 + 3] = 0x00E0;
        // e: ē é ě è
        t[1 * 4 + 0] = 0x0113;
        t[1 * 4 + 1] = 0x00E9;
        t[1 * 4 + 2] = 0x011B;
        t[1 * 4 + 3] = 0x00E8;
        // i: ī í ǐ ì
        t[2 * 4 + 0] = 0x012B;
        t[2 * 4 + 1] = 0x00ED;
        t[2 * 4 + 2] = 0x01D0;
        t[2 * 4 + 3] = 0x00EC;
        // o: ō ó ǒ ò
        t[3 * 4 + 0] = 0x014D;
        t[3 * 4 + 1] = 0x00F3;
        t[3 * 4 + 2] = 0x01D2;
        t[3 * 4 + 3] = 0x00F2;
        // u: ū ú ǔ ù
        t[4 * 4 + 0] = 0x016B;
        t[4 * 4 + 1] = 0x00FA;
        t[4 * 4 + 2] = 0x01D4;
        t[4 * 4 + 3] = 0x00F9;
        // v: ǖ ǘ ǚ ǜ
        t[5 * 4 + 0] = 0x01D6;
        t[5 * 4 + 1] = 0x01D8;
        t[5 * 4 + 2] = 0x01DA;
        t[5 * 4 + 3] = 0x01DC;
        return t;
    }

    static constexpr auto vowelToneTable = buildVowelToneTable();

    // Returns tone diacritic for (vowel, digit 1-4), or 0 if not a valid combination.
    static inline char16_t lookupPhoneticSymbol(const char16_t vowel, const char16_t digit) {
        int vi;
        switch (vowel) {
        case u'a':
            vi = 0;
            break;
        case u'e':
            vi = 1;
            break;
        case u'i':
            vi = 2;
            break;
        case u'o':
            vi = 3;
            break;
        case u'u':
            vi = 4;
            break;
        case u'v':
            vi = 5;
            break;
        default:
            return 0;
        }
        if (digit < u'1' || digit > u'4')
            return 0;
        return vowelToneTable[static_cast<std::size_t>(vi * 4 + (digit - u'1'))];
    }

    // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/style/_tone_rule.py
    //
    // An internal function with no header declaration: explicitly static so it stays out of the symbol
    // table -- the v1.7 lesson is that an exported symbol blocks cross-TU inlining.
    static int rightMarkIndex(const std::u16string &pinyin_no_tone) {
        // 'iou', 'uei', 'uen': marked according to the pinyin BEFORE it was restored to canonical form
        if (pinyin_no_tone.find(u"iou") != std::u16string::npos) {
            return static_cast<int>(pinyin_no_tone.find('u'));
        }
        if (pinyin_no_tone.find(u"uei") != std::u16string::npos) {
            return static_cast<int>(pinyin_no_tone.find('i'));
        }
        if (pinyin_no_tone.find(u"uen") != std::u16string::npos) {
            return static_cast<int>(pinyin_no_tone.find('u'));
        }

        // The tables below are compile-time constants of fixed length (3-4 entries): constexpr arrays rather
        // than std::vector/std::u16string, to avoid the heap allocation and the first-call guard that a
        // function-local static container brings. The ORDER must not change: this function places the tone
        // mark at the FIRST hit in table order, which is not find_first_of's semantics.

        // An 'a' is never passed over; without one, look for 'o' or 'e'
        constexpr char16_t vowels[] = {u'a', u'o', u'e'};
        for (const char16_t c : vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != std::u16string::npos) {
                return static_cast<int>(pos);
            }
        }

        // With 'i' and 'u' adjacent, the later one takes the mark
        const auto iuPos = pinyin_no_tone.find(u"iu");
        if (iuPos != std::u16string::npos) {
            return static_cast<int>(iuPos) + 1;
        }
        const auto uiPos = pinyin_no_tone.find(u"ui");
        if (uiPos != std::u16string::npos) {
            return static_cast<int>(uiPos) + 1;
        }

        // 'i', 'u', 'v', 'ü'
        constexpr char16_t other_vowels[] = {u'i', u'u', u'v', u'ü'};
        for (const char16_t c : other_vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != std::u16string::npos) {
                return static_cast<int>(pos);
            }
        }

        // 'n', 'm', 'ê'
        constexpr char16_t final_chars[] = {u'n', u'm', u'ê'};
        for (const char16_t c : final_chars) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != std::u16string::npos) {
                return static_cast<int>(pos);
            }
        }

        // When no suitable position is found, return -1 to mean there is nowhere to put a mark
        return -1;
    }

    static bool isToneNumber(const char16_t c) { return c >= u'1' && c <= u'5'; }

    static bool isPhoneticSymbol(const char16_t c) {
        static const std::u16string vowels = u"aeiouüv";
        return vowels.find(c) != std::u16string::npos;
    }

    static std::u16string toneToTone(const std::u16string &tone2) {
        // Replace "ü" with "v" and drop any 5 or 0
        std::u16string string;
        string.reserve(tone2.size());
        for (const char16_t c : tone2) {
            if (c != u'5' && c != u'0')
                string += (c == u'ü') ? u'v' : c;
        }

        std::u16string result;
        result.reserve(string.size());

        size_t pos = 0;
        while (pos < string.size()) {
            const char16_t &currentChar = string[pos];
            if (isPhoneticSymbol(currentChar)) {
                if (pos + 1 < string.size() && isToneNumber(string[pos + 1])) {
                    const char16_t mapped = lookupPhoneticSymbol(currentChar, string[pos + 1]);
                    if (mapped != 0) {
                        result += mapped;
                        pos += 2;
                    } else {
                        result += currentChar;
                        pos++;
                    }
                } else {
                    result += currentChar;
                    pos++;
                }
            } else {
                result += currentChar;
                pos++;
            }
        }

        // Replace any remaining ü with v
        for (char16_t &c : result) {
            if (c == u'ü')
                c = u'v';
        }

        return result;
    }

    static std::u16string tone3ToTone2(const std::u16string &pinyin) {
        // An empty string must return early: pinyin.substr(size()-1) below would be substr(SIZE_MAX) on an
        // empty string, throwing std::out_of_range -- and an exception crossing this boundary is not allowed.
        // (The rule was written down as decision D2 of the old REFACTORING ledger, which is retired; the rule
        // itself survives as "invalid input must not throw" in docs/DECISIONS.md section 4.1.) A word
        // list never carries an empty reading, but tone3ToTone is an exported function and can be called
        // from outside.
        if (pinyin.empty())
            return {};

        const auto no_number_tone3 =
            pinyin.size() > 1 && isToneNumber(pinyin.back()) ? pinyin.substr(0, pinyin.size() - 1) : pinyin;
        auto mark_index = rightMarkIndex(no_number_tone3);
        if (mark_index == -1)
            mark_index = static_cast<int>(no_number_tone3.size()) - 1;

        const std::u16string before = no_number_tone3.substr(0, static_cast<std::size_t>(mark_index) + 1);
        const std::u16string after = no_number_tone3.substr(static_cast<std::size_t>(mark_index) + 1);
        const std::u16string number = pinyin.substr(pinyin.size() - 1);

        return before + number + after;
    }

    std::u16string tone3ToTone(const std::u16string &pinyin) {
        const auto tone2 = tone3ToTone2(pinyin);
        return toneToTone(tone2);
    }


}  // namespace cpp_pinyin::v2
