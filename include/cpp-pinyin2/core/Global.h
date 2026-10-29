// `[[nodiscard]]` for the public functions whose return value must not be dropped:
// `u16strToUtf8str` / `utf8strToU16str` (core/U16Str.h) and the G2p accessors (core/G2pglobal.h).
//
// UNCONDITIONAL on purpose: this used to be gated on `__cplusplus >= 201703L`, which is FALSE on MSVC
// unless `/Zc:__cplusplus` is passed (it reports `199711L` by default), so the attribute became a no-op
// on the compiler with the loudest warning for it and no gate could see it (gates read the macro NAME,
// not its expansion). The library requires C++17 anyway.
#define CPP_PINYIN2_NODISCARD [[nodiscard]]

// There is deliberately NO `CPP_PINYIN2_DEPRECATED`: a macro no declaration carries is not a policy, and
// it invites retiring public names inconsistently. When a name has to go, put `[[deprecated("use X
// instead")]]` on THAT declaration -- the decision belongs at the declaration, not behind a macro.

// C4251 / C4275 are deliberately off on MSVC (measured on MSVC 19.51 / VS2026: one build emits 543 + 39,
// all restating one fact -- the public types hold STL members). The answers were: accept them and state
// the prerequisite (this), pimpl every public type (a rewrite of the frozen ABI), or leave the noise.
//
// The prerequisite is REAL: a consumer must use the same toolset family, CRT and STL as the DLL. A Debug
// (`/MDd`) consumer against the Release (`/MD`) DLL does not fail to link -- it opens the dictionary and
// reports "not ready" with an empty detail, because the strings that cross the boundary are freed through
// the other CRT's heap. `tests/install/check_install_consumer.cmake` therefore matches the build type.
//
// Scope: MSVC only, these two codes only. Not in push/pop because this header is included by every public
// declaration; a forgotten `pop` would silently re-arm the wall. Cost: a consumer's own exported class in
// the same translation unit also loses these two warnings.
#if defined(_MSC_VER)
#pragma warning(disable : 4251 4275)
#endif
