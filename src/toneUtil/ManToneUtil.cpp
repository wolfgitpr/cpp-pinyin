#include "ManToneUtil.h"

namespace Pinyin
{
    // 定义 phonetic_symbol_reverse 映射表
    static const u32strHashMap<u32str, char32_t> phoneticSymbolReverse = {
        {U"a1", U'ā'}, {U"a2", U'á'}, {U"a3", U'ǎ'}, {U"a4", U'à'},
        {U"e1", U'ē'}, {U"e2", U'é'}, {U"e3", U'ě'}, {U"e4", U'è'},
        {U"i1", U'ī'}, {U"i2", U'í'}, {U"i3", U'ǐ'}, {U"i4", U'ì'},
        {U"o1", U'ō'}, {U"o2", U'ó'}, {U"o3", U'ǒ'}, {U"o4", U'ò'},
        {U"u1", U'ū'}, {U"u2", U'ú'}, {U"u3", U'ǔ'}, {U"u4", U'ù'},
        {U"v1", U'ǖ'}, {U"v2", U'ǘ'}, {U"v3", U'ǚ'}, {U"v4", U'ǜ'},
    };

    // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/style/_tone_rule.py
    int rightMarkIndex(const u32str &pinyin_no_tone) {
        // 'iou', 'uei', 'uen': 根据还原前的拼音进行标记
        if (pinyin_no_tone.find(U"iou") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }
        if (pinyin_no_tone.find(U"uei") != std::string::npos) {
            return pinyin_no_tone.find('i');
        }
        if (pinyin_no_tone.find(U"uen") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }

        // 有 'a' 不放过, 没 'a' 找 'o'、'e'
        static const std::vector<char32_t> vowels = {U'a', U'o', U'e'};
        for (const char32_t c : vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u32str::npos) {
                return pos;
            }
        }

        // 'i'、'u' 若是连在一起，谁在后面就标谁
        static const u32strVec combos = {U"iu", U"ui"};
        for (const u32str &combo : combos) {
            const auto pos = pinyin_no_tone.find(combo);
            if (pos != u32str::npos) {
                return pos + 1;
            }
        }

        // 'i'、'u'、'v'、'ü'
        static const std::vector<char32_t> other_vowels = {U'i', U'u', U'v', U'ü'};
        for (const char32_t c : other_vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u32str::npos) {
                return pos;
            }
        }

        // 'n', 'm', 'ê'
        static const std::vector<char32_t> final_chars = {U'n', U'm', U'ê'};
        for (const char32_t c : final_chars) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u32str::npos) {
                return pos;
            }
        }

        // 如果没有找到合适的位置，则返回-1表示没有可以标记的位置
        return -1;
    }

    static bool isToneNumber(char32_t c) {
        return c >= U'0' && c <= U'4';
    }

    static bool isPhoneticSymbol(char32_t c) {
        return u32str(U"aeiouüv").find(c) != u32str::npos;
    }

    static u32str toneToTone(const u32str &tone2) {
        // 替换 "ü" 为 "v" 并去掉 5 和 0
        u32str string;
        for (const char32_t c : tone2)
            string += c == U'ü' ? U'v' : c;

        string.erase(std::remove(string.begin(), string.end(), U'5'), string.end());
        string.erase(std::remove(string.begin(), string.end(), U'0'), string.end());

        std::vector<char32_t> result;

        int pos = 0;
        while (pos < string.size()) {
            const char32_t &currentChar = string[pos];
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

        u32str result_str;
        for (const char32_t c : result)
            result_str += c == U'ü' ? U'v' : c;

        return result_str;
    }

    static u32str tone3ToTone2(const u32str &pinyin, bool v_to_u = false) {
        const auto no_number_tone3 = pinyin.substr(0, pinyin.size() - 1);
        auto mark_index = rightMarkIndex(no_number_tone3);
        if (mark_index == -1)
            mark_index = no_number_tone3.size() - 1;

        const u32str before = no_number_tone3.substr(0, mark_index + 1);
        const u32str after = no_number_tone3.substr(mark_index + 1);
        const u32str number = pinyin.substr(pinyin.size() - 1);

        return before + number + after;
    }

    u32str tone3ToTone(const u32str &pinyin) {
        const auto tone2 = tone3ToTone2(pinyin, true);
        return toneToTone(tone2);
    }


} // Pinyin
