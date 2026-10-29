#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    namespace detail
    {
        /// The answer of the dictionary-manifest check: an empty `detail` means "acceptable".
        ///
        /// `code` is the classification the caller reports: `Code::Load` for every R13 format-marker
        /// refusal (that is what R13 defined), `Code::DictIncompatible` for the one D4 dimension where
        /// the data is fine but was written for a newer library -- a host can act on that by upgrading,
        /// which "load failed" cannot say.
        struct ManifestCheck {
            Code code = Code::Load;
            std::string detail;  // empty == acceptable
        };

        /// Checks a dialect directory's manifest before ANY table is read (R13, docs/STABILITY.md §4.5).
        ///
        /// Judgement table, exactly as R13 states it: `format.txt` present and not newer than
        /// `supportedFormat` -> accept; missing -> accept as v1-heritage plain text (hand-built directories
        /// need no edit); newer than supported or not a number -> refuse, naming the directory, the value
        /// found and the supported value; carries `dialect.json` (the install-tree marker) but no
        /// `format.txt` -> refuse. In that same branch a zero-byte `word.txt` is also a refusal: the marker
        /// says the generation is complete, so an empty table means truncation.
        ///
        /// D4 (docs/DECISIONS.md section 4.1, min_library) adds ONE dimension: `dialect.json` may declare
        /// `min_library`, and data written for a newer release is refused with `Code::DictIncompatible`.
        /// An empty `libraryVersion` (a build that did not define it) skips that dimension rather than
        /// guessing; a NON-empty value that is not a version is refused with `Code::Load` -- a build that
        /// misdefines its own version must not silently install data written for a newer one.
        ///
        /// `supportedFormat` is the generation this ENGINE accepts -- passed in rather than owned here so
        /// the number keeps its one definition at the facade (`src/Engine.cpp`, which
        /// `tools/check_bindings.py` parses and compares with the shipped `format.txt`).
        [[nodiscard]] ManifestCheck checkDictManifest(const std::filesystem::path &dictDir,
                                                      const std::filesystem::path &subdir, int supportedFormat,
                                                      std::string_view libraryVersion);
    }  // namespace detail
}  // namespace cpp_pinyin::v2
