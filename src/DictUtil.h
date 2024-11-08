#ifndef DICTUTIL_H
#define DICTUTIL_H

#include <filesystem>
#include <functional>

#include <cpp-pinyin/U16Str.h>

#include "ManToneUtil.h"

namespace Pinyin
{
    bool loadDict(const std::filesystem::path &dict_dir,
                  u16strHashMap<u16str, u16str> &resultMap, const char &sep1 = ':');

    bool loadDict(const std::filesystem::path &dict_dir,
                  u16strHashMap<u16str, u16strVec> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    bool loadAdditionalDict(const std::filesystem::path &dict_dir,
                            u16strHashMap<u16str, u16strVec> &resultMap, const char &sep1 = ':',
                            const std::string &sep2 = " ",
                            const std::function<u16str(const u16str &pinyin)> &converterForDefaultPinyin = tone3ToTone);
} // Pinyin

#endif //DICTUTIL_H
