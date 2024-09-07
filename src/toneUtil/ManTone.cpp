#include <algorithm>
#include <unordered_map>

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/ManTone.h>

namespace Pinyin
{
    // 映射表，音调符号 -> (无音调元音, 对应的调号)
    static const std::unordered_map<char32_t, std::pair<char32_t, char32_t>> toneToNum = {
        {U'ā', {U'a', U'1'}}, {U'á', {U'a', U'2'}}, {U'ǎ', {U'a', U'3'}}, {U'à', {U'a', U'4'}},
        {U'ō', {U'o', U'1'}}, {U'ó', {U'o', U'2'}}, {U'ǒ', {U'o', U'3'}}, {U'ò', {U'o', U'4'}},
        {U'ē', {U'e', U'1'}}, {U'é', {U'e', U'2'}}, {U'ě', {U'e', U'3'}}, {U'è', {U'e', U'4'}},
        {U'ī', {U'i', U'1'}}, {U'í', {U'i', U'2'}}, {U'ǐ', {U'i', U'3'}}, {U'ì', {U'i', U'4'}},
        {U'ū', {U'u', U'1'}}, {U'ú', {U'u', U'2'}}, {U'ǔ', {U'u', U'3'}}, {U'ù', {U'u', U'4'}},
        {U'ǖ', {U'v', U'1'}}, {U'ǘ', {U'v', U'2'}}, {U'ǚ', {U'v', U'3'}}, {U'ǜ', {U'v', U'4'}},
        {U'ń', {U'n', U'2'}}, {U'ň', {U'n', U'3'}}, {U'ǹ', {U'n', U'4'}},
        {U'ḿ', {U'm', U'2'}}
    };

    u32str ManTone::toneToNormal(const u32str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u32str result;
        result.reserve(pinyin.size());

        for (char32_t ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto it = toneToNum.find(ch);
                result += it != toneToNum.end() ? it->second.first : ch;
            }
        }

        if (v_to_u)
            std::replace(result.begin(), result.end(), U'v', U'ü');

        return result;
    }

    u32str ManTone::toneToTone(const u32str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        if (v_to_u)
            return pinyin;

        u32str result;
        result.reserve(pinyin.size());

        for (const char32_t &ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                result += ch == U'ü' ? U'v' : ch;
            }
        }

        return result;
    }

    u32str ManTone::toneToTone2(const u32str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u32str result;
        result.reserve(pinyin.size() + 1);

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


    u32str ManTone::toneToTone3(const u32str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u32str result;
        result.reserve(pinyin.size() + 1);

        char32_t toneNumber = U'5';

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

        if (!neutral_tone_with_five && toneNumber == U'5')
            result = result.substr(0, result.length() - 1);
        return result;
    }
} // Pinyin
