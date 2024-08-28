#ifndef DATASET_TOOLS_CANTONESE_H
#define DATASET_TOOLS_CANTONESE_H

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/ChineseG2p.h>
#include <cpp-pinyin/CanTone.h>

namespace Pinyin
{
    class CPP_PINYIN_EXPORT Jyutping final : public ChineseG2p {
    public:
        explicit Jyutping() :
            ChineseG2p("cantonese", new CanTone()) {};
        ~Jyutping() = default;

        PinyinResVector hanziToPinyin(const std::string &hans,
                                      CanTone::Style style = CanTone::Style::TONE3,
                                      Error error = Default, bool candidates = true) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      CanTone::Style style = CanTone::Style::TONE3,
                                      Error error = Default, bool candidates = true) const;
        std::vector<std::string> getDefaultPinyin(const std::string &hanzi,
                                                  CanTone::Style style = CanTone::Style::TONE3) const;
    };
}
#endif // DATASET_TOOLS_CANTONESE_H
