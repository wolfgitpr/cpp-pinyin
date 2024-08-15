#include "Jyutping.h"

namespace Pinyin
{
    PinyinResVector Jyutping::hanziToPinyin(const std::string &hans, CanTone::Style style, Error error) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, false, false);
    }

    PinyinResVector Jyutping::hanziToPinyin(const std::vector<std::string> &hans, CanTone::Style style,
                                            Error error) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, false, false);
    }
} // Pinyin
