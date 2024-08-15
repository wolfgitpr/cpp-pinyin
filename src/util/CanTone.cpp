#include "CanTone.h"

namespace Pinyin
{
    u8string CanTone::tone3ToNormal(const u8string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        return pinyin.substr(0, pinyin.size() - 1);
    }
} // Pinyin
