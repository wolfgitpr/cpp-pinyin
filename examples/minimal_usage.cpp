// The smallest honest usage sample: construct, query, iterate -- six lines of real code, marked `// 1`..`// 6`.
//
// The six markers are the contract (docs/DECISIONS.md AC-1: the common path must be readable in <= 6
// effective lines), and tests/cpp/test_minimal_usage.cpp counts these lines out of THIS file. Each marked
// line is a WHOLE statement (the test reads the text to the LEFT of the marker as its body), so one more
// line -- or a statement split across two -- quietly breaks the criterion while the document keeps
// claiming it.
//
// Setup only: the `Registry` (caller-owned; `Engine::open` resolves through it, there is no process-level
// registry) and the failure `if` bodies. Both failing operations answer with an `Outcome`; the failure
// BRANCH is written once, for `open`, and later ones are unwrapped at the consuming line -- that is what
// keeps the sample at six lines instead of ten.
//
// Exit codes (part of the CTest entry's contract):
//   0  ran and the result is non-empty          2  no dictionary root on the command line
//   3  `open` failed, or it opened but produced no readings -- a silent failure must not look like success
//
// Build: cmake --build <build-dir> --target MinimalUsage
// Run:   ./MinimalUsage <dictionary root>        (e.g. the build's bin/dict)
// The root is an argument on purpose: the C++ API has no default dictionary path, and two engines with two
// roots are a supported state.

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: MinimalUsage <dictionary root>\n";
        return 2;
    }

    // The registry this program owns: constructing it registers the built-in dialects, so `open` below
    // resolves "mandarin" through THIS object and through no other.
    cpp_pinyin::v2::Registry registry;

    const auto built =
        cpp_pinyin::v2::Engine::open(registry, std::filesystem::path{argv[1]}, "mandarin");  // 1  id + dictionary root
    if (!built) {  // 2  failure is a value here
        std::cerr << "open failed: " << cpp_pinyin::v2::codeText(built.error().code) << ": " << built.error().detail
                  << "\n";  //    (reported, never silent)
        return 3;
    }
    const auto answer = built.value().query(u8"你好世界");  // 3  query; `Outcome<Result>`, style defaults to TONE
    const cpp_pinyin::v2::Result result = answer.value();  // 4  unwrap -- a failed query is an error, not empty rows
    if (result.empty()) {  // 5  no readings is not success
        std::cerr << "no readings for the dictionary at " << argv[1] << "\n";  //    (reported, never silent)
        return 3;
    }
    for (const auto &item : result)  // 6  iterate: one per character
        std::cout << item.text << '=' << item.rendered << ' ';  //    reading is already styled
    std::cout << "\n" << result.join() << "\n";  //    or join the whole result

    return 0;
}
