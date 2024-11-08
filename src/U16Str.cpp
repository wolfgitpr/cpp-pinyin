#include <cpp-pinyin/U16Str.h>

#include <string>
#include <stdexcept>

namespace Pinyin
{
    std::string u16strToUtf8str(const std::u16string &u16str) {
        std::string utf8str;
        utf8str.reserve(u16str.size() * 3); // UTF-16 characters could expand into 3 bytes in UTF-8

        for (size_t i = 0; i < u16str.size(); ++i) {
            const uint16_t ch = u16str[i];

            if (ch < 0x80) {
                // 1-byte sequence
                utf8str.push_back(static_cast<char>(ch));
            } else if (ch < 0x800) {
                // 2-byte sequence
                utf8str.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                utf8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            } else if (ch >= 0xD800 && ch <= 0xDBFF) {
                // High surrogate (part of a 4-byte UTF-16 character)
                if (i + 1 >= u16str.size())
                    throw std::invalid_argument("Invalid UTF-16 surrogate pair");

                uint16_t low = u16str[i + 1];
                if (low < 0xDC00 || low > 0xDFFF)
                    throw std::invalid_argument("Invalid UTF-16 surrogate pair");

                uint32_t codepoint = ((ch - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                utf8str.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                utf8str.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                ++i; // Skip next low surrogate
            } else {
                // 3-byte sequence
                utf8str.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                utf8str.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }

        return utf8str;
    }

    std::u16string utf8strToU16str(const std::string &utf8str) {
        std::u16string u16str;
        u16str.reserve(utf8str.size());

        size_t i = 0;
        while (i < utf8str.size()) {
            const unsigned char c = utf8str[i];

            if (c < 0x80) {
                // 1-byte sequence
                u16str.push_back(c);
                ++i;
            } else if (c < 0xE0) {
                // 2-byte sequence
                if (i + 1 >= utf8str.size())
                    throw std::invalid_argument("Invalid UTF-8 sequence");
                u16str.push_back(((c & 0x1F) << 6) | (utf8str[i + 1] & 0x3F));
                i += 2;
            } else if (c < 0xF0) {
                // 3-byte sequence
                if (i + 2 >= utf8str.size())
                    throw std::invalid_argument("Invalid UTF-8 sequence");
                u16str.push_back(((c & 0x0F) << 12) | ((utf8str[i + 1] & 0x3F) << 6) | (utf8str[i + 2] & 0x3F));
                i += 3;
            } else {
                // 4-byte sequence (assuming UTF-32 character, but storing in UTF-16)
                if (i + 3 >= utf8str.size())
                    throw std::invalid_argument("Invalid UTF-8 sequence");
                uint32_t codepoint = ((c & 0x07) << 18) | ((utf8str[i + 1] & 0x3F) << 12) |
                    ((utf8str[i + 2] & 0x3F) << 6) | (utf8str[i + 3] & 0x3F);
                codepoint -= 0x10000;
                u16str.push_back(0xD800 | (codepoint >> 10)); // High surrogate
                u16str.push_back(0xDC00 | (codepoint & 0x3FF)); // Low surrogate
                i += 4;
            }
        }

        return u16str;
    }
}
