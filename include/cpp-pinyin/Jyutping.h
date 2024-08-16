#ifndef DATASET_TOOLS_CANTONESE_H
#define DATASET_TOOLS_CANTONESE_H

#include "ChineseG2p.h"
#include <cpp-pinyin/CanTone.h>

namespace Pinyin
{
    class Jyutping final : public ChineseG2p {
    public:
        explicit Jyutping() :
            ChineseG2p("cantonese", new CanTone()) {};
        ~Jyutping() = default;

        PinyinResVector hanziToPinyin(const std::string &hans,
                                      CanTone::Style style = CanTone::Style::TONE3,
                                      Error error = Error::Default) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      CanTone::Style style = CanTone::Style::TONE3,
                                      Error error = Error::Default) const;
        std::vector<std::string> getDefaultPinyin(const std::string &hanzi,
                                                  CanTone::Style style = CanTone::Style::TONE3) const;
    };
}
#endif // DATASET_TOOLS_CANTONESE_H