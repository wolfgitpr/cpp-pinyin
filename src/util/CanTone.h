#ifndef CANTONECONVERTER_H
#define CANTONECONVERTER_H

#include <ToneConverter.h>

namespace Pinyin
{
    class CanTone : public ToneConverter {
    public:
        enum class Style {
            // 普通风格，不带声调。如： 中国 -> ``zhong guo``
            NORMAL = 0,
            // 标准声调风格，拼音声调在韵母第一个字母上（默认风格）。如： 中国 -> ``zhōng guó``
            TONE = 1,
        };

        CanTone():
            ToneConverter() {};
        ~CanTone() override = default;
    };
} // Pinyin

#endif //CANTONECONVERTER_H
