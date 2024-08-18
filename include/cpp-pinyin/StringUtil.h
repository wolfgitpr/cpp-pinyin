#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <string>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::string CPP_PINYIN_EXPORT wideToUtf8(const std::wstring &utf16Str);
    std::wstring CPP_PINYIN_EXPORT utf8ToWide(const std::string &utf8Str);
}

#endif //STRINGUTIL_H
