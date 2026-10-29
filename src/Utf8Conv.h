#pragma once

// Two encodings meet here; `docs/DECISIONS.md` D41 is the rule for where each may live (UTF-16 inside,
// UTF-8 at every boundary) and it names the units that must speak both, so a new one is a review question
// rather than silent growth. This header does not restate that rule.
//
// WHY `toU16` TAKES `const std::string &` AND NOT `std::string_view`: seventeen call sites pass a
// `std::string` that already exists, and four pass a view whose conversion really does allocate. An
// overload taking a view would have hidden that copy behind the same four words as the other seventeen.
// A short name is worth it only when the thing behind it is free:
//
//     const auto u16 = utf8strToU16str(std::string(utf8));   // view in, copy out: still visible
//     out.spelling = toUtf8(stored);                         // string in, string out: nothing hidden

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
