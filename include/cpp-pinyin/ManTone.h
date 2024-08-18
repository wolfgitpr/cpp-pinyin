#ifndef TONEUTIL_H
#define TONEUTIL_H

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    class CPP_PINYIN_EXPORT ManTone final : public ToneConverter {
    public:
        // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/constants.py
        enum Style {
            // 普通风格，不带声调。如： 中国 -> ``zhong guo``
            NORMAL = 0,
            // 标准声调风格，拼音声调在韵母第一个字母上（默认风格）。如： 中国 -> ``zhōng guó``
            TONE = 1,
            // 声调风格2，即拼音声调在各个韵母之后，用数字 [1-4] 进行表示。如： 中国 -> ``zho1ng guo2``
            TONE2 = 2,
            // 声调风格3，即拼音声调在各个拼音之后，用数字 [1-4] 进行表示。如： 中国 -> ``zhong1 guo2``
            TONE3 = 8
        };

        ManTone():
            ToneConverter() {
            m_converts.insert({static_cast<int>(Style::NORMAL), toneToNormal});
            m_converts.insert({static_cast<int>(Style::TONE), toneToTone});
            m_converts.insert({static_cast<int>(Style::TONE2), toneToTone2});
            m_converts.insert({static_cast<int>(Style::TONE3), toneToTone3});
        };
        ~ManTone() override = default;

        static u32str toneToNormal(const u32str &pinyin, bool v_to_u = false, bool neutral_tone_with_five = false);

        static u32str toneToTone(const u32str &pinyin, bool v_to_u = false, bool neutral_tone_with_five = false);

        static u32str toneToTone2(const u32str &pinyin, bool v_to_u = false, bool neutral_tone_with_five = false);

        static u32str toneToTone3(const u32str &pinyin, bool v_to_u = false, bool neutral_tone_with_five = false);
    };


} // Pinyin

#endif //TONEUTIL_H
