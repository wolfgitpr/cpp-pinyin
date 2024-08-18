#ifndef G2PGLOBAL_H
#define G2PGLOBAL_H

#include <string>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::string CPP_PINYIN_EXPORT dictionaryPath();

    void CPP_PINYIN_EXPORT setDictionaryPath(const std::string &dir);

    bool CPP_PINYIN_EXPORT isLetter(char32_t c);

    bool CPP_PINYIN_EXPORT isHanzi(char32_t c);

    bool CPP_PINYIN_EXPORT isKana(char32_t c);

    bool CPP_PINYIN_EXPORT isDigit(char32_t c);

    bool CPP_PINYIN_EXPORT isSpace(char32_t c);

    bool CPP_PINYIN_EXPORT isSpecialKana(char32_t c);
}

#endif // G2PGLOBAL_H
