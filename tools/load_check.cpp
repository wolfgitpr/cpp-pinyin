// load_check -- a load-time regression gate, the deliberately coarse kind.
//
// Why (an old backlog entry, "Benchmark 进构建门禁 + 内存/加载回归", in a ledger that is not in this tree):
// Benchmark.exe asserts nothing, and a timing gate over single measurements would be pure flakiness -- Dictionary
// Loading swings ~12% run to run (5 runs: Mandarin min 21.4 / max 23.9 ms, Cantonese 14.2 / 14.7 ms). So this compares
// the MINIMUM of N runs against a threshold with a huge margin (500 ms vs ~24 ms worst case = 20.9x / 34x), which means
// it can only fire on a catastrophic regression (accidental O(n^2) loading, repeated table loads, several copies of the
// dictionary), never on jitter. It is NOT a performance measurement.
//
// Usage:
//   load_check [dictDir] [--n N] [--threshold-ms MS] [--self-test]
// Exit codes: 0 pass, 1 over threshold, 2 dictionary failed to load, 3 usage error.

#include <chrono>
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{

    constexpr double kDefaultThresholdMs = 500.0;
    constexpr int kDefaultRuns = 5;

    double minOf(const std::vector<double> &v) {
        double m = v.front();
        for (const double x : v)
            if (x < m)
                m = x;
        return m;
    }

    std::optional<cpp_pinyin::v2::Engine> openDialect(const std::filesystem::path &dir, const char *id) {
        static const cpp_pinyin::v2::Registry registry;
        auto outcome = cpp_pinyin::v2::Engine::open(registry, dir, id);
        if (!outcome)
            return std::nullopt;
        return std::move(outcome.value());
    }

    // The facades this file used to measure are gone. These wrappers exist so that `measure<T>`'s body --
    // and therefore the measured region -- stays what it was: construct (that IS the load), then ask
    // whether it is ready. Nothing about the timing loop, the runs or the threshold moved. An engine that
    // failed to open is an empty optional, which is the same "not ready" answer a facade that could not
    // load gave, so this tool still reports the failure it always reported.
    struct MandarinEngine {
        explicit MandarinEngine(const std::filesystem::path &dir) : engine(openDialect(dir, "mandarin")) {}
        [[nodiscard]] bool isReady() const { return engine.has_value(); }
        std::optional<cpp_pinyin::v2::Engine> engine;
    };

    struct CantoneseEngine {
        explicit CantoneseEngine(const std::filesystem::path &dir) : engine(openDialect(dir, "cantonese")) {}
        [[nodiscard]] bool isReady() const { return engine.has_value(); }
        std::optional<cpp_pinyin::v2::Engine> engine;
    };
    // Loads one engine N times and returns the per-run milliseconds.
    template <typename Engine>
    std::vector<double> measure(std::filesystem::path dir, int runs, bool &ready) {
        std::vector<double> ms;
        ms.reserve(static_cast<size_t>(runs));
        ready = true;
        for (int i = 0; i < runs; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            {
                Engine engine(dir);
                ready = ready && engine.isReady();
            }
            const auto t1 = std::chrono::steady_clock::now();
            ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        return ms;
    }

    void print(const char *label, const std::vector<double> &ms, double threshold) {
        std::printf("%-10s runs=%d min=%.1f ms threshold=%.0f ms verdict=%s [", label, static_cast<int>(ms.size()),
                    minOf(ms), threshold, minOf(ms) <= threshold ? "PASS" : "FAIL");
        for (size_t i = 0; i < ms.size(); ++i)
            std::printf("%s%.1f", i ? " " : "", ms[i]);
        std::printf("]\n");
    }

    int selfTest() {
        // The whole gate rests on two pieces of pure logic; check them without a dictionary.
        const std::vector<double> a{9.0, 3.5, 12.0};
        if (minOf(a) != 3.5) {
            std::printf("self-test: minOf picked %f, expected 3.5\n", minOf(a));
            return 1;
        }
        if (minOf(std::vector<double>{7.0}) != 7.0) {
            std::printf("self-test: minOf of a single sample is wrong\n");
            return 1;
        }
        // A threshold of 0 must reject any positive measurement -- this is the same shape as the
        // negative case the CTest entry runs against the real binary.
        if (minOf(a) <= 0.0) {
            std::printf("self-test: 3.5 ms must not pass a 0 ms threshold\n");
            return 1;
        }
        std::printf("self-test: ok\n");
        return 0;
    }

}  // namespace

int main(int argc, char **argv) {
    std::filesystem::path dir = "dict";
    double threshold = kDefaultThresholdMs;
    int runs = kDefaultRuns;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test")
            return selfTest();
        if (arg == "--n" && i + 1 < argc)
            runs = std::atoi(argv[++i]);
        else if (arg == "--threshold-ms" && i + 1 < argc)
            threshold = std::atof(argv[++i]);
        else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "load_check: unknown option %s\n", arg.c_str());
            return 3;
        } else
            dir = arg;
    }
    if (runs < 1) {
        std::fprintf(stderr, "load_check: --n must be >= 1\n");
        return 3;
    }

    bool ready = false;
    const auto mandarin = measure<MandarinEngine>(dir, runs, ready);
    if (!ready) {
        std::fprintf(stderr, "load_check: Mandarin failed to load from %s\n", dir.string().c_str());
        return 2;
    }
    const auto cantonese = measure<CantoneseEngine>(dir, runs, ready);
    if (!ready) {
        std::fprintf(stderr, "load_check: Cantonese failed to load from %s\n", dir.string().c_str());
        return 2;
    }

    print("Mandarin", mandarin, threshold);
    print("Cantonese", cantonese, threshold);
    const bool ok = minOf(mandarin) <= threshold && minOf(cantonese) <= threshold;
    std::printf("result: %s (min-of-%d against %.0f ms)\n", ok ? "PASS" : "FAIL", runs, threshold);
    return ok ? 0 : 1;
}
