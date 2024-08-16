#include <cpp-pinyin/CanTone.h>

namespace Pinyin
{
    u32str CanTone::tone3ToNormal(const u32str &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        return {pinyin.begin(), pinyin.end() - 1};
    }
} // Pinyin
