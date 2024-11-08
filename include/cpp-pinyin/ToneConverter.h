#ifndef TUNEUTIL_H
#define TUNEUTIL_H

#include <functional>

#include <cpp-pinyin/PinyinGlobal.h>
#include <string>

namespace Pinyin
{
    class CPP_PINYIN_EXPORT ToneConverter {
    public:
        enum Style {
            // 普通风格，不带声调。如： 中国 -> ``zhong guo``
            NORMAL = 0,
            // 标准声调风格，拼音声调在韵母第一个字母上（默认风格）。如： 中国 -> ``zhōng guó``
            TONE = 1
        };

        ToneConverter() {}
        virtual ~ToneConverter() {}

        std::u16string convert(std::u16string str, int style, bool v_to_u = false, bool neutral_tone_with_five = false) const;

    protected:
        std::unordered_map<int, std::function<std::u16string(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_five)>>
        m_converts;
    };
}

#endif //TUNEUTIL_H
