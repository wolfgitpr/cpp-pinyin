#ifndef DATASET_TOOLS_MANDARIN_H
#define DATASET_TOOLS_MANDARIN_H

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/ChineseG2p.h>
#include <cpp-pinyin/ManTone.h>

namespace Pinyin
{
    class CPP_PINYIN_EXPORT Pinyin final : public ChineseG2p {
    public:
        explicit Pinyin() :
            ChineseG2p("mandarin", new ManTone()) {};
        ~Pinyin() = default;

        PinyinResVector hanziToPinyin(const std::string &hans,
                                      ManTone::Style style = ManTone::Style::TONE,
                                      Error error = Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      ManTone::Style style = ManTone::Style::TONE,
                                      Error error = Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        std::vector<std::string> getDefaultPinyin(const std::string &hanzi,
                                                  ManTone::Style style = ManTone::Style::TONE,
                                                  bool v_to_u = false, bool neutral_tone_with_five = false) const;
    };
}

#endif // DATASET_TOOLS_MANDARIN_H
