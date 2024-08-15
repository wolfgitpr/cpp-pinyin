#include "ChineseG2p.h"
#include "G2pglobal.h"
#include "ChineseG2p_p.h"

#include <unordered_map>
#include <stdexcept>

namespace Pinyin
{
    static const std::unordered_map<char32_t, u8string> numMap = {
        {U'0', U"零"},
        {U'1', U"一"},
        {U'2', U"二"},
        {U'3', U"三"},
        {U'4', U"四"},
        {U'5', U"五"},
        {U'6', U"六"},
        {U'7', U"七"},
        {U'8', U"八"},
        {U'9', U"九"}
    };

    static u8string mid(const u8stringlist &inputList, size_t cursor, size_t length) {
        const size_t end = std::min(cursor + length, inputList.size());

        u8string result;
        for (size_t i = cursor; i < end; ++i) {
            result += inputList[i];
        }

        return result;
    }

    // reset pinyin to raw string
    static PinyinResVector resetZH(const u8stringlist &input, const PinyinResVector &res,
                                   const std::vector<int> &positions) {
        PinyinResVector result;
        result.reserve(input.size());
        for (const auto &lyric : input) {
            result.emplace_back(PinyinRes{lyric.cpp_str()});
        }

        for (int i = 0; i < positions.size(); i++) {
            result[positions[i]] = res[i];
        }
        return result;
    }

    // delete elements from the list
    template <class T>
    static inline void removeElements(std::vector<T> &vector, int start, int n) {
        vector.erase(vector.begin() + start, vector.begin() + start + n);
    }

    ChineseG2pPrivate::ChineseG2pPrivate(std::string language, ToneConverter *toneConverter) :
        m_language(std::move(language)), m_toneConverter(toneConverter) {}

    ChineseG2pPrivate::~ChineseG2pPrivate() = default;

    // load zh convert dict
    void ChineseG2pPrivate::init() {
        const std::string dict_dir = dictionaryPath() + "/" + m_language;

        initialized = loadDict(dict_dir, "phrases_map.txt", phrases_map) &&
            loadDict(dict_dir, "phrases_dict.txt", phrases_dict) &&
            loadDict(dict_dir, "user_dict.txt", phrases_dict) &&
            loadDict(dict_dir, "word.txt", word_dict) &&
            loadDict(dict_dir, "trans_word.txt", trans_dict);
    }

    // get all chinese characters and positions in the list
    void ChineseG2pPrivate::zhPosition(const u8stringlist &input, u8stringlist &res, std::vector<int> &positions) {
        res.reserve(input.size());
        positions.reserve(input.size());
        for (int i = 0; i < input.size(); ++i) {
            const auto &item = input[i];
            if (item.empty())
                continue;

            const auto simItem = trans_dict.find(item) != trans_dict.end() ? trans_dict[item] : item;

            if (word_dict.find(simItem) != word_dict.end()) {
                res.emplace_back(simItem);
                positions.emplace_back(i);
            }
        }
    }

    ChineseG2p::ChineseG2p(const std::string &language, ToneConverter *toneConverter) :
        ChineseG2p(
            *new ChineseG2pPrivate(language, toneConverter)) {}

    ChineseG2p::~ChineseG2p() = default;

    bool ChineseG2p::initialized() const {
        return d_ptr->initialized;
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::string &hans, int style,
                                              Error error, bool v_to_u, bool neutral_tone_with_five) const {
        return hanziToPinyin(splitString(u8string(hans)), style, error, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::string> &hans, int style,
                                              Error error, bool v_to_u, bool neutral_tone_with_five) const {
        u8stringlist hansList;
        hansList.reserve(hans.size());
        for (const auto &item : hans) {
            hansList.emplace_back(item);
        }
        return hanziToPinyin(hansList, style, error, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const u8stringlist &hans, int style,
                                              Error error, bool v_to_u, bool neutral_tone_with_five) const {
        u8stringlist inputList;
        std::vector<int> inputPos;

        // get char&pos in dict
        d_ptr->zhPosition(hans, inputList, inputPos);

        PinyinResVector result;
        result.reserve(inputList.size());
        int cursor = 0;
        while (cursor < inputList.size()) {
            // get char
            const auto &current_char = inputList[cursor];

            // not in dict
            if (d_ptr->word_dict.find(current_char) == d_ptr->word_dict.end()) {
                result.emplace_back(PinyinRes{current_char.cpp_str()});
                cursor++;
                continue;
            }

            // is polypropylene
            if (!d_ptr->isPolyphonic(current_char)) {
                const auto pinyin = d_ptr->getDefaultPinyin(current_char, style);
                result.emplace_back(PinyinRes{
                    current_char.cpp_str(),
                    pinyin[0].cpp_str(),
                    toStdList(pinyin),
                    false
                });
                cursor++;
            }
            else {
                bool found = false;
                for (int length = 4; length >= 2 && !found; length--) {
                    if (cursor + length <= inputList.size()) {
                        // cursor: 地, subPhrase: 地久天长
                        const u8string subPhrase = mid(inputList, cursor, length);
                        if (d_ptr->phrases_dict.find(subPhrase) != d_ptr->phrases_dict.end()) {
                            const auto subRes = d_ptr->toneConvert(d_ptr->phrases_dict[subPhrase], style);
                            for (int i = 0; i < subRes.size(); i++) {
                                const auto lyric = subPhrase.substr(i, 1).cpp_str();
                                result.emplace_back(PinyinRes{
                                    lyric,
                                    subRes[i].cpp_str(),
                                    toStdList(d_ptr->getDefaultPinyin(lyric, style)),
                                    false
                                });
                            }
                            cursor += length;
                            found = true;
                        }

                        if (cursor >= 1) {
                            // cursor: 重, subPhrase_1: 语重心长
                            const u8string subPhrase1 = mid(inputList, cursor - 1, length);
                            auto it = d_ptr->phrases_dict.find(subPhrase1);
                            if (it != d_ptr->phrases_dict.end()) {
                                result.pop_back();
                                const auto &subRes1 = d_ptr->toneConvert(it->second, style);
                                for (int i = 0; i < subRes1.size(); i++) {
                                    const auto lyric = subPhrase1.substr(i, 1).cpp_str();
                                    result.emplace_back(PinyinRes{
                                        lyric,
                                        subRes1[i].cpp_str(),
                                        toStdList(d_ptr->getDefaultPinyin(lyric, style)),
                                        false
                                    });
                                }
                                cursor += length - 1;
                                found = true;
                            }
                        }
                    }

                    if (cursor + 1 >= length && cursor + 1 <= inputList.size()) {
                        // cursor: 好, xSubPhrase: 各有所好
                        const u8string subPhraseBack = mid(inputList, cursor + 1 - length, length);
                        auto it = d_ptr->phrases_dict.find(subPhraseBack);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 1 - length, length - 1);
                            const auto &subResBack = d_ptr->toneConvert(it->second, style);
                            for (int i = 0; i < subResBack.size(); i++) {
                                const auto lyric = subPhraseBack.substr(i, 1).cpp_str();
                                result.emplace_back(PinyinRes{
                                    lyric,
                                    subResBack[i].cpp_str(),
                                    toStdList(d_ptr->getDefaultPinyin(lyric, style)),
                                    false
                                });
                            }
                            cursor += 1;
                            found = true;
                        }
                    }

                    if (cursor + 2 >= length && cursor + 2 <= inputList.size()) {
                        // cursor: 好, xSubPhrase: 叶公好龙
                        const u8string subPhraseBack1 = mid(inputList, cursor + 2 - length, length);
                        const auto it = d_ptr->phrases_dict.find(subPhraseBack1);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 2 - length, length - 2);
                            const auto &subResBack1 = d_ptr->toneConvert(it->second, style);
                            for (int i = 0; i < subResBack1.size(); i++) {
                                const auto lyric = subPhraseBack1.substr(i, 1).cpp_str();
                                result.emplace_back(PinyinRes{
                                    lyric,
                                    subResBack1[i].cpp_str(),
                                    toStdList(d_ptr->getDefaultPinyin(lyric, style)),
                                    false
                                });
                            }
                            cursor += 2;
                            found = true;
                        }
                    }
                }

                // not found, use default pinyin
                if (!found) {
                    result.emplace_back(PinyinRes{
                        current_char.cpp_str(),
                        d_ptr->getDefaultPinyin(current_char, style)[0].cpp_str(),
                        toStdList(d_ptr->getDefaultPinyin(current_char, style)),
                        false
                    });
                    cursor++;
                }
            }
        }

        // Alloc 2
        if (error == Error::Ignore) {
            return result;
        }
        return resetZH(hans, result, inputPos);
    }

    ChineseG2p::ChineseG2p(ChineseG2pPrivate &d) :
        d_ptr(&d) {
        d.q_ptr = this;

        d.init();
    }

    std::string ChineseG2p::tradToSim(const std::string &text) const {
        return d_ptr->tradToSim(text).cpp_str();
    }

    bool ChineseG2p::isPolyphonic(const std::string &text) const {
        return d_ptr->isPolyphonic(text);
    }

    std::vector<std::string> ChineseG2p::getDefaultPinyin(const std::string &text, int style) const {
        return toStdList(d_ptr->getDefaultPinyin(d_ptr->tradToSim(text), style));
    }
}
