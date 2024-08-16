#include <cpp-pinyin/Pinyin.h>

namespace Pinyin
{
    PinyinResVector Pinyin::hanziToPinyin(const std::string &hans, ManTone::Style style,
                                          Error error, bool v_to_u, bool neutral_tone_with_five) const {
        /*
            @param hans : raw utf-8 std::string.
            @param ManTone::Style : Preserve the pinyin tone.
            @param errorType : Ignore words that have failed conversion. Default: Keep original.
            @param v_to_u : Convert v to ü. Default: false.
            @param neutral_tone_with_five : Use 5 as neutral tone. Default: false.
            @return PinyinResVector.
        */
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector Pinyin::hanziToPinyin(const std::vector<std::string> &hans, ManTone::Style style,
                                          Error error, bool v_to_u, bool neutral_tone_with_five) const {
        /*
            @param hans : raw utf-8 std::string vector, each element of the vector is a character.
            @param ManTone::Style : Preserve the pinyin tone.
            @param errorType : Ignore words that have failed conversion. Default: Keep original.
            @param v_to_u : Convert v to ü. Default: false.
            @param neutral_tone_with_five : Use 5 as neutral tone. Default: false.
            @return PinyinResVector.
        */
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, v_to_u, neutral_tone_with_five);
    }

    //  Convert to Simplified Chinese.  utf-8 std::string
    std::vector<std::string> Pinyin::getDefaultPinyin(const std::string &hanzi, ManTone::Style style,
                                                      bool v_to_u, bool neutral_tone_with_five) const {
        return ChineseG2p::getDefaultPinyin(hanzi, static_cast<int>(style), v_to_u, neutral_tone_with_five);
    }

}
