#include <cpp-pinyin2/core/U16Str.h>
#include <cstdint>

#include <string>

namespace cpp_pinyin::v2
{
    namespace
    {
        /// Unicode replacement character: what a malformed byte becomes.
        constexpr char16_t kReplacement = 0xFFFD;

        constexpr bool isContinuationByte(const unsigned char c) noexcept { return (c & 0xC0) == 0x80; }

        /// End index of a malformed sequence that starts at `i`: the invalid lead byte
        /// plus every continuation byte that follows it. Consuming the whole run is
        /// what makes "one malformed code point -> exactly one U+FFFD" hold, so the
        /// emitted entry count stays equal to the code-point count and positional
        /// alignment of the accuracy datasets is preserved. Decoding then RESUMES at
        /// the returned index instead of aborting the rest of the string.
        std::size_t malformedEnd(const std::string &s, std::size_t i) noexcept {
            ++i;
            while (i < s.size() && isContinuationByte(static_cast<unsigned char>(s[i])))
                ++i;
            return i;
        }

        /// True when the 3-byte sequence E0-E F.. names a surrogate (U+D800..U+DFFF).
        /// Those are legal UTF-8 *shapes* but illegal code points, and emitting them
        /// produces a u16string that this very file refuses to convert back (C-16).
        constexpr bool isEncodedSurrogate(const unsigned char c, const unsigned char b1) noexcept {
            return c == 0xED && b1 >= 0xA0;
        }
    }  // namespace

    std::string u16strToUtf8str(const char16_t &ch16) {
        std::string utf8str;
        // No reserve(): the result is at most 3 bytes, which always fits the small buffer of
        // std::string, so reserving only added a no-op call per conversion.

        // A lone surrogate has no UTF-8 form. Returning the replacement character keeps
        // this function total (it used to throw, which called terminate() from query()).
        if (ch16 >= 0xD800 && ch16 <= 0xDFFF)
            utf8str += u8"\uFFFD";
        else if (ch16 <= 0x7F)
            utf8str.push_back(static_cast<char>(ch16));  // 1-byte
        else if (ch16 <= 0x7FF) {
            utf8str.push_back(static_cast<char>(0xC0 | ((ch16 >> 6) & 0x1F)));
            utf8str.push_back(static_cast<char>(0x80 | (ch16 & 0x3F)));
        } else {
            utf8str.push_back(static_cast<char>(0xE0 | ((ch16 >> 12) & 0x0F)));
            utf8str.push_back(static_cast<char>(0x80 | ((ch16 >> 6) & 0x3F)));
            utf8str.push_back(static_cast<char>(0x80 | (ch16 & 0x3F)));
        }
        return utf8str;
    }

    std::string u16strToUtf8str(const std::u16string &u16str) {
        std::string utf8str;
        // Only reserve once the result cannot fit the small buffer of std::string (15 bytes);
        // for the short readings that dominate, reserving was a no-op call per conversion.
        if (u16str.size() * 3 > 15)
            utf8str.reserve(u16str.size() * 3);

        for (size_t i = 0; i < u16str.size(); ++i) {
            const uint16_t ch = u16str[i];

            if (ch < 0x80) {
                utf8str.push_back(static_cast<char>(ch));  // 1-byte
            } else if (ch < 0x800) {
                utf8str.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                utf8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            } else if (ch >= 0xD800 && ch <= 0xDBFF) {
                // High surrogate: valid only when a low surrogate follows.
                if (i + 1 >= u16str.size() || u16str[i + 1] < 0xDC00 || u16str[i + 1] > 0xDFFF) {
                    utf8str += u8"\uFFFD";  // total function: no throw
                    continue;
                }
                // Computed explicitly as uint32_t: subtracting two code points and shifting left by 10 cannot
                // overflow an int anyway, but this makes the intent clearer (and avoids -Wconversion's warning
                // about int -> uint32_t).
                const uint32_t codepoint = ((static_cast<uint32_t>(ch) - 0xD800u) << 10) +
                    (static_cast<uint32_t>(u16str[i + 1]) - 0xDC00u) + 0x10000u;
                utf8str.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                utf8str.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                ++i;  // consume the low surrogate
            } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                utf8str += u8"\uFFFD";  // lone low surrogate
            } else {
                utf8str.push_back(static_cast<char>(0xE0 | (ch >> 12)));  // 3-byte
                utf8str.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                utf8str.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }

        return utf8str;
    }

    std::u16string utf8strToU16str(const std::string &utf8str) {
        std::u16string u16str;
        u16str.reserve(utf8str.size());

        // Total function: malformed input becomes U+FFFD, never an exception.
        //
        // Why this matters beyond not throwing: the engine emits exactly one result
        // entry per input code point, and the accuracy datasets align positionally.
        // Every branch below therefore consumes the whole malformed sequence and emits
        // exactly ONE replacement character, so the entry count matches the number of
        // code points a well-formed decoder would have produced. Rejected here:
        // truncated sequences, bare continuation bytes, overlong encodings
        // (0xC0/0xC1 leads, 0xE0 0x80.., 0xF0 0x80..), encoded surrogates
        // (ED A0..BF ..), and code points above U+10FFFF.
        size_t i = 0;
        while (i < utf8str.size()) {
            const unsigned char c = static_cast<unsigned char>(utf8str[i]);

            if (c < 0x80) {  // 1-byte ASCII, including an embedded NUL
                u16str.push_back(c);
                ++i;
            } else if (c < 0xE0) {
                // 2-byte. c < 0xC2 would be an overlong encoding of a 1-byte value.
                if (c < 0xC2 || i + 1 >= utf8str.size() ||
                    !isContinuationByte(static_cast<unsigned char>(utf8str[i + 1]))) {
                    i = malformedEnd(utf8str, i);
                    u16str.push_back(kReplacement);
                    continue;
                }
                u16str.push_back(((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8str[i + 1]) & 0x3F));
                i += 2;
            } else if (c < 0xF0) {
                const bool truncated = i + 2 >= utf8str.size();
                const unsigned char b1 = truncated ? 0 : static_cast<unsigned char>(utf8str[i + 1]);
                const unsigned char b2 = truncated ? 0 : static_cast<unsigned char>(utf8str[i + 2]);
                // E0 80.. is overlong; ED A0..BF encodes a surrogate.
                if (truncated || isEncodedSurrogate(c, b1) || (c == 0xE0 && b1 < 0xA0) || !isContinuationByte(b1) ||
                    !isContinuationByte(b2)) {
                    i = malformedEnd(utf8str, i);
                    u16str.push_back(kReplacement);
                    continue;
                }
                u16str.push_back(((c & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F));
                i += 3;
            } else {
                const bool truncated = i + 3 >= utf8str.size();
                const unsigned char b1 = truncated ? 0 : static_cast<unsigned char>(utf8str[i + 1]);
                const unsigned char b2 = truncated ? 0 : static_cast<unsigned char>(utf8str[i + 2]);
                const unsigned char b3 = truncated ? 0 : static_cast<unsigned char>(utf8str[i + 3]);
                // F0 80.. is overlong; above 0xF4 would exceed U+10FFFF.
                if (truncated || c > 0xF4 || (c == 0xF0 && b1 < 0x90) || (c == 0xF4 && b1 > 0x8F) ||
                    !isContinuationByte(b1) || !isContinuationByte(b2) || !isContinuationByte(b3)) {
                    i = malformedEnd(utf8str, i);
                    u16str.push_back(kReplacement);
                    continue;
                }
                const uint32_t codepoint = ((c & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
                const uint32_t v = codepoint - 0x10000;
                // Emitted as ONE surrogate pair, so downstream tokenization sees a
                // single character instead of two halves (C-16).
                u16str.push_back(static_cast<char16_t>(0xD800 | (v >> 10)));
                u16str.push_back(static_cast<char16_t>(0xDC00 | (v & 0x3FF)));
                i += 4;
            }
        }

        return u16str;
    }
}  // namespace cpp_pinyin::v2
