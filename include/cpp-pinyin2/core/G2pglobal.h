#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>

namespace cpp_pinyin::v2
{
    // Character-class predicates. They used to live unqualified in the global namespace (`::isHanzi`), which
    // is why the file is named after them; the `cpp_pinyin::v2::` qualification has been the rule since 2.0.
    //
    // These are PURE predicates: the engine does not depend on any of them (see the note on `isHanzi` in
    // src/G2pglobal.cpp), so widening a range here changes no conversion output. The authoritative character
    // ranges -- and the reason the range is three blocks rather than the single one an earlier revision wrote -- are
    // there, not here.
    //
    // One behaviour that looks like a defect and is not: `isSpace` accepts U+0020 ONLY, not Unicode
    // whitespace. Callers that need "is this whitespace" must use `isSkippableSpace` semantics as the
    // tokenizer does; loosening this one turns '\t' and U+3000 into failed rows (K1, see src/EngineImpl.cpp).
    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isLetter(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isHanzi(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isKana(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isDigit(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isSpace(char16_t c);

    CPP_PINYIN2_NODISCARD bool CPP_PINYIN2_EXPORT isSpecialKana(char16_t c);
}  // namespace cpp_pinyin::v2
