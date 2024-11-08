#include "ManToneUtil.h"

#include <algorithm>

namespace Pinyin
{
    // 定义 phonetic_symbol_reverse 映射表
    static const u16strHashMap<u16str, char16_t> phoneticSymbolReverse = {
        {u"a1", u'ā'}, {u"a2", u'á'}, {u"a3", u'ǎ'}, {u"a4", u'à'},
        {u"e1", u'ē'}, {u"e2", u'é'}, {u"e3", u'ě'}, {u"e4", u'è'},
        {u"i1", u'ī'}, {u"i2", u'í'}, {u"i3", u'ǐ'}, {u"i4", u'ì'},
        {u"o1", u'ō'}, {u"o2", u'ó'}, {u"o3", u'ǒ'}, {u"o4", u'ò'},
        {u"u1", u'ū'}, {u"u2", u'ú'}, {u"u3", u'ǔ'}, {u"u4", u'ù'},
        {u"v1", u'ǖ'}, {u"v2", u'ǘ'}, {u"v3", u'ǚ'}, {u"v4", u'ǜ'},
    };

    // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/style/_tone_rule.py
    int rightMarkIndex(const u16str &pinyin_no_tone) {
        // 'iou', 'uei', 'uen': 根据还原前的拼音进行标记
        if (pinyin_no_tone.find(u"iou") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }
        if (pinyin_no_tone.find(u"uei") != std::string::npos) {
            return pinyin_no_tone.find('i');
        }
        if (pinyin_no_tone.find(u"uen") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }

        // 有 'a' 不放过, 没 'a' 找 'o'、'e'
        static const std::vector<char16_t> vowels = {u'a', u'o', u'e'};
        for (const char16_t c : vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u16str::npos) {
                return pos;
            }
        }

        // 'i'、'u' 若是连在一起，谁在后面就标谁
        static const u16strVec combos = {u"iu", u"ui"};
        for (const u16str &combo : combos) {
            const auto pos = pinyin_no_tone.find(combo);
            if (pos != u16str::npos) {
                return pos + 1;
            }
        }

        // 'i'、'u'、'v'、'ü'
        static const std::vector<char16_t> other_vowels = {u'i', u'u', u'v', u'ü'};
        for (const char16_t c : other_vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u16str::npos) {
                return pos;
            }
        }

        // 'n', 'm', 'ê'
        static const std::vector<char16_t> final_chars = {u'n', u'm', u'ê'};
        for (const char16_t c : final_chars) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u16str::npos) {
                return pos;
            }
        }

        // 如果没有找到合适的位置，则返回-1表示没有可以标记的位置
        return -1;
    }

    static bool isToneNumber(const char16_t c) {
        return c >= u'0' && c <= u'4';
    }

    static bool isPhoneticSymbol(const char16_t c) {
        return u16str(u"aeiouüv").find(c) != u16str::npos;
    }

    static u16str toneToTone(const u16str &tone2) {
        // 替换 "ü" 为 "v" 并去掉 5 和 0
        u16str string;
        for (const char16_t c : tone2)
            string += c == u'ü' ? u'v' : c;

        string.erase(std::remove(string.begin(), string.end(), u'5'), string.end());
        string.erase(std::remove(string.begin(), string.end(), u'0'), string.end());

        std::vector<char16_t> result;

        int pos = 0;
        while (pos < string.size()) {
            const char16_t &currentChar = string[pos];
            if (isPhoneticSymbol(currentChar)) {
                if (pos + 1 < string.length() && isToneNumber(string[pos + 1])) {
                    const auto str = string.substr(pos, 2);
                    const auto it = phoneticSymbolReverse.find(str);
                    if (it != phoneticSymbolReverse.end()) {
                        result.emplace_back(it->second);
                        pos += 2;
                    } else {
                        result.emplace_back(currentChar);
                        pos++;
                    }
                } else {
                    result.emplace_back(currentChar);
                    pos++;
                }
            } else {
                result.emplace_back(currentChar);
                pos++;
            }
        }

        u16str result_str;
        for (const char16_t c : result)
            result_str += c == u'ü' ? u'v' : c;

        return result_str;
    }

    static u16str tone3ToTone2(const u16str &pinyin) {
        const auto no_number_tone3 = pinyin.substr(0, pinyin.size() - 1);
        auto mark_index = rightMarkIndex(no_number_tone3);
        if (mark_index == -1)
            mark_index = no_number_tone3.size() - 1;

        const u16str before = no_number_tone3.substr(0, mark_index + 1);
        const u16str after = no_number_tone3.substr(mark_index + 1);
        const u16str number = pinyin.substr(pinyin.size() - 1);

        return before + number + after;
    }

    u16str tone3ToTone(const u16str &pinyin) {
        const auto tone2 = tone3ToTone2(pinyin);
        return toneToTone(tone2);
    }


} // Pinyin
