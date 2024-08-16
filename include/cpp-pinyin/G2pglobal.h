#ifndef G2PGLOBAL_H
#define G2PGLOBAL_H

#include <string>

namespace Pinyin
{
    std::string dictionaryPath();

    void setDictionaryPath(const std::string &dir);

    bool isLetter(char32_t c);

    bool isHanzi(char32_t c);

    bool isKana(char32_t c);

    bool isDigit(char32_t c);

    bool isSpace(char32_t c);

    bool isSpecialKana(char32_t c);
}

#endif // G2PGLOBAL_H
