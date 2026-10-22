#include <cpp-pinyin2/core/G2pglobal.h>

namespace cpp_pinyin::v2
{
    bool isLetter(const char16_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    // FR-4.6 / D6's target range. D6 wrote it as `[0x3400,0x9FFF]`, but U+4DC0..U+4DFF are the 64
    // I Ching hexagram signs, not ideographs -- so this follows Unicode's actual allocation and splits
    // the range into three parts:
    //   U+3400..U+4DBF  CJK Extension A
    //   U+4E00..U+9FFF  the basic Hanzi block (the old implementation stopped at U+9FA5, the end of the
    //                   pre-Unicode-4.0 block, which made U+9FA6..U+9FFF count as "not Hanzi")
    //   U+F900..U+FAFF  CJK compatibility ideographs
    // This is a pure predicate: the engine does not depend on it (the tokenizer in EngineImpl.cpp no
    // longer calls it -- see the equivalence note there), so widening the range changes no conversion
    // output.
    bool isHanzi(const char16_t c) {
        return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF);
    }

    bool isKana(const char16_t c) { return (c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF); }

    bool isDigit(const char16_t c) { return c >= '0' && c <= '9'; }

    bool isSpace(const char16_t c) { return c == ' '; }

    // A fixed set of 16 code points: a switch rather than std::unordered_set -- no static
    // initialisation, no heap allocation, no hash lookup, and the compiler turns it into a jump table.
    // (Semantically equivalent.)
    bool isSpecialKana(const char16_t c) {
        switch (c) {
        case u'ャ':
        case u'ュ':
        case u'ョ':
        case u'ゃ':
        case u'ゅ':
        case u'ょ':
        case u'ァ':
        case u'ィ':
        case u'ゥ':
        case u'ェ':
        case u'ォ':
        case u'ぁ':
        case u'ぃ':
        case u'ぅ':
        case u'ぇ':
        case u'ぉ':
            return true;
        default:
            return false;
        }
    }
}  // namespace cpp_pinyin::v2
