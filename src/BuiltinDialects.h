#pragma once

// Internal (not installed): the two built-in dialect descriptions, each defined next to its own facade.
// `src/Mandarin.cpp` and `src/Cantonese.cpp` define them and `src/Engine.cpp` registers both ids, so the two
// dialects are always compiled.
//
// They live in the facades rather than in the registry because `Mandarin` and `Cantonese` must keep working
// even if a caller registers a dialect under the id "mandarin" with replace=true: the built-in facades never
// go through the registry.

#include <cpp-pinyin2/ext/Dialect.h>

namespace cpp_pinyin::v2
{
    namespace detail
    {
        Dialect makeMandarinDialect();
        Dialect makeCantoneseDialect();
    }  // namespace detail
}  // namespace cpp_pinyin::v2
