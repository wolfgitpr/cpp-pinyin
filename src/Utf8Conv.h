#pragma once

// The library speaks two encodings, and this header is where the vocabulary for that lives.
//
// RULE (docs/DECISIONS.md, D41): UTF-16 is the dialect of the inside -- the dictionary is indexed by code
// unit, so one Han character is one index -- and UTF-8 is the dialect of every boundary: the C ABI, the
// dictionary files loaded at startup, and the `Result`/`Syllable` fields a caller reads. A translation unit
// should speak ONE of them; where it must speak both, the conversions belong at the edge of that unit.
// Nothing enforces that rule mechanically yet -- it is stated here and in D41, which also names the eight files
// that do speak both, so a ninth one is a review question rather than a silent growth.
//
// WHY `toU16` TAKES `const std::string &` AND NOT `std::string_view`. Seventeen call sites pass a `std::string`
// that already exists, and wrapping those hides nothing. Four pass a `std::string_view`, and each of them
// spells out `std::string(view)` because it really does allocate; an overload taking a view would have turned
// those four into the same four words as the other seventeen and hidden the copy. A short name is worth having
// only when the thing behind it is free -- so the view sites keep their allocation in plain sight:
//
//     const auto u16 = utf8strToU16str(std::string(utf8));   // view in, copy out: still visible
//     out.spelling = toUtf8(stored);                         // string in, string out: nothing hidden
//
// NOT INSTALLED. This is `src/`, not `include/`: the three functions below are not part of the public surface,
// not exported, and not in any frozen-surface manifest. That is the point -- the public headers keep their
// long, explicit names, and the library's own code gets to read like prose.

#include <string>

#include <cpp-pinyin2/core/U16Str.h>

namespace cpp_pinyin::v2
{
    /// UTF-16 code unit -> UTF-8 bytes. A lone surrogate becomes U+FFFD (the public function is total).
    [[nodiscard]] inline std::string toUtf8(const char16_t codeUnit) { return u16strToUtf8str(codeUnit); }

    /// UTF-16 string -> UTF-8 bytes. Same totality, same replacement rule.
    [[nodiscard]] inline std::string toUtf8(const std::u16string &u16str) { return u16strToUtf8str(u16str); }

    /// UTF-8 bytes -> UTF-16. Takes an existing string by reference: a `string_view` overload would hide the
    /// copy that the four view-taking call sites actually pay (see the note above).
    [[nodiscard]] inline std::u16string toU16(const std::string &utf8str) { return utf8strToU16str(utf8str); }
}  // namespace cpp_pinyin::v2
