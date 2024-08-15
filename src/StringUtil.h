#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <string>

namespace Pinyin
{
    std::string wideToUtf8(const std::wstring& utf16Str);
    std::wstring utf8ToWide(const std::string& utf8Str);
}

#endif //STRINGUTIL_H
