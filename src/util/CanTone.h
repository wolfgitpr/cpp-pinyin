#ifndef CANTONECONVERTER_H
#define CANTONECONVERTER_H

#include <ToneConverter.h>

namespace Pinyin
{
    class CanTone : public ToneConverter {
    public:
        enum class Style {
            // 普通风格，不带声调。如： 中国 -> ``zung gwok``
            NORMAL = 0,
            // 声调风格3，即拼音声调在各个拼音之后，用数字 [1-4] 进行表示。如： 中国 -> ``zung1 gwok3``
            TONE3 = 8
        };

        CanTone():
            ToneConverter() {
            m_converts.insert({static_cast<int>(Style::NORMAL), tone3ToNormal});
        };
        ~CanTone() override = default;

        static u8string tone3ToNormal(const u8string &pinyin, bool v_to_u = false, bool neutral_tone_with_five = false);
    };
} // Pinyin

#endif //CANTONECONVERTER_H
