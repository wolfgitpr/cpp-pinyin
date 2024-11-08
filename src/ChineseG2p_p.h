#ifndef ChineseG2pPRIVATE_H
#define ChineseG2pPRIVATE_H

#include <unordered_map>
#include <unordered_set>

#include <cpp-pinyin/ToneConverter.h>

#include "cpp-pinyin/U16Str.h"

namespace Pinyin
{
    class ChineseG2pPrivate final {
    public:
        explicit ChineseG2pPrivate(std::string language);
        ~ChineseG2pPrivate();

        void init();

        bool initialized = false;

        std::unordered_map<char16_t, std::u16string> phrases_map;
        std::unordered_map<std::u16string, std::vector<std::u16string>> phrases_dict;
        std::unordered_map<char16_t, std::vector<std::u16string>> word_dict;
        std::unordered_map<char16_t, char16_t> trans_dict;

        std::string m_language;
        ToneConverter m_toneConverter;

        inline bool isPolyphonic(const char16_t &oneHanzi) const {
            return phrases_map.find(oneHanzi) != phrases_map.end();
        }

        inline char16_t tradToSim(const char16_t &oneHanzi) const {
            const auto &it = trans_dict.find(oneHanzi);
            return it != trans_dict.end() ? it->second : oneHanzi;
        }

        inline std::u16string toneConvert(const std::u16string &pinyin, int style, bool v_to_u = false,
                                          bool neutral_tone_with_five = false) const {
            return m_toneConverter.convert({pinyin.begin(), pinyin.end()}, style, v_to_u, neutral_tone_with_five);
        }

        inline std::vector<std::u16string> toneConvert(const std::vector<std::u16string> &pinyin, int style,
                                                       bool v_to_u = false,
                                                       bool neutral_tone_with_five = false) const {
            std::vector<std::u16string> tonePinyin;
            tonePinyin.reserve(pinyin.size());
            for (const std::u16string &p : pinyin) {
                tonePinyin.push_back(toneConvert(p, style, v_to_u, neutral_tone_with_five));
            }
            return tonePinyin;
        }

        inline std::vector<std::string> getDefaultPinyin(const char16_t &oneHanzi, int style = 0,
                                                         bool v_to_u = false,
                                                         bool neutral_tone_with_five = false) const {
            const auto &it = word_dict.find(oneHanzi);
            if (it == word_dict.end())
                return {u16strToUtf8str(oneHanzi)};

            const std::vector<std::u16string> &candidates = it->second;
            std::vector<std::string> toneCandidates;
            toneCandidates.reserve(candidates.size());

            std::unordered_set<std::string> seen(candidates.size());

            for (const std::u16string &pinyin : candidates) {
                const auto &tarPinyin = u16strToUtf8str(toneConvert(pinyin, style, v_to_u, neutral_tone_with_five));
                if (seen.insert(tarPinyin).second) {
                    toneCandidates.push_back(tarPinyin);
                }
            }

            if (toneCandidates.empty())
                return {u16strToUtf8str(oneHanzi)};
            return toneCandidates;
        }

        void zhPosition(const std::vector<std::u16string> &input, std::vector<char16_t> &res,
                        std::vector<bool> &positions);
    };
}

#endif // ChineseG2pPRIVATE_H
