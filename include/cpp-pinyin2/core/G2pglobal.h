#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>

namespace cpp_pinyin::v2
{
    // Character-class predicates.
    //
    // These are PURE: the engine depends on none of them (see the note on `isHanzi` in
    // src/G2pglobal.cpp), so widening a range here changes no conversion output. The authoritative
    // ranges -- and the reason `isHanzi` is three blocks rather than one -- are documented there, not
    // here.
    //
    // One behaviour that looks like a defect and is not: `isSpace` accepts U+0020 ONLY, not Unicode
    // whitespace. Callers that need "is this whitespace" must use `isSkippableSpace` semantics as the
    // tokenizer does; loosening this one turns '\t' and U+3000 into failed rows (K1, see the tokenizer's window gate).
    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isLetter(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isHanzi(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isKana(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isDigit(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isSpace(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isSpecialKana(char16_t c);
}  // namespace cpp_pinyin::v2
