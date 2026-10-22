// D2 (docs/DECISIONS.md section 4.1, "no executable-relative guessing"): a host that links this library directly -- no
// CMake package, no .pc file -- still needs to know where the build put the dictionary tables. This answers
// that from the ONE anchor a shared library really has: its own module. It never reads an environment
// variable (the shells own that, D1) and never guesses the host executable's location.
//
// The answer is built once and cached: it cannot change for the life of the process. The FIRST call
// allocates (a path, a filesystem query); every later call is a pointer copy, like cppp2_version.
//
// WHY THE `try` AROUND THE CACHE: an exception escaping an `extern "C"` function is undefined
// behaviour (in practice `std::terminate`), and both `std::filesystem` here and the `std::string`
// allocations can throw `bad_alloc`. Every other C entry point in this library catches for the same
// reason; this one was the only export that allocated without a fence (2026-10-20 audit, G02).

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/c_api.h>

#include <algorithm>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace
{
    /// Where this build installs the tables, relative to the install prefix. CMake passes the real value
    /// (`CMAKE_INSTALL_DATADIR`/cpp-pinyin2/dict, "share" on every platform this repository builds for);
    /// the fallback exists so a non-CMake build still answers with the documented layout.
    constexpr const char *kDictSubdir =
#ifdef CPP_PINYIN2_DICT_SUBDIR
        CPP_PINYIN2_DICT_SUBDIR;
#else
        "share/cpp-pinyin2/dict";
#endif

    /// The directory holding this module, or an empty path when there is no usable anchor.
    std::filesystem::path moduleDirectory() {
#if defined(CPP_PINYIN2_STATIC)
        // No anchor: a static library is code inside the host, so "this module" would resolve to the host
        // executable. Reporting that would be the executable guess D1 forbids, so say "unknown" instead.
        return {};
#elif defined(_WIN32)
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(&moduleDirectory), &module) == 0)
            return {};
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD written = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
                return {};
            if (written < buffer.size()) {
                buffer.resize(written);
                break;
            }
            buffer.resize(buffer.size() * 2);  // the path grew between the call and the copy: try again
        }
        return std::filesystem::path(buffer).parent_path();
#elif defined(__unix__) || defined(__APPLE__)
        Dl_info info{};
        if (::dladdr(reinterpret_cast<const void *>(&moduleDirectory), &info) == 0 || info.dli_fname == nullptr)
            return {};
        // dli_fname can be relative (it is whatever the loader was given); canonicalise when possible so the
        // answer does not depend on the caller's working directory, and fall back to the raw name otherwise.
        std::error_code ec;
        const std::filesystem::path self = std::filesystem::weakly_canonical(info.dli_fname, ec);
        return (ec ? std::filesystem::path(info.dli_fname) : self).parent_path();
#else
        return {};
#endif
    }

    /// Empty when there is no anchor; otherwise the installed dictionary root, lexically normalised.
    std::string computeCandidate() {
        const std::filesystem::path moduleDir = moduleDirectory();
        if (moduleDir.empty())
            return {};
        // Installed layout: <prefix>/bin/<module> (Windows) or <prefix>/lib*/<module> (POSIX), with the
        // tables at <prefix>/<subdir>. Both module homes sit one level under the prefix, so ".." serves both.
        std::string text = (moduleDir / ".." / kDictSubdir).lexically_normal().u8string();
        std::replace(text.begin(), text.end(), '\\', '/');  // one separator in the answer on every platform
        if (!text.empty() && text.back() == '/')
            text.pop_back();
        return text;
    }
}  // namespace

namespace cpp_pinyin::v2
{
    std::string dictDirCandidate() {
        // The ONE cache. The C face below copies this value once so it can hand out a `const char *`
        // with process lifetime; neither face recomputes, and neither can disagree with the other.
        static const std::string candidate = computeCandidate();
        return candidate;
    }
}  // namespace cpp_pinyin::v2

extern "C" {
const char *cppp2_dict_dir_candidate(void) {
    // `catch (...)` and not `catch (const std::exception &)`: the promise to the C caller is "this
    // function does not throw", and a `bad_alloc` is exactly the case that must not take the process
    // down. A throw out of the initialisation leaves the static uninitialised, so the next call
    // retries instead of returning a half-built path (C++ [stmt.dcl]/4).
    try {
        static const std::string candidate = cpp_pinyin::v2::dictDirCandidate();
        return candidate.empty() ? nullptr : candidate.c_str();
    }
    catch (...) {
        return nullptr;
    }
}
}
