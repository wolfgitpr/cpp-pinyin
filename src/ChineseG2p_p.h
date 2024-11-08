#ifndef ChineseG2pPRIVATE_H
#define ChineseG2pPRIVATE_H

#include <unordered_map>
#include <unordered_set>

#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    class ChineseG2pPrivate final {
    public:
        explicit ChineseG2pPrivate(std::string language);
        ~ChineseG2pPrivate();

        void init();

        bool initialized = false;

        u16strHashMap<u16str, u16str> phrases_map;
        u16strHashMap<u16str, u16strVec> phrases_dict;
        u16strHashMap<u16str, u16strVec> word_dict;
        u16strHashMap<u16str, u16str> trans_dict;

        std::string m_language;
        ToneConverter m_toneConverter;

        inline bool isPolyphonic(const u16str &text) const {
            return phrases_map.find(text) != phrases_map.end();
        }

        inline u16str tradToSim(const u16str &text) const {
            const auto &it = trans_dict.find(text);
            return it != trans_dict.end() ? it->second : text;
        }

        inline u16str toneConvert(const u16str &pinyin, int style, bool v_to_u = false,
                                  bool neutral_tone_with_five = false) const {
            return m_toneConverter.convert({pinyin.begin(), pinyin.end()}, style, v_to_u, neutral_tone_with_five);
        }

        inline u16strVec toneConvert(const u16strVec &pinyin, int style, bool v_to_u = false,
                                     bool neutral_tone_with_five = false) const {
            u16strVec tonePinyin;
            tonePinyin.reserve(pinyin.size());
            for (const u16str &p : pinyin) {
                tonePinyin.push_back(toneConvert(p, style, v_to_u, neutral_tone_with_five));
            }
            return tonePinyin;
        }

        inline std::vector<std::string> getDefaultPinyin(const u16str &hanzi, int style = 0, bool v_to_u = false,
                                                         bool neutral_tone_with_five = false) const {
            const auto &it = word_dict.find(hanzi);
            if (it == word_dict.end())
                return {u16strToUtf8str(hanzi)};

            const u16strVec &candidates = it->second;
            std::vector<std::string> toneCandidates;
            toneCandidates.reserve(candidates.size());

            std::unordered_set<std::string> seen(candidates.size());

            for (const u16str &pinyin : candidates) {
                const auto &tarPinyin = u16strToUtf8str(toneConvert(pinyin, style, v_to_u, neutral_tone_with_five));
                if (seen.insert(tarPinyin).second) {
                    toneCandidates.push_back(tarPinyin);
                }
            }

            if (toneCandidates.empty())
                return {u16strToUtf8str(hanzi)};
            return toneCandidates;
        }

        void zhPosition(const u16strVec &input, u16strVec &res, std::vector<bool> &positions);
    };
}

#endif // ChineseG2pPRIVATE_H
