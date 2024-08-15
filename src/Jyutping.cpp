#include "Jyutping.h"

namespace Pinyin
{
    PinyinResVector Jyutping::hanziToPinyin(const std::string &hans, CanTone::Style style,
                                            Error error, bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector Jyutping::hanziToPinyin(const std::vector<std::string> &hans, CanTone::Style style,
                                            Error error, bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }
} // Pinyin
