#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cpp/dialect_fixtures.h"

// Benchmark: what the library costs per API path, comparable between runs -- the per-section table a
// human reads (format unchanged, so quoted output stays valid), a `--json` report + greppable marker
// line for archiving/diffing, and `--compare <baseline.json>` per-case ratios for the A/B the board work
// did by hand (PERFORMANCE.md §4.18). Cases are warm and measure ~50 ms (a 22 us case x200 measures the
// scheduler); `--repeat N` reports the best run -- min-of-N, as that A/B did -- because measured
// 2026-09-29 over eight identical runs per-case min moves 0.55% where the median moves 25% (§2.4), and a
// ratio inside the printed band is "tied". No timing assertion lives here on purpose: a gate would fail
// on that noise, so CTest only checks this still runs (BenchmarkSmoke).

namespace
{

    struct BenchResult {
        std::string name;
        size_t iterations = 0;
        size_t repeats = 1;
        double total_ms = 0;
        double mean_us = 0;
        double median_us = 0;
        double p90_us = 0;
        double min_us = 0;
        double max_us = 0;
        double stddev_us = 0;
        // (max - min) / min over the repeats' medians, in percent: how stable the case is run to run.
        // 0 when --repeat 1 (the value then says "not measured", not "perfectly stable").
        double repeat_spread_pct = 0;
        // Cases whose cost is dominated by I/O and page cache rather than by our code (dictionary
        // loading). Measured: load time swings ~12% run to run even at min-of-N (docs/BACKLOG.md,
        // LoadCheck used a ~20x margin for the same reason), so a comparison must not judge them with
        // the +/-2% band it uses for the rest.
        bool noisy = false;
        double throughput = 0;
        std::string throughput_unit;
    };

    struct RunStats {
        double total_ms = 0;
        double mean_us = 0;
        double median_us = 0;
        double p90_us = 0;
        double min_us = 0;
        double max_us = 0;
        double stddev_us = 0;
    };

    static std::vector<BenchResult> g_results;
    static bool g_verbose = false;
    static bool g_brief = false;
    static size_t g_repeats = 1;

    using Clock = std::chrono::high_resolution_clock;
    using Ms = std::chrono::duration<double, std::milli>;
    using Us = std::chrono::duration<double, std::micro>;

    template <typename Func>
    static double measure_once_us(Func &&fn) {
        auto start = Clock::now();
        fn();
        return Us(Clock::now() - start).count();
    }

    // Nearest-rank percentile over an already sorted vector.
    static double percentile(const std::vector<double> &sorted, double q) {
        if (sorted.empty())
            return 0;
        size_t idx = static_cast<size_t>(q * static_cast<double>(sorted.size() - 1) + 0.5);
        if (idx >= sorted.size())
            idx = sorted.size() - 1;
        return sorted[idx];
    }

    template <typename Func>
    static RunStats measure_run(size_t iterations, Func &&fn) {
        std::vector<double> timings;
        timings.reserve(iterations);
        for (size_t i = 0; i < iterations; i++)
            timings.push_back(measure_once_us(fn));
        std::sort(timings.begin(), timings.end());

        RunStats s;
        s.total_ms = std::accumulate(timings.begin(), timings.end(), 0.0) / 1000.0;
        s.mean_us = (s.total_ms * 1000.0) / static_cast<double>(iterations);
        s.median_us = percentile(timings, 0.50);
        s.p90_us = percentile(timings, 0.90);
        s.min_us = timings.front();
        s.max_us = timings.back();

        double sum_sq = 0;
        for (double t : timings) {
            double d = t - s.mean_us;
            sum_sq += d * d;
        }
        s.stddev_us = std::sqrt(sum_sq / static_cast<double>(iterations));
        return s;
    }

    // Best of g_repeats runs, judged by median (the statistic this file reports everywhere else).
    template <typename Func>
    static BenchResult bench(const std::string &name, size_t iterations, Func &&fn) {
        const size_t warmup = std::max<size_t>(1, iterations / 10);
        for (size_t i = 0; i < warmup; i++)
            fn();

        RunStats best;
        bool have_best = false;
        double spread_lo = 0, spread_hi = 0;
        for (size_t rep = 0; rep < g_repeats; rep++) {
            const RunStats s = measure_run(iterations, fn);
            if (!have_best || s.median_us < best.median_us)
                best = s;
            if (rep == 0 || s.median_us < spread_lo)
                spread_lo = s.median_us;
            if (rep == 0 || s.median_us > spread_hi)
                spread_hi = s.median_us;
            have_best = true;
        }

        BenchResult r;
        r.name = name;
        r.iterations = iterations;
        r.repeats = g_repeats;
        r.total_ms = best.total_ms;
        r.mean_us = best.mean_us;
        r.median_us = best.median_us;
        r.p90_us = best.p90_us;
        r.min_us = best.min_us;
        r.max_us = best.max_us;
        r.stddev_us = best.stddev_us;
        r.repeat_spread_pct = (g_repeats > 1 && spread_lo > 0) ? 100.0 * (spread_hi - spread_lo) / spread_lo : 0.0;
        return r;
    }

    static std::string fmtDuration(double us) {
        double ms = us / 1000.0;
        std::ostringstream oss;
        if (ms < 0.01)
            oss << std::fixed << std::setprecision(3) << ms << " ms";
        else if (ms < 1.0)
            oss << std::fixed << std::setprecision(2) << ms << " ms";
        else if (ms < 100.0)
            oss << std::fixed << std::setprecision(1) << ms << " ms";
        else
            oss << std::fixed << std::setprecision(0) << ms << " ms";
        return oss.str();
    }

    static std::string fmtCount(double val) {
        std::ostringstream oss;
        if (val >= 1e9)
            oss << std::fixed << std::setprecision(1) << (val / 1e9) << "G";
        else if (val >= 1e6)
            oss << std::fixed << std::setprecision(1) << (val / 1e6) << "M";
        else if (val >= 1e3)
            oss << std::fixed << std::setprecision(1) << (val / 1e3) << "K";
        else
            oss << std::fixed << std::setprecision(0) << val;
        return oss.str();
    }

    static std::string fmtThroughput(double val, const std::string &unit) { return fmtCount(val) + " " + unit; }

    // Compact, round-trippable number for the JSON report: no trailing zero padding, and std::strtod
    // (which the --compare reader uses) accepts whatever this produces, including 1e-05.
    static std::string fmtJson(double val) {
        std::ostringstream oss;
        oss << std::setprecision(6) << val;
        return oss.str();
    }

    static std::string jsonEscape(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\')
                out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    static size_t utf8CharCount(const std::string &s) {
        size_t count = 0;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = s[i];
            if (c < 0x80)
                i += 1;
            else if (c < 0xE0)
                i += 2;
            else if (c < 0xF0)
                i += 3;
            else
                i += 4;
            count++;
        }
        return count;
    }

    static constexpr int COL_W = 86;
    static void printSep(char ch = '-') { std::cout << std::string(COL_W, ch) << std::endl; }
    static void printHeader(const std::string &title) {
        if (g_brief)
            return;
        std::cout << std::endl;
        printSep('=');
        std::cout << "  " << title << std::endl;
        printSep('=');
    }

    // A line that belongs to a section rather than to a result row (e.g. "callback calls: ...").
    static void say(const std::string &text) {
        if (!g_brief)
            std::cout << "  " << text << std::endl;
    }

    static void record(const BenchResult &r) {
        g_results.push_back(r);
        if (g_brief)
            return;
        std::cout << "  " << std::left << std::setw(36) << r.name << std::right;
        std::cout << std::setw(10) << fmtDuration(r.median_us);
        if (g_verbose) {
            std::cout << "    +-" << std::setw(8) << fmtDuration(r.stddev_us);
            if (r.repeats > 1)
                std::cout << "  spread " << std::fixed << std::setprecision(2) << r.repeat_spread_pct << "%";
        }
        if (r.throughput > 0)
            std::cout << "    " << std::setw(16) << fmtThroughput(r.throughput, r.throughput_unit);
        std::cout << std::endl;
    }

    static void benchDictLoading() {
        printHeader("Dictionary Loading");
        {
            auto r = bench("Mandarin", 20,
                           []()
                           {
                               auto p = std::make_unique<cpp_pinyin::v2::Engine>(cpp_test::mandarin());
                               volatile bool ok = !p->dialect().id.empty();
                               (void)ok;
                           });
            r.noisy = true;
            record(r);
        }
        {
            auto r = bench("Cantonese", 20,
                           []()
                           {
                               auto p = std::make_unique<cpp_pinyin::v2::Engine>(cpp_test::cantonese());
                               volatile bool ok = !p->dialect().id.empty();
                               (void)ok;
                           });
            r.noisy = true;
            record(r);
        }
    }

    static void benchThroughput(cpp_pinyin::v2::Engine &man) {
        printHeader("Throughput by Input Length");

        const std::string base = u8"明月几时有把酒问青天不知天上宫阙今夕是何年"
                                 u8"我欲乘风归去又恐琼楼玉宇高处不胜寒"
                                 u8"起舞弄清影何似在人间转朱阁低绮户照无眠"
                                 u8"不应有恨何事长向别时圆人有悲欢离合月有阴晴圆缺"
                                 u8"此事古难全但愿人长久千里共婵娟";

        struct TC {
            std::string name;
            std::string input;
            size_t iters;
        };
        // Iteration counts are sized so that each case measures for ~50 ms, not for a few: a 22 us case
        // run 200 times is 4.4 ms of work, and one scheduler interruption inside it moves the min
        // (measured 2026-09-29: the 95-char case swung 22.4 -> 35.4 us across five identical runs at
        // 200 iterations, 58%). The same five runs at these counts stay inside the band the comparison
        // uses, and the whole suite still costs about the same wall time, because (cases x 50 ms) is
        // less than what the table above prints.
        std::vector<TC> cases = {
            {"1 char", u8"明", 100000},
            {"5 chars", u8"明月几时有", 50000},
            {"10 chars", std::string(base, 0, 30), 20000},
            {"50 chars", base.substr(0, 150), 5000},
            {"95 chars", base, 2000},
            {"Mixed", u8"Hello世界！test测试123数据data", 20000},
        };

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;

        for (auto &tc : cases) {
            size_t n = utf8CharCount(tc.input);
            auto r = bench(tc.name + " (" + std::to_string(n) + ")", tc.iters,
                           [&]() { volatile auto res = man.query(tc.input, cfg).value(); });
            r.throughput = (static_cast<double>(n) / r.median_us) * 1e6;
            r.throughput_unit = "chars/s";
            record(r);
        }
    }

    static void benchToneStyles(cpp_pinyin::v2::Engine &man) {
        printHeader("Tone Style Comparison (27 chars)");

        const std::string input = u8"人有悲欢离合月有阴晴圆缺此事古难全但愿人长久千里共婵娟";
        // ~50 ms per case at ~5.7 us per query, for the reason spelled out in benchThroughput.
        const size_t iters = 10000;

        struct SC {
            std::string name;
            cpp_pinyin::v2::MandarinStyle style;
        };
        std::vector<SC> styles = {
            {"NORMAL", cpp_pinyin::v2::MandarinStyle::NORMAL},
            {"TONE", cpp_pinyin::v2::MandarinStyle::TONE},
            {"TONE2", cpp_pinyin::v2::MandarinStyle::TONE2},
            {"TONE3", cpp_pinyin::v2::MandarinStyle::TONE3},
        };

        for (auto &sc : styles) {
            auto r = bench(sc.name, iters,
                           [&]()
                           {
                               cpp_pinyin::v2::EngineConfig cfg;
                               cfg.style = sc.style;
                               volatile auto res = man.query(input, cfg).value();
                           });
            record(r);
        }

        say("");
        say("candidates enabled vs disabled:");

        {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
            cfg.includeCandidates = true;
            auto r = bench("candidates=true", iters, [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
        }
        {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
            cfg.includeCandidates = false;
            auto r = bench("candidates=false", iters, [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
        }

        // The S8 switch, priced on its own: same input, same style, same candidate setting, ONE field
        // different. This pair is what the "what does the structure cost" question is answered with --
        // the default (`false`) is the query path this project's performance口径 measures, and `true` is
        // what a caller pays to have `Segment::reading` filled on every row.
        say("");
        say("structured reading enabled vs disabled (candidates off in both):");
        {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
            cfg.includeCandidates = false;
            cfg.includeReading = true;
            auto r = bench("reading=true", iters, [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
        }
        {
            cpp_pinyin::v2::EngineConfig cfg;
            cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
            cfg.includeCandidates = false;
            cfg.includeReading = false;
            auto r = bench("reading=false", iters, [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
        }
    }

    static void benchCantonese(cpp_pinyin::v2::Engine &can) {
        printHeader("Cantonese (Jyutping) Throughput");

        const std::string input = u8"人有悲欢离合月有阴晴圆缺此事古难全但愿人长久千里共婵娟";
        const size_t charCount = utf8CharCount(input);

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::CantoneseStyle::TONE3;

        auto r1 = bench("Jyutping TONE3", 10000, [&]() { volatile auto res = can.query(input, cfg).value(); });
        r1.throughput = (static_cast<double>(charCount) / r1.median_us) * 1e6;
        r1.throughput_unit = "chars/s";
        record(r1);

        cfg.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;
        auto r2 = bench("Jyutping NORMAL", 10000, [&]() { volatile auto res = can.query(input, cfg).value(); });
        r2.throughput = (static_cast<double>(charCount) / r2.median_us) * 1e6;
        r2.throughput_unit = "chars/s";
        record(r2);
    }

    static void benchReadings(cpp_pinyin::v2::Engine &man) {
        printHeader("readings");

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;

        {
            // 0.1-0.2 us per call: the counts here are large because this is the only way a case this
            // short can be measured at all (see benchThroughput).
            auto r = bench("Polyphonic char", 200000, [&]() { volatile auto res = man.readings(u8"行", cfg); });
            record(r);
        }
        {
            auto r = bench("Non-polyphonic char", 200000, [&]() { volatile auto res = man.readings(u8"天", cfg); });
            record(r);
        }
    }

    // Two API paths the sections above never touch, paid for only when a caller opts in: the callback form of
    // query() (no Result vector materialised for the caller) and an installed disambiguation callback (every
    // polyphonic character pays the conversion + candidate list handed to it). The no-op callback returns
    // nothing, so the ENGINE'S RESULT IS UNCHANGED -- what is measured is the hook's overhead, and the case is
    // named so nobody reads it as "disambiguation costs X". One config (TONE3, candidates off) on purpose,
    // with the plain query timed alongside so the overhead is read off the table directly; the former
    // third case timed the "streaming" overload, deleted in D33 (it computed the whole result before calling
    // back once per syllable and silently skipped `Dialect::postProcess` -- a name that promised more).
    static void benchCallbackPaths(cpp_pinyin::v2::Engine &man) {
        printHeader("Callback paths (27 chars, TONE3, candidates off)");

        const std::string input = u8"人有悲欢离合月有阴晴圆缺此事古难全但愿人长久千里共婵娟";
        const size_t iters = 10000;

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
        cfg.includeCandidates = false;

        {
            auto r = bench("plain", iters, [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
        }
        {
            size_t calls = 0;
            man.setDisambiguationCallback(
                [&calls](std::string_view, size_t, std::string_view, const std::vector<std::string> &) -> std::string
                {
                    calls++;
                    return {};  // no opinion -> the engine falls through to its own resolution
                });
            auto r = bench("disambiguation hook (no-op)", iters,
                           [&]() { volatile auto res = man.query(input, cfg).value(); });
            record(r);
            man.setDisambiguationCallback({});
            say("disambiguation calls: " + std::to_string(calls) + " (one per polyphonic character per query)");
        }
    }

    // Index over the cases whose cost is our code's, and over their MIN rather than their median.
    // Both choices are measurements, not taste: including the dictionary-loading cases or using the
    // median made the index swing 25-28% between runs of the same binary (the medians of sub-microsecond
    // cases are quantised by the clock, and one tick of movement is a 100% change in log terms), while
    // the min over non-noisy cases moves 0.54% across eight identical runs. The tables still show the
    // median because that is what the docs quote; the index is a different question ("did anything get
    // slower at all?") and gets the estimator that can answer it.
    static double indexGeomean() {
        double sum_log = 0;
        size_t n = 0;
        for (const auto &r : g_results) {
            if (r.min_us > 0 && !r.noisy) {
                sum_log += std::log(r.min_us);
                n++;
            }
        }
        return n ? std::exp(sum_log / static_cast<double>(n)) : 0.0;
    }

    static size_t noisyCaseCount() {
        size_t n = 0;
        for (const auto &r : g_results)
            if (r.noisy)
                n++;
        return n;
    }

    static void printSummary() {
        auto sorted = g_results;
        std::sort(sorted.begin(), sorted.end(),
                  [](const BenchResult &a, const BenchResult &b) { return a.median_us < b.median_us; });

        // --brief exists for logs and for CTest: one line per case, no box, no separators. The marker
        // line prints either way, so a brief run is still machine-checkable.
        if (g_brief) {
            for (const auto &r : sorted)
                std::cout << r.name << " " << fmtDuration(r.median_us) << std::endl;
            return;
        }

        printHeader("SUMMARY");
        say("Performance (sorted by latency)");
        printSep('-');
        std::cout << "  " << std::left << std::setw(36) << "Benchmark" << std::right << std::setw(12) << "Median"
                  << std::setw(12) << "p90" << std::setw(12) << "Min" << std::setw(18) << "Throughput" << std::endl;
        printSep('-');

        for (const auto &r : sorted) {
            std::cout << "  " << std::left << std::setw(36) << r.name << std::right << std::setw(12)
                      << fmtDuration(r.median_us) << std::setw(12) << fmtDuration(r.p90_us) << std::setw(12)
                      << fmtDuration(r.min_us);
            if (r.throughput > 0)
                std::cout << std::setw(18) << fmtThroughput(r.throughput, r.throughput_unit);
            std::cout << std::endl;
        }
    }

    // One greppable line per run: the repo's gates each print a machine-checkable summary, and a
    // benchmark that only prints a table cannot be checked by anything but a human.
    static void printMarker() {
        std::cout << std::endl
                  << "BENCH cases=" << g_results.size() << " noisy=" << noisyCaseCount() << " repeats=" << g_repeats
                  << " index_geomean_min_us=" << fmtJson(indexGeomean()) << std::endl;
    }

    static std::string timestampNow() {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }

    static void writeJson(const std::string &path) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "ERROR: cannot write " << path << std::endl;
            return;
        }
        out << "{\n \"tool\": \"cpp-pinyin2 Benchmark\",\n \"schema\": 1,\n \"timestamp\": \"" << timestampNow()
            << "\",\n \"repeats\": " << g_repeats << ",\n \"cases\": [\n";
        for (size_t i = 0; i < g_results.size(); i++) {
            const auto &r = g_results[i];
            out << "  {\"name\": \"" << jsonEscape(r.name) << "\", \"iterations\": " << r.iterations
                << ", \"repeats\": " << r.repeats << ", \"median_us\": " << fmtJson(r.median_us)
                << ", \"p90_us\": " << fmtJson(r.p90_us) << ", \"min_us\": " << fmtJson(r.min_us)
                << ", \"max_us\": " << fmtJson(r.max_us) << ", \"mean_us\": " << fmtJson(r.mean_us)
                << ", \"stddev_us\": " << fmtJson(r.stddev_us)
                << ", \"repeat_spread_pct\": " << fmtJson(r.repeat_spread_pct)
                << ", \"noisy\": " << (r.noisy ? "true" : "false") << ", \"throughput\": " << fmtJson(r.throughput)
                << ", \"throughput_unit\": \"" << jsonEscape(r.throughput_unit) << "\"}";
            out << (i + 1 < g_results.size() ? ",\n" : "\n");
        }
        out << " ],\n \"index\": {\"cases\": " << g_results.size() << ", \"noisy_cases\": " << noisyCaseCount()
            << ", \"definition\": \"geomean of per-case min_us over the non-noisy cases\""
            << ", \"index_geomean_min_us_excl_noisy\": " << fmtJson(indexGeomean()) << "}\n}\n";
        std::cout << "wrote " << path << std::endl;
    }

    // Reader for the file writeJson() just described -- not a general JSON parser, and deliberately so:
    // the writer emits one case object per line, and a line scanner cannot disagree with a schema it
    // does not have.
    struct BaselineCase {
        double median_us = 0;
        double min_us = 0;
        bool noisy = false;
    };

    static bool readJsonNumber(const std::string &line, const char *key, double &out) {
        const std::string needle = std::string("\"") + key + "\":";
        const size_t k = line.find(needle);
        if (k == std::string::npos)
            return false;
        const size_t num_begin = line.find_first_of("-0123456789", k + needle.size());
        if (num_begin == std::string::npos)
            return false;
        out = std::strtod(line.c_str() + num_begin, nullptr);
        return true;
    }

    static std::map<std::string, BaselineCase> readBaseline(const std::string &path, size_t &repeats_out) {
        std::map<std::string, BaselineCase> out;
        repeats_out = 0;
        std::ifstream in(path);
        if (!in) {
            std::cerr << "ERROR: cannot read " << path << std::endl;
            return out;
        }
        std::string line;
        while (std::getline(in, line)) {
            const size_t nk = line.find("\"name\": \"");
            if (nk == std::string::npos) {
                // Top-level "repeats" of the file itself (case lines always carry "name" too).
                double file_repeats = 0;
                if (readJsonNumber(line, "repeats", file_repeats) && file_repeats > 0)
                    repeats_out = static_cast<size_t>(file_repeats + 0.5);
                continue;
            }
            const size_t name_begin = nk + 9;
            const size_t name_end = line.find('"', name_begin);
            if (name_end == std::string::npos)
                continue;
            BaselineCase c;
            readJsonNumber(line, "median_us", c.median_us);
            readJsonNumber(line, "min_us", c.min_us);
            c.noisy = line.find("\"noisy\": true") != std::string::npos;
            if (c.min_us > 0)
                out[line.substr(name_begin, name_end - name_begin)] = c;
        }
        return out;
    }

    // Noise band, measured on this machine 2026-09-29: eight identical runs of this binary, comparing
    // each case's min, gave a worst spread of 4.55% (the Mixed case), second 3.57%, third 3.51%, median
    // 0.00%. The band is therefore 6% -- anything tighter would report the noisiest case as a
    // regression about as often as a real one. It is a property of the INSTRUMENT, not of the code under
    // test, which is why the comparison prints the band it applied next to each verdict.
    static constexpr double kNoiseBandRatio = 1.06;

    // The wide band for the cases the report marks noisy (dictionary loading), measured the same day:
    // five runs spread 11.0-11.1%, three runs 10.5-19.0%, and BACKLOG.md section 1.1 records max/min =
    // 1.39 across five samples inside ONE run. 20% covers the tail; the LoadCheck gate next door uses a
    // 20x margin for the same reason.
    static constexpr double kNoisyBandRatio = 1.20;

    // The comparison is on MIN, not on the median the tables show. Two reasons, both measured: the
    // project's own A/B methodology is min-of-N (PERFORMANCE.md §4.18 "9 轮 x 4 passes min"), and the
    // median of a sub-microsecond case is quantised by the clock -- a 1.0 vs 2.0 us pair made a first
    // version of this table report a 2.0x "regression" on `readings` that no code change could explain.
    // Min over thousands of samples is the stable estimator for exactly those cases; the tables keep
    // showing the median because that is what the docs already quote.
    static void printComparison(const std::string &path) {
        size_t baseline_repeats = 0;
        const auto baseline = readBaseline(path, baseline_repeats);
        if (baseline.empty()) {
            std::cout << std::endl << "BENCH_COMPARE error=1 reason=empty-or-unreadable-baseline" << std::endl;
            return;
        }

        printHeader("COMPARISON vs " + path);
        if (!g_brief) {
            std::cout << "  " << std::left << std::setw(36) << "Benchmark" << std::right << std::setw(12) << "baseline"
                      << std::setw(12) << "current" << std::setw(10) << "ratio"
                      << "   (min_us)" << std::endl;
            printSep('-');
        }

        size_t better = 0, worse = 0, tied = 0, missing = 0, extra = 0;
        double sum_log_ratio = 0;
        size_t compared = 0;
        double worst_ratio = 0;
        std::string worst_name;

        for (const auto &r : g_results) {
            const auto it = baseline.find(r.name);
            if (it == baseline.end() || it->second.min_us <= 0) {
                extra++;
                continue;
            }
            const double ratio = r.min_us / it->second.min_us;
            // A case the baseline itself marked noisy gets the wide band: judging dictionary loading
            // with a +/-2% rule would flag most runs as a regression (measured swing ~12%).
            const double band = it->second.noisy ? kNoisyBandRatio : kNoiseBandRatio;
            compared++;
            sum_log_ratio += std::log(ratio);
            if (ratio > band) {
                worse++;
                if (ratio > worst_ratio) {
                    worst_ratio = ratio;
                    worst_name = r.name;
                }
            } else if (ratio < 1.0 / band) {
                better++;
            } else {
                tied++;
            }
            if (g_brief)
                continue;
            std::cout << "  " << std::left << std::setw(36) << r.name << std::right << std::setw(12)
                      << fmtDuration(it->second.min_us) << std::setw(12) << fmtDuration(r.min_us) << std::setw(9)
                      << std::fixed << std::setprecision(3) << ratio << "x"
                      << ((ratio > band)             ? "  worse"
                              : (ratio < 1.0 / band) ? "  better"
                                                     : "")
                      << (it->second.noisy ? "  (noisy case, +/-15% band)" : "") << std::endl;
        }
        for (const auto &kv : baseline) {
            bool present = false;
            for (const auto &r : g_results)
                if (r.name == kv.first) {
                    present = true;
                    break;
                }
            if (!present)
                missing++;
        }

        const double geomean_ratio = compared ? std::exp(sum_log_ratio / static_cast<double>(compared)) : 0.0;
        const bool repeats_match = (baseline_repeats == 0 || baseline_repeats == g_repeats);
        const long band_pct = std::lround((kNoiseBandRatio - 1.0) * 100.0);
        const long noisy_band_pct = std::lround((kNoisyBandRatio - 1.0) * 100.0);
        if (!g_brief) {
            say("");
            say("better / tied / worse = " + std::to_string(better) + " / " + std::to_string(tied) + " / " +
                std::to_string(worse) + "   (tied = inside +/-" + std::to_string(band_pct) +
                "%, the measured noise band of this instrument; dictionary loading uses +/-" +
                std::to_string(noisy_band_pct) + "%)");
            if (missing || extra)
                say("cases only in baseline = " + std::to_string(missing) +
                    ", only in this run = " + std::to_string(extra));
            if (!repeats_match)
                say("WARNING: baseline was measured with --repeat " + std::to_string(baseline_repeats) +
                    ", this run with --repeat " + std::to_string(g_repeats) +
                    " -- best-of-N lowers the number, so the ratios carry that difference");
        }
        std::cout << "BENCH_COMPARE estimator=min_us cases=" << compared << " better=" << better << " tied=" << tied
                  << " worse=" << worse << " missing=" << missing << " extra=" << extra << " band_pct=" << band_pct
                  << " noisy_band_pct=" << noisy_band_pct << " repeats_match=" << (repeats_match ? 1 : 0)
                  << " geomean_ratio=" << std::fixed << std::setprecision(4) << geomean_ratio;
        if (!worst_name.empty())
            std::cout << " worst_ratio=" << std::fixed << std::setprecision(4) << worst_ratio << " worst_case=\""
                      << worst_name << "\"";
        std::cout << std::endl;
    }

    static void usage(const char *argv0) {
        std::cout << "usage: " << argv0 << " [--verbose|-v] [--brief] [--repeat N]\n"
                  << "                        [--json report.json] [--compare baseline.json]\n"
                  << "  --repeat N   measure every case N times and report the best run (min-of-N)\n"
                  << "  --json PATH  write the machine-readable report\n"
                  << "  --compare    diff this run against an earlier --json report\n"
                  << "  --brief      print only the summary and the marker lines\n";
    }

}  // anonymous namespace

int main(int argc, char *argv[]) {
    std::string json_path;
    std::string compare_path;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            g_verbose = true;
        } else if (arg == "--brief") {
            g_brief = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--json" && i + 1 < argc) {
            json_path = argv[++i];
        } else if (arg == "--compare" && i + 1 < argc) {
            compare_path = argv[++i];
        } else if (arg == "--repeat" && i + 1 < argc) {
            const long n = std::strtol(argv[++i], nullptr, 10);
            g_repeats = (n > 0) ? static_cast<size_t>(n) : 1;
        } else {
            std::cerr << "unknown argument: " << arg << std::endl;
            usage(argv[0]);
            return 2;
        }
    }

    const auto dictPath = std::filesystem::current_path() / "dict";

    cpp_pinyin::v2::Engine man = cpp_test::mandarin();
    cpp_pinyin::v2::Engine can = cpp_test::cantonese();
    if (man.dialect().id.empty() || can.dialect().id.empty()) {
        std::cerr << "ERROR: Dict init failed. Check path: " << dictPath << std::endl;
        return 1;
    }

    benchDictLoading();
    benchThroughput(man);
    benchToneStyles(man);
    benchCantonese(can);
    benchReadings(man);
    benchCallbackPaths(man);
    printSummary();
    printMarker();
    if (!json_path.empty())
        writeJson(json_path);
    if (!compare_path.empty())
        printComparison(compare_path);

    std::cout << std::endl << "Done." << std::endl;
    return 0;
}
