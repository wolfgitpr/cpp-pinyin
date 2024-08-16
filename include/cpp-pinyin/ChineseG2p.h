#ifndef ChineseG2p_H
#define ChineseG2p_H

#include <memory>

#include <cpp-pinyin/PinyinRes.h>
#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    enum class Error {
        // Keep original characters
        Default = 0,
        // Ignore this character (do not export)
        Ignore = 1
    };

    class ChineseG2pPrivate;

    class ChineseG2p {
    public:
        explicit ChineseG2p(const std::string &language, ToneConverter *toneConverter = nullptr);

        ~ChineseG2p();

        bool initialized() const;

        bool loadUserDict(const std::string &filePath) const;

        std::string tradToSim(const std::string &hanzi) const;

        bool isPolyphonic(const std::string &hanzi) const;

    protected:
        PinyinResVector hanziToPinyin(const std::string &hans, int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        std::vector<std::string> getDefaultPinyin(const std::string &text, int style = 0, bool v_to_u = false,
                                                  bool neutral_tone_with_five = false) const;

        explicit ChineseG2p(ChineseG2pPrivate &d);

        std::unique_ptr<ChineseG2pPrivate> d_ptr;

    private:
        PinyinResVector hanziToPinyin(const u32strVec &hans, int style = 0,
                                      Error error = Error::Default, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;
    };
}

#endif // ChineseG2p_H
