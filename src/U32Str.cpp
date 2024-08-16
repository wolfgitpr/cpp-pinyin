#include <cpp-pinyin/U32Str.h>

namespace Pinyin
{
    std::u32string utf8strToU32str(const std::string &utf8str) {
        std::u32string u32str;
        u32str.reserve(utf8str.size());

        size_t i = 0;
        while (i < utf8str.size()) {
            char32_t ch = 0;
            const unsigned char byte = utf8str[i];

            if (byte <= 0x7F) {
                // 1-byte UTF-8 (ASCII)
                ch = byte;
                i += 1;
            } else if ((byte & 0xE0) == 0xC0) {
                // 2-byte UTF-8
                ch = (byte & 0x1F) << 6 |
                    utf8str[i + 1] & 0x3F;
                i += 2;
            } else if ((byte & 0xF0) == 0xE0) {
                // 3-byte UTF-8
                ch = (byte & 0x0F) << 12 |
                    (utf8str[i + 1] & 0x3F) << 6 |
                    utf8str[i + 2] & 0x3F;
                i += 3;
            } else if ((byte & 0xF8) == 0xF0) {
                // 4-byte UTF-8
                ch = (byte & 0x07) << 18 |
                    (utf8str[i + 1] & 0x3F) << 12 |
                    (utf8str[i + 2] & 0x3F) << 6 |
                    utf8str[i + 3] & 0x3F;
                i += 4;
            }

            u32str.push_back(ch);
        }

        return u32str;
    }

    std::string u32strToUtf8str(const std::u32string &u32str) {
        std::string utf8str;
        utf8str.reserve(u32str.size() * 4);

        for (const char32_t ch : u32str) {
            if (ch <= 0x7F) {
                utf8str.push_back(static_cast<char>(ch));
            } else if (ch <= 0x7FF) {
                utf8str.push_back(static_cast<char>(0xC0 | ch >> 6));
                utf8str.push_back(static_cast<char>(0x80 | ch & 0x3F));
            } else if (ch <= 0xFFFF) {
                utf8str.push_back(static_cast<char>(0xE0 | ch >> 12));
                utf8str.push_back(static_cast<char>(0x80 | ch >> 6 & 0x3F));
                utf8str.push_back(static_cast<char>(0x80 | ch & 0x3F));
            } else if (ch <= 0x10FFFF) {
                utf8str.push_back(static_cast<char>(0xF0 | ch >> 18));
                utf8str.push_back(static_cast<char>(0x80 | ch >> 12 & 0x3F));
                utf8str.push_back(static_cast<char>(0x80 | ch >> 6 & 0x3F));
                utf8str.push_back(static_cast<char>(0x80 | ch & 0x3F));
            }
        }
        return utf8str;
    }
}
