#ifndef DICTUTIL_H
#define DICTUTIL_H

#include <functional>

#include <cpp-pinyin/U32Str.h>

#include "ManToneUtil.h"

namespace Pinyin
{
    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  u32strHashMap<u32str, u32str> &resultMap, const char &sep1 = ':');

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  u32strHashMap<u32str, u32strVec> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    bool loadAdditionalDict(const std::string &dict_dir, const std::string &fileName,
                            u32strHashMap<u32str, u32strVec> &resultMap, const char &sep1 = ':',
                            const std::string &sep2 = " ",
                            const std::function<u32str(const u32str &pinyin)> &converterForDefaultPinyin = tone3ToTone);
} // Pinyin

#endif //DICTUTIL_H
