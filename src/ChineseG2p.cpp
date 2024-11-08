#include <cpp-pinyin/ChineseG2p.h>
#include <cpp-pinyin/G2pglobal.h>

#include "ChineseG2p_p.h"
#include "DictUtil.h"

#include <algorithm>
#include <filesystem>

namespace Pinyin
{
    static u32strVec splitString(const u32str &input) {
        u32strVec res;
        int pos = 0;
        while (pos < input.length()) {
            const auto &currentChar = input[pos];
            if (isLetter(currentChar)) {
                const int start = pos;
                while (pos < input.length() && isLetter(input[pos])) {
                    pos++;
                }
                res.push_back(input.substr(start, pos - start));
            } else if (isHanzi(currentChar) || isDigit(currentChar) || !isSpace(currentChar)) {
                res.push_back(input.substr(pos, 1));
                pos++;
            } else if (isKana(currentChar)) {
                const int length = pos + 1 < input.length() && isSpecialKana(input[pos + 1]) ? 2 : 1;
                res.push_back(input.substr(pos, length));
                pos += length;
            } else {
                pos++;
            }
        }
        return res;
    }

    static u32str mid(const u32strVec &inputList, size_t cursor, size_t length) {
        const size_t end = std::min(cursor + length, inputList.size());

        u32str result;
        for (size_t i = cursor; i < end; ++i) {
            result += inputList[i];
        }

        return result;
    }

    // reset pinyin to raw string
    static PinyinResVector resetZH(const u32strVec &input, const PinyinResVector &res,
                                   const std::vector<bool> &positions) {
        PinyinResVector result;
        result.reserve(input.size());
        int offset = 0;
        for (int i = 0; i < input.size(); ++i) {
            const auto &encodeStr = input[i].encodeUtf8();
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
    void ChineseG2pPrivate::zhPosition(const u32strVec &input, u32strVec &res, std::vector<bool> &positions) {
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
        return hanziToPinyin(splitString(utf8strToU32str(hans)), style, error, candidates, v_to_u,
                             neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::string> &hans, int style, Error error,
                                              bool candidates, bool v_to_u, bool neutral_tone_with_five) const {
        u32strVec hansList;
        hansList.reserve(hans.size());
        for (const auto &item : hans) {
            hansList.emplace_back(utf8strToU32str(item));
        }
        return hanziToPinyin(hansList, style, error, candidates, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const u32strVec &hans, int style, Error error, bool candidates,
                                              bool v_to_u, bool neutral_tone_with_five) const {
        u32strVec inputList;
        std::vector<bool> inputPos(hans.size(), false);

        // get char&pos in dict
        d_ptr->zhPosition(hans, inputList, inputPos);

        PinyinResVector result;
        result.reserve(inputList.size());
        int cursor = 0;
        while (cursor < inputList.size()) {
            // get char
            const u32str &current_char = inputList[cursor];

            // not in dict
            if (d_ptr->word_dict.find(current_char) == d_ptr->word_dict.end()) {
                result.emplace_back(PinyinRes{current_char.encodeUtf8()});
                cursor++;
                continue;
            }

            // is polypropylene
            if (!d_ptr->isPolyphonic(current_char)) {
                const auto &pinyin = d_ptr->getDefaultPinyin(current_char, style, v_to_u, neutral_tone_with_five);
                result.emplace_back(PinyinRes{
                    current_char.encodeUtf8(),
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
                        const u32str subPhrase = mid(inputList, cursor, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhrase);
                        if (it != d_ptr->phrases_dict.end()) {
                            const auto &subRes = d_ptr->toneConvert(it->second, style, v_to_u, neutral_tone_with_five);
                            for (int i = 0; i < subRes.size(); i++) {
                                const u32str lyric = subPhrase.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    lyric.encodeUtf8(),
                                    subRes[i].encodeUtf8(),
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
                            const u32str subPhrase1 = mid(inputList, cursor - 1, length);
                            const auto &it1 = d_ptr->phrases_dict.find(subPhrase1);
                            if (it1 != d_ptr->phrases_dict.end()) {
                                result.pop_back();
                                const auto &subRes1 = d_ptr->toneConvert(
                                    it1->second, style, v_to_u, neutral_tone_with_five);
                                for (int i = 0; i < subRes1.size(); i++) {
                                    const u32str lyric = subPhrase1.substr(i, 1);
                                    result.emplace_back(PinyinRes{
                                        lyric.encodeUtf8(),
                                        subRes1[i].encodeUtf8(),
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
                        const u32str subPhraseBack = mid(inputList, cursor + 1 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 1 - length, length - 1);
                            const auto &subResBack = d_ptr->toneConvert(it->second, style, v_to_u,
                                                                        neutral_tone_with_five);
                            for (int i = 0; i < subResBack.size(); i++) {
                                const u32str lyric = subPhraseBack.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    lyric.encodeUtf8(),
                                    subResBack[i].encodeUtf8(),
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
                        const u32str subPhraseBack1 = mid(inputList, cursor + 2 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack1);
                        if (it != d_ptr->phrases_dict.end()) {
                            // overwrite pinyin
                            removeElements(result, cursor + 2 - length, length - 2);
                            const auto &subResBack1 = d_ptr->toneConvert(
                                it->second, style, v_to_u, neutral_tone_with_five);
                            for (int i = 0; i < subResBack1.size(); i++) {
                                const u32str lyric = subPhraseBack1.substr(i, 1);
                                result.emplace_back(PinyinRes{
                                    lyric.encodeUtf8(),
                                    subResBack1[i].encodeUtf8(),
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
                        current_char.encodeUtf8(),
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
        return u32strToUtf8str(d_ptr->tradToSim(utf8strToU32str(hanzi)));
    }

    bool ChineseG2p::isPolyphonic(const std::string &hanzi) const {
        return d_ptr->isPolyphonic(utf8strToU32str(hanzi));
    }

    std::vector<std::string> ChineseG2p::getDefaultPinyin(const std::string &text, int style, bool v_to_u,
                                                          bool neutral_tone_with_five) const {
        return d_ptr->getDefaultPinyin(d_ptr->tradToSim(utf8strToU32str(text)), style, v_to_u, neutral_tone_with_five);
    }
}
