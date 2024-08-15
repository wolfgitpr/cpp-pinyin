#ifndef MANTONEUTIL_H
#define MANTONEUTIL_H

#include <tinyutf8.h>

typedef tiny_utf8::string u8string;
typedef std::vector<tiny_utf8::string> u8stringlist;

namespace Pinyin {
    u8string tone3ToTone(const u8string& pinyin);
} // Pinyin

#endif //MANTONEUTIL_H
