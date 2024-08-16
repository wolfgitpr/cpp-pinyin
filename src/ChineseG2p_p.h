#ifndef ChineseG2pPRIVATE_H
#define ChineseG2pPRIVATE_H

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    class ChineseG2pPrivate final {
    public:
        explicit ChineseG2pPrivate(std::string language, ToneConverter *toneConverter);
        virtual ~ChineseG2pPrivate();

        void init();

        bool initialized = false;

        ChineseG2p *q_ptr{};

        u32strHashMap<u32str, u32str> phrases_map;
        u32strHashMap<u32str, u32strVec> phrases_dict;
        u32strHashMap<u32str, u32strVec> word_dict;
        u32strHashMap<u32str, u32str> trans_dict;

        std::string m_language;
        ToneConverter *m_toneConverter;

        inline bool isPolyphonic(const u32str &text) const {
            return phrases_map.find(text) != phrases_map.end();
        }

        inline u32str tradToSim(const u32str &text) const {
            const auto it = trans_dict.find(text);
            return it != trans_dict.end() ? it->second : text;
        }

        inline u32str toneConvert(const u32str &pinyin, int style, bool v_to_u = false,
                                  bool neutral_tone_with_five = false) const {
            return m_toneConverter->convert({pinyin.begin(), pinyin.end()}, style, v_to_u, neutral_tone_with_five);
        }

        inline u32strVec toneConvert(const u32strVec &pinyin, int style, bool v_to_u = false,
                                     bool neutral_tone_with_five = false) const {
            u32strVec tonePinyin;
            tonePinyin.reserve(pinyin.size());
            for (const u32str &p : pinyin) {
                tonePinyin.push_back(toneConvert(p, style, v_to_u, neutral_tone_with_five));
            }
            return tonePinyin;
        }

        inline std::vector<std::string> getDefaultPinyin(const u32str &hanzi, int style = 0, bool v_to_u = false,
                                                         bool neutral_tone_with_five = false) const {
            const auto it = word_dict.find(hanzi);
            if (it == word_dict.end())
                return {u32strToUtf8str(hanzi)};

            const u32strVec &candidates = it->second;
            std::vector<std::string> toneCandidates;
            toneCandidates.reserve(candidates.size());

            std::unordered_set<std::string> seen(candidates.size());

            for (const u32str &pinyin : candidates) {
                const auto tarPinyin = u32strToUtf8str(toneConvert(pinyin, style, v_to_u, neutral_tone_with_five));
                if (seen.insert(tarPinyin).second) {
                    toneCandidates.push_back(tarPinyin);
                }
            }

            if (toneCandidates.empty())
                return {u32strToUtf8str(hanzi)};
            return toneCandidates;
        }

        void zhPosition(const u32strVec &input, u32strVec &res, std::vector<int> &positions);
    };
}

#endif // ChineseG2pPRIVATE_H
