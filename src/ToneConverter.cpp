#include <cpp-pinyin2/core/ToneConverter.h>

namespace cpp_pinyin::v2
{
    std::u16string ToneConverter::convert(std::u16string str, const int style, const bool v_to_u,
                                          const bool neutral_tone_with_five) const {
        if (style < 0 || style >= MAX_STYLE || m_converts[static_cast<std::size_t>(style)] == nullptr) {
            return str;
        }
        return m_converts[static_cast<std::size_t>(style)](str, v_to_u, neutral_tone_with_five);
    }
}  // namespace cpp_pinyin::v2
