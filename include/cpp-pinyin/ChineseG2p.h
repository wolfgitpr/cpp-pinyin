#ifndef ChineseG2p_H
#define ChineseG2p_H

#include <filesystem>
#include <memory>

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/PinyinRes.h>
#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    enum CPP_PINYIN_EXPORT Error {
        // Keep original characters
        Default = 0,
        // Ignore this character (do not export)
        Ignore = 1
    };

    class ChineseG2pPrivate;

    class CPP_PINYIN_EXPORT ChineseG2p {
    public:
        explicit ChineseG2p(const std::string &language);

        ~ChineseG2p();

        bool initialized() const;

        bool loadUserDict(const std::filesystem::path &filePath) const;

        void setToneConverter(const ToneConverter &toneConverter) const;

        std::string tradToSim(const std::string &hanzi) const;

        bool isPolyphonic(const std::string &hanzi) const;

    protected:
        PinyinResVector hanziToPinyin(const std::string &hans, int style = 0,
                                      Error error = Default, bool candidates = true, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                                      int style = 0, Error error = Default,
                                      bool candidates = true, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;

        std::vector<std::string> getDefaultPinyin(const std::string &text, int style = 0, bool v_to_u = false,
                                                  bool neutral_tone_with_five = false) const;

        std::unique_ptr<ChineseG2pPrivate> d_ptr;

    private:
        PinyinResVector hanziToPinyin(const u32strVec &hans, int style = 0,
                                      Error error = Default, bool candidates = true, bool v_to_u = false,
                                      bool neutral_tone_with_five = false) const;
    };
}

#endif // ChineseG2p_H
