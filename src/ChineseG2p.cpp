#include <cpp-pinyin/ChineseG2p.h>
#include <cpp-pinyin/G2pglobal.h>

#include "ChineseG2p_p.h"
#include "DictUtil.h"

#include <algorithm>
#include <filesystem>

#include "cpp-pinyin/U16Str.h"

namespace Pinyin
{
    static std::vector<std::u16string> splitString(const std::u16string &input) {
        std::vector<std::u16string> res;
        res.reserve(input.length());
        auto start = input.begin();
        const auto end = input.end();

        while (start != end) {
            const auto &currentChar = *start;
            if (Pinyin::isLetter(currentChar)) {
                auto letterStart = start;
                while (start != end && Pinyin::isLetter(*start)) {
                    ++start;
                }
                res.emplace_back(letterStart, start);
            } else if (Pinyin::isHanzi(currentChar) || Pinyin::isDigit(currentChar) || !Pinyin::isSpace(currentChar)) {
                res.emplace_back(1, currentChar);
                ++start;
            } else if (Pinyin::isKana(currentChar)) {
                const int length = (start + 1 != end && Pinyin::isSpecialKana(*(start + 1))) ? 2 : 1;
                res.emplace_back(start, start + length);
                std::advance(start, length);
            } else {
                ++start;
            }
        }
        return res;
    }

    static std::u16string mid(const std::vector<std::u16string> &inputList, const size_t cursor, const size_t length) {
        const size_t end = std::min(cursor + length, inputList.size());

        std::u16string result;
        for (size_t i = cursor; i < end; ++i) {
            result += inputList[i];
        }

        return result;
    }

    // reset pinyin to raw string
    static PinyinResVector resetZH(const std::vector<std::u16string> &input, const PinyinResVector &res,
                                   const std::vector<bool> &positions) {
        PinyinResVector result;
        result.reserve(input.size());
        int offset = 0;
        for (int i = 0; i < input.size(); ++i) {
            const auto &encodeStr = u16strToUtf8str(input[i]);
            if (positions[i])
                result.emplace_back(PinyinRes{encodeStr, res[i - offset].pinyin,
                                              res[i - offset].candidates, false});
            else {
                result.emplace_back(PinyinRes{encodeStr, encodeStr, {encodeStr},
                                              true});
                offset++;
            }
        }
        return result;
    }

    // delete elements from the list
    template <class T>
    static inline void removeElements(std::vector<T> &vector, int start, int n) {
        vector.erase(vector.begin() + start, vector.begin() + start + n);
    }

    ChineseG2pPrivate::ChineseG2pPrivate(std::string language) :
        m_language(std::move(language)) {}

    ChineseG2pPrivate::~ChineseG2pPrivate() = default;

    // load zh convert dict
    void ChineseG2pPrivate::init() {
        const std::filesystem::path dict_dir = dictionaryPath() / m_language;

        initialized = loadDict(dict_dir / "phrases_map.txt", phrases_map) &&
            loadDict(dict_dir / "phrases_dict.txt", phrases_dict) &&
            loadAdditionalDict(dict_dir / "user_dict.txt", phrases_dict) &&
            loadDict(dict_dir / "word.txt", word_dict) &&
            loadDict(dict_dir / "trans_word.txt", trans_dict);
    }

    // get all chinese characters and positions in the list
    void ChineseG2pPrivate::zhPosition(const std::vector<std::u16string> &input, std::vector<std::u16string> &res,
                                       std::vector<bool> &positions) {
        res.reserve(input.size());
        for (int i = 0; i < input.size(); ++i) {
            const auto &item = input[i];
            if (item.empty())
                continue;

            const auto &simItem = trans_dict.find(item) != trans_dict.end() ? trans_dict[item] : item;

            if (word_dict.find(simItem) != word_dict.end()) {
                res.emplace_back(simItem);
                positions[i] = true;
            }
        }
    }

    ChineseG2p::ChineseG2p(const std::string &language) {
        d_ptr = std::make_unique<ChineseG2pPrivate>(language);
        d_ptr->init();
    }

    ChineseG2p::~ChineseG2p() = default;

    bool ChineseG2p::initialized() const {
        return d_ptr->initialized;
    }

    /*
    Style:
        陟罚臧否:zhi4 fa2 zang2 pi3
        汤汤:shang1 shang1
        到了:dao4 le1
    */
    bool ChineseG2p::loadUserDict(const std::filesystem::path &filePath) const {
        return loadAdditionalDict(filePath / "user_dict.txt", d_ptr->phrases_dict);
    }

    void ChineseG2p::setToneConverter(const ToneConverter &toneConverter) const {
        d_ptr->m_toneConverter = toneConverter;
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::string &hans, int style, Error error, bool candidates,
                                              bool v_to_u, bool neutral_tone_with_five) const {
        return hanziToPinyin(splitString(utf8strToU16str(hans)), style, error, candidates, v_to_u,
                             neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::string> &hans, int style, Error error,
                                              bool candidates, bool v_to_u, bool neutral_tone_with_five) const {
        std::vector<std::u16string> hansList;
        hansList.reserve(hans.size());
        for (const auto &item : hans) {
            hansList.emplace_back(utf8strToU16str(item));
        }
        return hanziToPinyin(hansList, style, error, candidates, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::u16string> &hans, int style, Error error,
                                              bool candidates,
                                              bool v_to_u, bool neutral_tone_with_five) const {
        std::vector<std::u16string> inputList;
        std::vector<bool> inputPos(hans.size(), false);

        // get char&pos in dict
        d_ptr->zhPosition(hans, inputList, inputPos);

        PinyinResVector result;
        result.reserve(inputList.size());
        int cursor = 0;
        while (cursor < inputList.size()) {
            // get char
            const std::u16string &current_char = inputList[cursor];

            // not in dict
            if (d_ptr->word_dict.find(current_char) == d_ptr->word_dict.end()) {
                result.emplace_back(PinyinRes{u16strToUtf8str(current_char)});
                cursor++;
                continue;
            }

            // is polypropylene
            if (!d_ptr->isPolyphonic(current_char)) {
                const auto &pinyin = d_ptr->getDefaultPinyin(current_char, style, v_to_u, neutral_tone_with_five);
                result.emplace_back(PinyinRes{
                    u16strToUtf8str(current_char),
                    pinyin[0],
                    candidates ? pinyin : std::vector<std::string>{},
                    false
                });
                cursor++;
            } else {
                bool found = false;
                for (int length = 4; length >= 2 && !found; length--) {
                    if (cursor + length <= inputList.size()) {
                        // cursor: 地, subPhrase: 地久天长
                        const std::u16string subPhrase = mid(inputList, cursor, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhrase);
                        if (it != d_ptr->phrases_dict.end()) {
                            const auto &subRes = d_ptr->toneConvert(it->second, style, v_to_u, neutral_tone_with_five);
                            for (int i = 0; i < subRes.size(); i++) {
                                const std::u16string lyric = subPhrase.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric),
                                    u16strToUtf8str(subRes[i]),
                                    candidates
                                    ? d_ptr->getDefaultPinyin(lyric, style, v_to_u, neutral_tone_with_five)
                                    : std::vector<std::string>{},
                                    false
                                });
                            }
                            cursor += length;
                            found = true;
                        }

                        if (cursor >= 1) {
                            // cursor: 重, subPhrase_1: 语重心长
                            const std::u16string subPhrase1 = mid(inputList, cursor - 1, length);
                            const auto &it1 = d_ptr->phrases_dict.find(subPhrase1);
                            if (it1 != d_ptr->phrases_dict.end()) {
                                result.pop_back();
                                const auto &subRes1 = d_ptr->toneConvert(
                                    it1->second, style, v_to_u, neutral_tone_with_five);
                                for (int i = 0; i < subRes1.size(); i++) {
                                    const std::u16string lyric = subPhrase1.substr(i, 1);
                                    result.emplace_back(PinyinRes{
                                        u16strToUtf8str(lyric),
                                        u16strToUtf8str(subRes1[i]),
                                        candidates
                                        ? d_ptr->getDefaultPinyin(lyric, style, v_to_u, neutral_tone_with_five)
                                        : std::vector<std::string>{},
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
                        const std::u16string subPhraseBack = mid(inputList, cursor + 1 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 1 - length, length - 1);
                            const auto &subResBack = d_ptr->toneConvert(it->second, style, v_to_u,
                                                                        neutral_tone_with_five);
                            for (int i = 0; i < subResBack.size(); i++) {
                                const std::u16string lyric = subPhraseBack.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric),
                                    u16strToUtf8str(subResBack[i]),
                                    candidates
                                    ? d_ptr->getDefaultPinyin(lyric, style, v_to_u, neutral_tone_with_five)
                                    : std::vector<std::string>{},
                                    false
                                });
                            }
                            cursor += 1;
                            found = true;
                        }
                    }

                    if (cursor + 2 >= length && cursor + 2 <= inputList.size()) {
                        // cursor: 好, xSubPhrase: 叶公好龙
                        const std::u16string subPhraseBack1 = mid(inputList, cursor + 2 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack1);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 2 - length, length - 2);
                            const auto &subResBack1 = d_ptr->toneConvert(
                                it->second, style, v_to_u, neutral_tone_with_five);
                            for (int i = 0; i < subResBack1.size(); i++) {
                                const std::u16string lyric = subPhraseBack1.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric),
                                    u16strToUtf8str(subResBack1[i]),
                                    candidates
                                    ? d_ptr->getDefaultPinyin(lyric, style, v_to_u, neutral_tone_with_five)
                                    : std::vector<std::string>{},
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
                    const auto &pinyin = d_ptr->getDefaultPinyin(current_char, style, v_to_u, neutral_tone_with_five);
                    result.emplace_back(PinyinRes{
                        u16strToUtf8str(current_char),
                        pinyin[0],
                        candidates ? pinyin : std::vector<std::string>{},
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

    std::string ChineseG2p::tradToSim(const std::string &hanzi) const {
        return u16strToUtf8str(d_ptr->tradToSim(utf8strToU16str(hanzi)));
    }

    bool ChineseG2p::isPolyphonic(const std::string &hanzi) const {
        return d_ptr->isPolyphonic(utf8strToU16str(hanzi));
    }

    std::vector<std::string> ChineseG2p::getDefaultPinyin(const std::string &text, int style, bool v_to_u,
                                                          bool neutral_tone_with_five) const {
        return d_ptr->getDefaultPinyin(d_ptr->tradToSim(utf8strToU16str(text)), style, v_to_u, neutral_tone_with_five);
    }
}
