#ifndef DICTUTIL_H
#define DICTUTIL_H

#include <filesystem>
#include <functional>

#include "ManToneUtil.h"

namespace Pinyin
{
    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<char16_t, char16_t> &resultMap, const char &sep1 = ':');

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<char16_t, std::u16string> &resultMap, const char &sep1 = ':');

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<char16_t, std::vector<std::u16string>> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<std::u16string, std::vector<std::u16string>> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    bool loadAdditionalDict(const std::filesystem::path &dict_dir,
                            std::unordered_map<std::u16string, std::vector<std::u16string>> &resultMap,
                            const char &sep1 = ':',
                            const std::string &sep2 = " ",
                            const std::function<std::u16string(const std::u16string &pinyin)> &converterForDefaultPinyin
                                = tone3ToTone);
} // Pinyin

#endif //DICTUTIL_H
