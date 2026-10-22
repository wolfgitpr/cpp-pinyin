#pragma once

#include <cstddef>
#include <filesystem>

namespace cpp_pinyin::v2::detail
{
    /// One table compiled into the library by CPP_PINYIN2_EMBED_DICTS, or `{nullptr, 0}` when this build
    /// embeds nothing or nothing matches. The bytes are the file's bytes exactly: no parsing happens here.
    struct EmbeddedTable {
        const char *data;
        std::size_t size;
    };

    /// The embedded copy of the TABLE `requested` names, matched by its last two path components
    /// (`<dialect>/<file>`). The caller's dictionary root is not compared: a build that embeds its data has to
    /// answer for whatever root the host passed, including one that does not exist.
    ///
    /// Two builds provide this: `src/dict/EmbeddedTables.cpp` (answers `{nullptr, 0}` -- the default) and the
    /// generated `embedded_dialects.cpp` (tools/embed_dialects.py). Exactly one of them is compiled in.
    EmbeddedTable findEmbeddedTable(const std::filesystem::path &requested);
}  // namespace cpp_pinyin::v2::detail
