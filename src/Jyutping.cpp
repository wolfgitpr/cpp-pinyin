#include <cpp-pinyin/Jyutping.h>

namespace Pinyin
{
    PinyinResVector Jyutping::hanziToPinyin(const std::string &hans, CanTone::Style style, Error error,
                                            bool candidates) const {
        /*
            @param hans : raw utf-8 std::string.
            @param ManTone::Style : Preserve the pinyin tone.
            @param errorType : Ignore words that have failed conversion. Default: Keep original.
            @param candidates : Return all possible pinyin candidates. Default: true.
            @return PinyinResVector.
        */
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, candidates, false, false);
    }

    PinyinResVector Jyutping::hanziToPinyin(const std::vector<std::string> &hans, CanTone::Style style,
                                            Error error, bool candidates) const {
        /*
            @param hans : raw utf-8 std::string vector, each element of the vector is a character.
            @param ManTone::Style : Preserve the pinyin tone.
            @param errorType : Ignore words that have failed conversion. Default: Keep original.
            @param candidates : Return all possible pinyin candidates. Default: true.
            @return PinyinResVector.
        */
        return ChineseG2p::hanziToPinyin(hans, static_cast<int>(style), error, candidates, false, false);
    }

    //  Convert to Simplified Chinese.  utf-8 std::string
    std::vector<std::string> Jyutping::getDefaultPinyin(const std::string &hanzi, CanTone::Style style) const {
        return ChineseG2p::getDefaultPinyin(hanzi, static_cast<int>(style), false, false);
    }
} // Pinyin
