#pragma once

// CPP_PINYIN2_VERSION_* -- the COMPILE-TIME half of the version, so a consumer can write `#if`
// instead of calling a function at run time. The run-time half already exists (`cppp2_version()`);
// this header is what fmt/spdlog/zstd/utf8proc all ship and this library did not (2026-10-19 audit,
// finding F15: the only version string in the tree was a PRIVATE compile definition, so no consumer
// could branch on it).
//
// WHY THESE ARE PLAIN #define AND NOT A GENERATED FILE: the public headers are installed by a
// directory copy (`install(DIRECTORY include/cpp-pinyin2 ...)`) and every gate that freezes the
// public face works on the paths under include/. A configure_file()-generated header would need an
// extra install rule, a binary-directory include path and an exemption from each of those gates --
// a second mechanism for one file. Instead the numbers are written here ONCE and checked against
// `project(cpp-pinyin2 VERSION x.y.z)` by tools/check_version_tokens.py (axis 9), which refuses the
// tree when they disagree. Drift is impossible; a hand edit without a bump is loud.
//
// The numbers are the PACKAGE version, not an ABI version: 2.x is the same compatibility level
// (docs/CONTRACT.md section 1) and the same binary generation (docs/STABILITY.md R9).

#define CPP_PINYIN2_VERSION_MAJOR 2
#define CPP_PINYIN2_VERSION_MINOR 0
#define CPP_PINYIN2_VERSION_PATCH 0

// String form. Keep in step with the numbers above: the same gate compares it, so a half-edit
// ("bump MAJOR, forget the string") is red rather than silently reported by `cppp2_version()`.
#define CPP_PINYIN2_VERSION_STRING "2.0.0"

// `#if CPP_PINYIN2_VERSION_AT_LEAST(2, 1, 0)` -- the reason this header exists. Integer comparison,
// no strings, works in the preprocessor everywhere. No `ULL` suffix, so the same spelling is legal
// in C and in -Wpedantic builds.
#define CPP_PINYIN2_VERSION_NUMBER(major, minor, patch) ((major) * 10000 + (minor) * 100 + (patch))
#define CPP_PINYIN2_VERSION_AT_LEAST(major, minor, patch)                                                              \
    (CPP_PINYIN2_VERSION_NUMBER(CPP_PINYIN2_VERSION_MAJOR, CPP_PINYIN2_VERSION_MINOR, CPP_PINYIN2_VERSION_PATCH) >=    \
     CPP_PINYIN2_VERSION_NUMBER(major, minor, patch))
