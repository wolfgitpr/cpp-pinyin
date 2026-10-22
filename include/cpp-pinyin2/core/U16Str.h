#pragma once

#include <string>

#include <cpp-pinyin2/core/PinyinGlobal.h>

namespace cpp_pinyin::v2
{
    // The three conversions between the two encodings the library speaks internally (UTF-16) and at its
    // boundary (UTF-8). `[[nodiscard]]` for the reason G2pglobal.h's predicates carry it: a caller that
    // ignores the result has written a no-op, and nothing about a compile does that on purpose.
    CPP_PINYIN2_NODISCARD std::string CPP_PINYIN2_EXPORT u16strToUtf8str(const char16_t &ch16);
    CPP_PINYIN2_NODISCARD std::string CPP_PINYIN2_EXPORT u16strToUtf8str(const std::u16string &u16str);
    CPP_PINYIN2_NODISCARD std::u16string CPP_PINYIN2_EXPORT utf8strToU16str(const std::string &utf8str);
}  // namespace cpp_pinyin::v2
