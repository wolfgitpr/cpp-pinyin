#pragma once

// Internal (not installed): the two built-in dialect descriptions, each defined next to its own
// facade. `src/Mandarin.cpp` and `src/Cantonese.cpp` define these, and `src/Engine.cpp` registers
// both ids -- the two dialects are always compiled. (An earlier CPP_PINYIN_DIALECT_* switch made
// this header conditional; it was deleted on 2026-10-01, see docs/DECISIONS.md D28.)
//
// Why the descriptions live in the facades rather than in the registry: `Mandarin` and `Cantonese`
// must keep working exactly as before even if a caller registers a dialect under the id
// "mandarin" with replace=true. The built-in facades therefore never go through the registry.

#include <cpp-pinyin2/ext/Dialect.h>

namespace cpp_pinyin::v2
{
    namespace detail
    {
        Dialect makeMandarinDialect();
        Dialect makeCantoneseDialect();
    }  // namespace detail
}  // namespace cpp_pinyin::v2
