#ifndef U16STR_H
#define U16STR_H

#include <string>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::string CPP_PINYIN_EXPORT u16strToUtf8str(const std::u16string &u16str);
    std::u16string CPP_PINYIN_EXPORT utf8strToU16str(const std::string &utf8str);
}
#endif //U16STR_H
