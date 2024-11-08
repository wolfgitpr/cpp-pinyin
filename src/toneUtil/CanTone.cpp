#include <cpp-pinyin/CanTone.h>

namespace Pinyin
{
    std::u16string CanTone::tone3ToNormal(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        return {pinyin.begin(), pinyin.end() - 1};
    }
} // Pinyin
