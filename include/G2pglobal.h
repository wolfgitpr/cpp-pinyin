#ifndef G2PGLOBAL_H
#define G2PGLOBAL_H

#include <string>
#include <vector>
#include <unordered_map>

#include <tinyutf8.h>

typedef tiny_utf8::string u8string;
typedef std::vector<tiny_utf8::string> u8stringlist;

namespace Pinyin
{
    std::string dictionaryPath();

    void setDictionaryPath(const std::string &dir);

    std::vector<std::string> split(const std::string &s, const std::string &delimiter);

    std::string join(const std::vector<std::string> &v, const std::string &delimiter);

    u8stringlist splitString(const u8string &input);

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  std::unordered_map<u8string, u8string> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  std::unordered_map<u8string, u8stringlist> &resultMap, const char &sep1 = ':',
                  const std::string &sep2 = ",");

    std::vector<std::string> toStdList(const u8stringlist &input);

    bool isLetter(char32_t c);

    bool isHanzi(char32_t c);

    bool isKana(char32_t c);

    bool isDigit(char32_t c);

    bool isSpace(char32_t c);

    bool isSpecialKana(char32_t c);
}

#endif // G2PGLOBAL_H
