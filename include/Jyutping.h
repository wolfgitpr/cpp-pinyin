#ifndef DATASET_TOOLS_CANTONESE_H
#define DATASET_TOOLS_CANTONESE_H

#include "ChineseG2p.h"
#include "../src/util/CanTone.h"

namespace Pinyin
{
    class Jyutping final : public ChineseG2p {
    public:
        explicit Jyutping() :
            ChineseG2p("cantonese", new CanTone()) {};
        ~Jyutping() = default;

        PinyinResVector hanziToPinyin(const std::string &hans,
                                      CanTone::Style style = CanTone::Style::TONE,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      CanTone::Style style = CanTone::Style::TONE,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;
    };
}
#endif // DATASET_TOOLS_CANTONESE_H
