#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    class CPP_PINYIN2_EXPORT CanTone final : public ToneConverter {
    public:
        /// The three Cantonese output styles, aliased to Types.h's single authority (2026-10-01).
        /// Cantonese has no tone-marked form, so there is nothing between NORMAL and TONE3 to add.
        using Style = CantoneseStyle;

        CanTone() {
            m_converts[static_cast<int>(Style::NORMAL)] = tone3ToNormal;
            m_converts[static_cast<int>(Style::FIRST_LETTER)] = tone3ToFirstLetter;
        }

        ~CanTone() override = default;

        static std::u16string tone3ToNormal(const std::u16string &pinyin, bool v_to_u = false,
                                            bool neutral_tone_with_five = false);

        static std::u16string tone3ToFirstLetter(const std::u16string &pinyin, bool v_to_u = false,
                                                 bool neutral_tone_with_five = false);
    };
}  // namespace cpp_pinyin::v2
