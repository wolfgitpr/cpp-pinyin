#include "ManToneUtil.h"

#include <iostream>
#include <ostream>
#include <regex>

namespace Pinyin
{
    // 定义 phonetic_symbol_reverse 映射表
    static const std::unordered_map<u8string, char32_t> phoneticSymbolReverse = {
        {u8"a1", U'ā'}, {u8"a2", U'á'}, {u8"a3", U'ǎ'}, {u8"a4", U'à'},
        {u8"e1", U'ē'}, {u8"e2", U'é'}, {u8"e3", U'ě'}, {u8"e4", U'è'},
        {u8"i1", U'ī'}, {u8"i2", U'í'}, {u8"i3", U'ǐ'}, {u8"i4", U'ì'},
        {u8"o1", U'ō'}, {u8"o2", U'ó'}, {u8"o3", U'ǒ'}, {u8"o4", U'ò'},
        {u8"u1", U'ū'}, {u8"u2", U'ú'}, {u8"u3", U'ǔ'}, {u8"u4", U'ù'},
        {u8"v1", U'ǖ'}, {u8"v2", U'ǘ'}, {u8"v3", U'ǚ'}, {u8"v4", U'ǜ'},
    };

    // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/style/_tone_rule.py
    int rightMarkIndex(const u8string &pinyin_no_tone) {
        // 'iou', 'uei', 'uen': 根据还原前的拼音进行标记
        if (pinyin_no_tone.find("iou") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }
        if (pinyin_no_tone.find("uei") != std::string::npos) {
            return pinyin_no_tone.find('i');
        }
        if (pinyin_no_tone.find("uen") != std::string::npos) {
            return pinyin_no_tone.find('u');
        }

        // 有 'a' 不放过, 没 'a' 找 'o'、'e'
        static const std::vector<char32_t> vowels = {U'a', U'o', U'e'};
        for (const char32_t c : vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u8string::npos) {
                return pos;
            }
        }

        // 'i'、'u' 若是连在一起，谁在后面就标谁
        static const u8stringlist combos = {U"iu", U"ui"};
        for (const u8string &combo : combos) {
            const auto pos = pinyin_no_tone.find(combo);
            if (pos != u8string::npos) {
                return pos + 1;
            }
        }

        // 'i'、'u'、'v'、'ü'
        static const std::vector<char32_t> other_vowels = {U'i', U'u', U'v', U'ü'};
        for (const char32_t c : other_vowels) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u8string::npos) {
                return pos;
            }
        }

        // 'n', 'm', 'ê'
        static const std::vector<char32_t> final_chars = {U'n', U'm', U'ê'};
        for (const char32_t c : final_chars) {
            const auto pos = pinyin_no_tone.find(c);
            if (pos != u8string::npos) {
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
        return u8string("aeiouüv").find(c) != u8string::npos;
    }

    static u8string toneToTone(const u8string &tone2) {
        // 替换 "ü" 为 "v" 并去掉 5 和 0
        u8string string;
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

        u8string result_str;
        for (const char32_t c : result)
            result_str += c == U'ü' ? U'v' : c;

        return result_str;
    }

    static u8string tone3ToTone2(const u8string &pinyin, bool v_to_u = false) {
        const auto no_number_tone3 = pinyin.substr(0, pinyin.size() - 1);
        auto mark_index = rightMarkIndex(no_number_tone3);
        if (mark_index == -1)
            mark_index = no_number_tone3.size() - 1;

        const u8string before = no_number_tone3.substr(0, mark_index + 1);
        const u8string after = no_number_tone3.substr(mark_index + 1);
        const u8string number = pinyin.substr(pinyin.size() - 1);

        return before + number + after;
    }

    u8string tone3ToTone(const u8string &pinyin) {
        const auto tone2 = tone3ToTone2(pinyin, true);
        return toneToTone(tone2);
    }


} // Pinyin
