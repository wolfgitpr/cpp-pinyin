#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    u16str ToneConverter::convert(u16str str, int style, bool v_to_u, bool neutral_tone_with_five) const {
        const auto it = m_converts.find(style);

        if (it == m_converts.end()) {
            return str;
        }
        return it->second(str, v_to_u, neutral_tone_with_five);
    }
}
