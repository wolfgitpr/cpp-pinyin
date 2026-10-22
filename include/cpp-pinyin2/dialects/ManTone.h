#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    class CPP_PINYIN2_EXPORT ManTone final : public ToneConverter {
    public:
        /// The nine Mandarin output styles. An alias, not a second definition: the values and their
        /// per-style comments live once, in Types.h. `ManTone::Style::TONE3` keeps working; the old
        /// unscoped spelling (`ManTone::TONE3`) no longer exists -- nothing in this repository used
        /// it (2026-10-01; the style enums collapsed into Types.h).
        using Style = MandarinStyle;

        ManTone() {
            m_converts[static_cast<int>(Style::NORMAL)] = toneToNormal;
            m_converts[static_cast<int>(Style::TONE)] = toneToTone;
            m_converts[static_cast<int>(Style::TONE2)] = toneToTone2;
            m_converts[static_cast<int>(Style::FIRST_LETTER)] = toneToFirstLetter;
            m_converts[static_cast<int>(Style::BOPOMOFO)] = toneToBopomofo;
            m_converts[static_cast<int>(Style::INITIALS)] = toneToInitials;
            m_converts[static_cast<int>(Style::FINALS)] = toneToFinals;
            m_converts[static_cast<int>(Style::FINALS_TONE3)] = toneToFinals3;
            m_converts[static_cast<int>(Style::TONE3)] = toneToTone3;
            // An external dictionary (user_dict.txt) uses tone3 while the engine stores the toned form, so
            // this hook must be registered PER DIALECT -- otherwise a Cantonese dictionary would be rewritten
            // by the Mandarin rules too (see the comment on ManTone::normalizeReading).
            m_normalize = &ManTone::normalizeReading;
        }

        ~ManTone() override = default;

        static std::u16string toneToNormal(const std::u16string &pinyin, bool v_to_u = false,
                                           bool neutral_tone_with_five = false);

        static std::u16string toneToTone(const std::u16string &pinyin, bool v_to_u = false,
                                         bool neutral_tone_with_five = false);

        static std::u16string toneToTone2(const std::u16string &pinyin, bool v_to_u = false,
                                          bool neutral_tone_with_five = false);

        static std::u16string toneToFirstLetter(const std::u16string &pinyin, bool v_to_u = false,
                                                bool neutral_tone_with_five = false);

        static std::u16string toneToBopomofo(const std::u16string &pinyin, bool v_to_u = false,
                                             bool neutral_tone_with_five = false);

        // The initial. The input is a toned reading, like the other toneToX functions: u"zhōng" -> u"zh".
        // A zero-initial syllable returns an empty string; by 《汉语拼音方案》 y/w/yu are not initials.
        // Aligned case by case with pypinyin's Style.INITIALS (strict=True); the comparison footing is
        // stated below.
        static std::u16string toneToInitials(const std::u16string &pinyin, bool v_to_u = false,
                                             bool neutral_tone_with_five = false);

        // The final, toneless, in its canonical form: yi->i, ya->ia, you->iou, yu->v, jue->ve, gui->uei,
        // lun->uen, zhi->i. With v_to_u=true ü is written as ü (the default is v).
        // Aligned case by case with pypinyin's Style.FINALS (strict=True).
        static std::u16string toneToFinals(const std::u16string &pinyin, bool v_to_u = false,
                                           bool neutral_tone_with_five = false);

        // The final plus a trailing tone number: u"zhōng" -> u"ong1". When the original reading has no tone,
        // neutral_tone_with_five decides whether a 5 is appended (the same footing as TONE3's neutral tone).
        static std::u16string toneToFinals3(const std::u16string &pinyin, bool v_to_u = false,
                                            bool neutral_tone_with_five = false);

        // ! The measured footing behind "aligned with pypinyin" above (2026-09-23): of a 115-case reference
        // table, 113 cases are byte-for-byte identical and only the two ê-series interjection spellings
        // (ê+U+0304 and ế) differ -- pypinyin treats them as a toned ê, while this library makes no special
        // case (lookupTone has no ê entry) and returns an empty string. Every word list under data/dialects
        // was checked and none carries an ê-family or combining-mark character, so the library's own data
        // and the frozen dump are unaffected. The per-case evidence is the regression test
        // tests/cpp/test_style_convert.cpp.

        static std::u16string toneToTone3(const std::u16string &pinyin, bool v_to_u = false,
                                          bool neutral_tone_with_five = false);

        /// External readings in Mandarin dictionaries are written in tone3 form
        /// (``user_dict.txt``: ``陟罚臧否:zhi4 fa2 zang2 pi3``) while the engine stores the
        /// tone-marked form (``zhì``), so this hook is ``tone3ToTone``. Registered as
        /// ``ToneConverter::m_normalize`` by the constructor.
        [[nodiscard]] static std::u16string normalizeReading(const std::u16string &pinyin);
    };


}  // namespace cpp_pinyin::v2
