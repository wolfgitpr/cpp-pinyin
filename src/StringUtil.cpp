#include "StringUtil.h"

#include <algorithm>
#include <limits>

#ifdef _WIN32
#  include <windows.h>
#endif

#ifdef _WIN32
#  ifndef WC_ERR_INVALID_CHARS
#    define WC_ERR_INVALID_CHARS 0x00000080
#  endif
#endif

namespace Pinyin
{
    /*!
    Returns the wide string converted from UTF-8 multi-byte string.
*/
    std::string wideToUtf8(const std::wstring &utf16Str) {
#ifdef _WIN32
        if (utf16Str.empty()) {
            return {};
        }
        const auto utf16Length = static_cast<int>(utf16Str.size());
        if (utf16Length >= (std::numeric_limits<int>::max)()) {
            return {};
        }
        const int utf8Length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16Str.data(),
                                                     utf16Length, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) {
            return {};
        }
        std::string utf8Str;
        utf8Str.resize(utf8Length);
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16Str.data(), utf16Length,
                              utf8Str.data(), utf8Length, nullptr, nullptr);
        return utf8Str;
#else
        return std::filesystem::path(utf16Str).string();
#endif
    }

    /*!
        Returns the UTF-8 multi-byte string converted from wide string.
    */
    std::wstring utf8ToWide(const std::string &utf8Str) {
#ifdef _WIN32
        if (utf8Str.empty()) {
            return {};
        }
        const auto utf8Length = static_cast<int>(utf8Str.size());
        if (utf8Length >= (std::numeric_limits<int>::max)()) {
            return {};
        }
        const int utf16Length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Str.data(),
                                                      utf8Length, nullptr, 0);
        if (utf16Length <= 0) {
            return {};
        }
        std::wstring utf16Str;
        utf16Str.resize(utf16Length);
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Str.data(), utf8Length,
                              utf16Str.data(), utf16Length);
        return utf16Str;
#else
        return std::filesystem::path(utf8Str).wstring();
#endif
    }
}
