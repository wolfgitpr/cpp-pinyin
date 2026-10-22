#include "EmbeddedTables.h"

// The default provider: a build that does NOT embed the dictionaries (CPP_PINYIN2_EMBED_DICTS=OFF) compiles
// this file instead of the generated one, so the loader's fallback is always linked and never a no-op macro.
// Keeping it a real translation unit (rather than an `#ifdef` inside the loader) is what makes the two builds
// differ in ONE place: which definition of this function ends up in the library.

namespace cpp_pinyin::v2::detail
{
    EmbeddedTable findEmbeddedTable(const std::filesystem::path &) { return {nullptr, 0}; }
}  // namespace cpp_pinyin::v2::detail
