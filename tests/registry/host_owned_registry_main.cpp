// The registry's two structural constraints, as a program rather than as prose. (Until 2026-10-05 the claim
// was "registration is link-time" -- a static-initialized `bool` per translation unit, droppable by the
// linker. That macro is gone: the registry is owned by the caller, so "who registered this id" is answered
// by the object passed to `Engine::open`, not by the link map.) What survives, pinned here both ways:
//
//   1. `resolve`/`Engine::open` see exactly what was registered INTO THE REGISTRY THEY WERE GIVEN. The two
//      runs differ in one thing -- whether the caller called `add`; the second is the negative control,
//      without which "the id resolves" would also pass on a built-in fallback or a process-level table.
//   2. an unknown id is an ERROR: `resolve` returns nullptr and `Engine::open` reports it, naming the id,
//      without falling back to a built-in dialect.
//
// The duplicate guard (`add` with `replace=false` on a taken id returns false) is pinned in-process by
// tests/cpp/test_engine_dialect.cpp, "registry semantics"; this program stays small on purpose, because its
// subject is the id -> dialect path.

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace
{

    constexpr std::string_view kProbeId = "e2e-host-registered";

    std::shared_ptr<const cpp_pinyin::v2::Dialect> makeProbeDialect() {
        auto dialect = std::make_shared<cpp_pinyin::v2::Dialect>();
        dialect->id = std::string{kProbeId};
        // The tables are never loaded: this test is about the id -> dialect path, not about
        // querying. Pointing dictSubDir at the shipped mandarin tables keeps it inert but valid if it
        // were ever loaded.
        dialect->dictSubDir = "mandarin";
        return dialect;
    }

}  // namespace

int main(int argc, char **argv) {
    const bool expectRegistered = argc > 1 && std::strcmp(argv[1], "--expect-registered") == 0;
    const bool expectAbsent = argc > 1 && std::strcmp(argv[1], "--expect-absent") == 0;
    if (!expectRegistered && !expectAbsent) {
        std::fprintf(stderr, "usage: HostOwnedRegistry (--expect-registered | --expect-absent)\n");
        return 2;
    }

    // A registry the caller declares. It carries the two built-in dialects because construction
    // registers them; the probe is present only because the caller said so.
    cpp_pinyin::v2::Registry registry;
    if (expectRegistered) {
        if (!registry.add(std::string{kProbeId}, makeProbeDialect)) {
            std::fprintf(stderr, "FAIL: adding '%s' to a fresh registry was refused\n", std::string{kProbeId}.c_str());
            return 1;
        }
    }

    const bool present = registry.resolve(kProbeId) != nullptr;
    if (expectRegistered && !present) {
        std::fprintf(stderr, "FAIL: '%s' did not resolve although the caller registered it\n",
                     std::string{kProbeId}.c_str());
        return 1;
    }
    if (expectAbsent && present) {
        std::fprintf(stderr,
                     "FAIL: '%s' resolved although the caller never registered it -- the negative "
                     "control is broken, so the positive case proves nothing.\n",
                     std::string{kProbeId}.c_str());
        return 1;
    }

    // The same path `Engine::open` takes, in both directions. The root is the one CTest puts in this
    // working directory (bin/dict), the same root tests/cpp/test_engine_dialect.cpp uses.
    const std::filesystem::path dictRoot = std::filesystem::current_path() / "dict";
    cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened = cpp_pinyin::v2::Engine::open(registry, dictRoot, kProbeId);
    if (expectRegistered) {
        if (!opened.has_value()) {
            std::fprintf(stderr, "FAIL: the registered dialect did not produce a usable engine: %s\n",
                         opened.error().detail.c_str());
            return 1;
        }
        cpp_pinyin::v2::Engine engine = std::move(opened).value();
        const auto probe = engine.query(u8"你");
        if (!probe) {
            std::fprintf(stderr, "FAIL: the registered dialect produced an engine that cannot answer: %s\n",
                         probe.error().detail.c_str());
            return 1;
        }
        if (engine.dialect().id != kProbeId) {
            std::fprintf(stderr, "FAIL: the engine was built from '%s' instead of '%s'\n", engine.dialect().id.c_str(),
                         std::string{kProbeId}.c_str());
            return 1;
        }
    } else {
        if (opened.has_value()) {
            std::fprintf(stderr, "FAIL: an unregistered id produced a usable engine\n");
            return 1;
        }
        // Not just "some error": the failure has to be CLASSIFIED as the object-state failure
        // (c_api.h's "an unknown id is NOT_READY") and the detail has to name the id, or a caller
        // cannot tell a typo from a dictionary that failed to load. Both used to be read off
        // `lastError()`; they now come out of the one error channel, with the same wording.
        if (opened.error().code != cpp_pinyin::v2::Code::NotReady) {
            std::fprintf(stderr, "FAIL: an unregistered id was classified as code %d instead of NotReady\n",
                         static_cast<int>(opened.error().code));
            return 1;
        }
        if (opened.error().detail.empty()) {
            std::fprintf(stderr, "FAIL: an unregistered id failed without saying why\n");
            return 1;
        }
        if (opened.error().detail.find(kProbeId) == std::string::npos) {
            std::fprintf(stderr, "FAIL: the error does not name the id that failed: %s\n",
                         opened.error().detail.c_str());
            return 1;
        }
    }

    // No fallback, in either mode: the built-ins are reachable, an unknown id is not.
    if (registry.resolve("definitely-not-registered") != nullptr) {
        std::fprintf(stderr, "FAIL: an unknown id resolved -- the registry has a default dialect\n");
        return 1;
    }
    if (registry.resolve("mandarin") == nullptr) {
        std::fprintf(stderr, "FAIL: a fresh registry does not carry the built-in dialects\n");
        return 1;
    }

    std::printf("OK: '%s' is %s (expected %s); 'mandarin' is built in and an unknown id is not\n",
                std::string{kProbeId}.c_str(), present ? "registered" : "absent",
                expectRegistered ? "registered" : "absent");
    return 0;
}
