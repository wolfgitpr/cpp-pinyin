#include "ManTone.h"

#include <map>

#include "G2pglobal.h"

#include <unordered_map>

namespace Pinyin
{
    // 映射表，音调符号 -> (无音调元音, 对应的调号)
    static const std::unordered_map<char32_t, std::pair<char, char>> toneToNum = {
        {U'ā', {'a', '1'}}, {U'á', {'a', '2'}}, {U'ǎ', {'a', '3'}}, {U'à', {'a', '4'}},
        {U'ō', {'o', '1'}}, {U'ó', {'o', '2'}}, {U'ǒ', {'o', '3'}}, {U'ò', {'o', '4'}},
        {U'ē', {'e', '1'}}, {U'é', {'e', '2'}}, {U'ě', {'e', '3'}}, {U'è', {'e', '4'}},
        {U'ī', {'i', '1'}}, {U'í', {'i', '2'}}, {U'ǐ', {'i', '3'}}, {U'ì', {'i', '4'}},
        {U'ū', {'u', '1'}}, {U'ú', {'u', '2'}}, {U'ǔ', {'u', '3'}}, {U'ù', {'u', '4'}},
        {U'ǖ', {'v', '1'}}, {U'ǘ', {'v', '2'}}, {U'ǚ', {'v', '3'}}, {U'ǜ', {'v', '4'}}
    };

    u8string ManTone::toneToNormal(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u8string result;

        for (char32_t ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto it = toneToNum.find(ch);
                result += it != toneToNum.end() ? it->second.first : ch;
            }
        }

        if (v_to_u)
            result = result.replace(U'v', U'ü');

        return result;
    }

    u8string ManTone::toneToTone(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u8string result;

        if (!v_to_u) {
            for (const char32_t &ch : pinyin) {
                if (isLetter(ch)) {
                    result += ch;
                } else {
                    result += ch == U'ü' ? U'v' : ch;
                }
            }
        } else
            return pinyin;

        return result;
    }

    u8string ManTone::toneToTone2(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u8string result;

        for (char32_t ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto it = toneToNum.find(ch);
                if (it != toneToNum.end()) {
                    result += it->second.first;
                    const char32_t toneNumber = it->second.second;
                    if (!(!neutral_tone_with_five && toneNumber == U'5'))
                        result += toneNumber;
                } else {
                    if (!v_to_u && ch == U'ü')
                        ch = U'v';
                    result += ch;
                }
            }
        }
        return result;
    }


    u8string ManTone::toneToTone3(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u8string result;
        char toneNumber = '5';

        for (char32_t ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto it = toneToNum.find(ch);
                if (it != toneToNum.end()) {
                    result += it->second.first;
                    toneNumber = it->second.second;
                } else {
                    if (!v_to_u && ch == U'ü')
                        ch = U'v';
                    result += ch;
                }
            }
        }

        result += toneNumber;

        if (!neutral_tone_with_five && toneNumber == '5')
            result = result.substr(0, result.length() - 1);
        return result;
    }
} // Pinyin
