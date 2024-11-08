#include <cpp-pinyin/CanTone.h>

namespace Pinyin
{
    u16str CanTone::tone3ToNormal(const u16str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        return {pinyin.begin(), pinyin.end() - 1};
    }
} // Pinyin
