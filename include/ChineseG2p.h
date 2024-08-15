#ifndef ChineseG2p_H
#define ChineseG2p_H

#include "G2pglobal.h"

#include "PinyinRes.h"
#include "ToneConverter.h"

namespace Pinyin
{
    enum class Error {
        // 保留原字符
        Default = 0,
        // 忽略该字符
        Ignore = 1
    };

    class ChineseG2pPrivate;

    class ChineseG2p {
    public:
        explicit ChineseG2p(const std::string &language, ToneConverter *toneConverter = nullptr);

        ~ChineseG2p();

        bool initialized() const;

        std::string tradToSim(const std::string &text) const;

        bool isPolyphonic(const std::string &text) const;

    protected:
        PinyinResVector hanziToPinyin(const std::string &hans, int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        std::vector<std::string> getDefaultPinyin(const std::string &text, int style = 0) const;

        explicit ChineseG2p(ChineseG2pPrivate &d);

        std::unique_ptr<ChineseG2pPrivate> d_ptr;

    private:
        PinyinResVector hanziToPinyin(const u8stringlist &hans, int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;
    };
}

#endif // ChineseG2p_H
