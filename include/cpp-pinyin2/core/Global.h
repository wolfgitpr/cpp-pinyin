#pragma once

// `[[nodiscard]]` for the public functions whose return value must not be dropped:
// `u16strToUtf8str` / `utf8strToU16str` (core/U16Str.h) and the G2p accessors (core/G2pglobal.h).
//
// UNCONDITIONAL on purpose. This used to be gated on `__cplusplus >= 201703L`, which is FALSE on MSVC
// unless `/Zc:__cplusplus` is passed -- it reports `199711L` by default even while compiling as C++17 --
// so the attribute silently became a no-op on the compiler with the loudest warning for it, and no gate
// could see it (gates read the macro's NAME, not its expansion). The library requires C++17 anyway
// (`CMakeLists.txt` sets it, and these headers use `std::string_view`), so the
// condition has no remaining true-for-a-reason branch: keep it unconditional.
#define CPP_PINYIN2_NODISCARD [[nodiscard]]

// There is deliberately NO `CPP_PINYIN2_DEPRECATED`: a deprecation macro that no declaration carries is
// not a policy, and it invites retiring public names inconsistently. When a name has to go, put
// `[[deprecated("use X instead")]]` on THAT declaration and name the replacement in the message -- the
// decision belongs at the declaration, not behind a macro.

// C4251 / C4275 ARE DELIBERATELY OFF ON MSVC (2026-10-21, MEASURED on MSVC 19.51 / VS2026).
// `warning C4251: 'X' needs to have dll-interface to be used by clients` fires once per exported class that
// carries a std:: member -- Syllable, StyleDecl, Error, Segment, Result, Engine, Registry, Dialect -- and
// C4275 for `ErrorException : std::runtime_error`. ONE MSVC build of this tree emits 543 + 39 of them, all
// restating the same fact: the public types hold STL members. The only three answers are
//   (a) accept them and state the prerequisite in the header (this),
//   (b) pimpl every public type -- a rewrite of the frozen public face and its ABI, or
//   (c) leave the noise in the build.
// We take (a). The prerequisite is REAL, not a formality: a consumer must use the same toolset family, the
// same CRT and the same STL as the DLL. Measured 2026-10-21: a consumer built Debug (`/MDd`) against a
// Release `cpp-pinyin2.dll` (`/MD`) does not fail to link -- it opens the dictionary and reports "not ready"
// with an empty detail, because the std::string and Registry objects that cross the module boundary are read
// back through the other CRT's heap. `tests/install/check_install_consumer.cmake` therefore configures its
// consumer for the library's own build type.
// Scope: MSVC only, and only these two codes. The disable is NOT inside push/pop because it is emitted from a
// header that every public declaration includes, and the matching `pop` would have to be repeated at the end
// of each of them -- one forgotten `pop` would silently re-arm the whole wall of warnings. The cost, stated:
// a consumer's OWN exported class declared in the same translation unit also loses these two warnings.
#if defined(_MSC_VER)
#pragma warning(disable : 4251 4275)
#endif
