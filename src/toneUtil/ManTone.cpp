#include <algorithm>
#include <unordered_map>

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/ManTone.h>

namespace Pinyin
{
    // 映射表，音调符号 -> (无音调元音, 对应的调号)
    static const std::unordered_map<char16_t, std::pair<char16_t, char16_t>> toneToNum = {
        {u'ā', {u'a', u'1'}}, {u'á', {u'a', u'2'}}, {u'ǎ', {u'a', u'3'}}, {u'à', {u'a', u'4'}},
        {u'ō', {u'o', u'1'}}, {u'ó', {u'o', u'2'}}, {u'ǒ', {u'o', u'3'}}, {u'ò', {u'o', u'4'}},
        {u'ē', {u'e', u'1'}}, {u'é', {u'e', u'2'}}, {u'ě', {u'e', u'3'}}, {u'è', {u'e', u'4'}},
        {u'ī', {u'i', u'1'}}, {u'í', {u'i', u'2'}}, {u'ǐ', {u'i', u'3'}}, {u'ì', {u'i', u'4'}},
        {u'ū', {u'u', u'1'}}, {u'ú', {u'u', u'2'}}, {u'ǔ', {u'u', u'3'}}, {u'ù', {u'u', u'4'}},
        {u'ǖ', {u'v', u'1'}}, {u'ǘ', {u'v', u'2'}}, {u'ǚ', {u'v', u'3'}}, {u'ǜ', {u'v', u'4'}},
        {u'ń', {u'n', u'2'}}, {u'ň', {u'n', u'3'}}, {u'ǹ', {u'n', u'4'}},
        {u'ḿ', {u'm', u'2'}}
    };

    u16str ManTone::toneToNormal(const u16str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u16str result;
        result.reserve(pinyin.size());

        for (const char16_t &ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto &it = toneToNum.find(ch);
                result += it != toneToNum.end() ? it->second.first : ch;
            }
        }

        if (!v_to_u)
            std::replace(result.begin(), result.end(), u'ü', u'v');

        return result;
    }

    u16str ManTone::toneToTone(const u16str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        if (v_to_u)
            return pinyin;

        u16str result;
        result.reserve(pinyin.size());

        for (const char16_t &ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                result += ch == u'ü' ? u'v' : ch;
            }
        }

        return result;
    }

    u16str ManTone::toneToTone2(const u16str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u16str result;
        result.reserve(pinyin.size() + 1);

        for (const char16_t &ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto &it = toneToNum.find(ch);
                if (it != toneToNum.end()) {
                    result += it->second.first;
                    const char16_t &toneNumber = it->second.second;
                    if (!(!neutral_tone_with_five && toneNumber == u'5'))
                        result += toneNumber;
                } else {
                    if (!v_to_u && ch == u'ü') {
                        result += u'v';
                        continue;
                    }
                    result += ch;
                }
            }
        }
        return result;
    }


    u16str ManTone::toneToTone3(const u16str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        u16str result;
        result.reserve(pinyin.size() + 1);

        char16_t toneNumber = u'5';

        for (const char16_t &ch : pinyin) {
            if (isLetter(ch)) {
                result += ch;
            } else {
                const auto &it = toneToNum.find(ch);
                if (it != toneToNum.end()) {
                    result += it->second.first;
                    toneNumber = it->second.second;
                } else {
                    if (!v_to_u && ch == u'ü') {
                        result += u'v';
                        continue;
                    }
                    result += ch;
                }
            }
        }

        result += toneNumber;

        if (!neutral_tone_with_five && toneNumber == u'5')
            result = result.substr(0, result.length() - 1);
        return result;
    }
} // Pinyin
