#pragma once

// Test-only fixtures for the two dialects every C++ test uses (C1+D7).
//
// The `cpp_pinyin::v2::Mandarin` / `cpp_pinyin::v2::Cantonese` facades are being deleted: `Engine` is the one
// entry point now, and the tests must exercise IT rather than a facade that ships separately. What the
// tests still need is the one thing the facades did for them -- open the dictionaries they keep in
// `<cwd>/dict` -- so that lives here, once, instead of in eighteen files.
//
// Not installed, not part of the frozen public header list, not an ABI surface: this is test
// scaffolding, and a test is free to include it by relative path. A file that needs to see a FAILED
// construction must call `Engine::open` itself and look at the `Outcome` -- this helper asserts
// success by taking `value()`, which is exactly what a fixture should do.

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <filesystem>
#include <string_view>
#include <utility>

namespace cpp_test
{
    /// The engine under test for `id`, whose dictionaries live under `root` (the directory that
    /// contains the dialect's own subdirectory -- `<cwd>/dict` for every C++ test here).
    inline cpp_pinyin::v2::Engine openDialect(std::string_view id, const std::filesystem::path &root) {
        // A fresh registry per call, deliberately. `Engine::open` takes the dialect by `shared_ptr`
        // and keeps it alive itself (`Engine.h`'s private constructor), so this registry may die right
        // here -- and it should, because the two facades this replaces each built their own dialect
        // instance. Sharing ONE dialect across every engine in the test binary is a different design
        // with different observable behaviour, and not something this fixture needs to answer.
        const cpp_pinyin::v2::Registry registry;
        auto outcome = cpp_pinyin::v2::Engine::open(registry, root, id);
        return std::move(outcome.value());
    }

    inline cpp_pinyin::v2::Engine openDialect(std::string_view id) {
        return openDialect(id, std::filesystem::current_path() / "dict");
    }

    inline cpp_pinyin::v2::Engine mandarin() { return openDialect("mandarin"); }

    inline cpp_pinyin::v2::Engine cantonese() { return openDialect("cantonese"); }
}  // namespace cpp_test
