#include "Pinyin.h"

namespace Pinyin
{
    PinyinResVector Pinyin::hanziToPinyin(const std::string &hans, ManTone::Style style,
                                          Error error, bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector Pinyin::hanziToPinyin(const std::vector<std::string> &hans, ManTone::Style style,
                                          Error error, bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }

    std::vector<std::string> Pinyin::getDefaultPinyin(const std::string &text, ManTone::Style style,
                                                      bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::getDefaultPinyin(text, static_cast<int>(style));
    }

}
