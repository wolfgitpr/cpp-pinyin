#ifndef TUNEUTIL_H
#define TUNEUTIL_H

#include <ostream>
#include <tinyutf8.h>

typedef tiny_utf8::string u8string;

class ToneConverter {
public:
    enum class Style {
        // 普通风格，不带声调。如： 中国 -> ``zhong guo``
        NORMAL = 0,
        // 标准声调风格，拼音声调在韵母第一个字母上（默认风格）。如： 中国 -> ``zhōng guó``
        TONE = 1
    };

    ToneConverter() {}
    virtual ~ToneConverter() {}

    inline u8string convert(u8string str, int style, bool v_to_u = false,
                            bool neutral_tone_with_five = false) {
        const auto it = m_converts.find(style);

        if (it == m_converts.end()) {
            return str;
        }
        return it->second(str, v_to_u, neutral_tone_with_five);
    }

    std::unordered_map<int, std::function<u8string(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five)>>
    m_converts;
};

#endif //TUNEUTIL_H
