#pragma once

#include <cpp-pinyin2/core/Global.h>

// Both Windows toolchains mark exports with __declspec: MSVC obviously, and MinGW too -- there is
// no ELF-style visibility on PE, so `__attribute__((visibility("default")))` means NOTHING to a PE
// linker. Measured 2026-10-01: with MinGW falling through to the attribute branch, the shared
// library had no explicit exports at all, so GNU ld applied its default "export every global
// symbol" rule and the DLL exported 250+ symbols -- including internal ones (EngineImpl,
// PhraseGate). tests/abi/check_exports.cmake is the gate that caught it; keep MinGW on this branch.
#ifdef _MSC_VER
#define CPP_PINYIN2_DECL_EXPORT __declspec(dllexport)
#define CPP_PINYIN2_DECL_IMPORT __declspec(dllimport)
#elif defined(__MINGW32__)
#define CPP_PINYIN2_DECL_EXPORT __declspec(dllexport)
#define CPP_PINYIN2_DECL_IMPORT __declspec(dllimport)
#else
#define CPP_PINYIN2_DECL_EXPORT __attribute__((visibility("default")))
#define CPP_PINYIN2_DECL_IMPORT __attribute__((visibility("default")))
#endif

#ifndef CPP_PINYIN2_EXPORT
#ifdef CPP_PINYIN2_STATIC
#define CPP_PINYIN2_EXPORT
#else
#ifdef CPP_PINYIN2_LIBRARY
#define CPP_PINYIN2_EXPORT CPP_PINYIN2_DECL_EXPORT
#else
#define CPP_PINYIN2_EXPORT CPP_PINYIN2_DECL_IMPORT
#endif
#endif
#endif
