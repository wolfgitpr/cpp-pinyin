// Repo-committed strong-equivalence gate (docs/REQUIREMENTS.md §2.2, protocol P1).
//
// Why this file exists: earlier byte-equivalence evidence came from a tool that lived only in a gitignored
// scratch directory, so a later change could not re-verify itself and nobody else could reproduce the gate
// It is that tool, in the repo, with its own SHA-256 so one command is the whole gate.
//
// What it does: query all four datasets for includeCandidates in {false,true}, stream every syllable of every
// query to a canonical byte stream and hash it on the way. Two builds with the same digest are byte-equivalent
// on this input set -- the strongest statement available about "the change did not alter any conversion".
//
// The byte format is frozen and must not change casually: the expected digest was produced by exactly this
// layout. The live value is CPP_PINYIN2_FROZEN_DUMP_SHA256 in tools/CMakeLists.txt
// (re-recorded 2026-10-01: 5,384,712 entries / 115,666,595 bytes / 820c880f..., after res/dict changed in
// e03a772; the older 115,712,228 B / B871E3B7... digest stays reproducible from that revision's inputs).
//
// Usage:
//   dump_equivalence <dictRoot> <testDataDir> [--out FILE] [--expect SHA256] [--quiet]
//   dump_equivalence --self-test
//
// Exit codes: 0 = ok/expected digest matched, 1 = digest mismatch, 2 = usage/IO error,
//             3 = a dataset is missing or empty, 4 = engine not ready.

#include <algorithm>
#include <cctype>
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct OpenedDialect {
        std::optional<cpp_pinyin::v2::Engine> engine;
        std::string error;
        [[nodiscard]] bool ready() const { return engine.has_value(); }
    };

    // The facades this tool was written against are gone. The registry + Engine::open + Outcome unwrap
    // lives here, in the tool, on purpose: tools/ must not reach into the tests scaffolding. The error
    // string is carried out so the "engine not ready: ..." report below still says what it said.
    OpenedDialect openDialect(const std::filesystem::path &dir, const char *id) {
        static const cpp_pinyin::v2::Registry registry;
        auto outcome = cpp_pinyin::v2::Engine::open(registry, dir, id);
        if (!outcome)
            return {std::nullopt, outcome.error().detail};
        return {std::move(outcome.value()), {}};
    }


    // ---------------------------------------------------------------- SHA-256 (FIPS 180-4)
    class Sha256 {
    public:
        void update(const char *data, size_t len) {
            for (size_t i = 0; i < len; ++i)
                byte(static_cast<uint8_t>(data[i]));
        }

        [[nodiscard]] std::string hex() const {
            Sha256 copy = *this;
            return copy.finishHex();
        }

    private:
        static constexpr uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        uint32_t m_h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        uint8_t m_buf[64] = {};
        size_t m_bufLen = 0;
        uint64_t m_total = 0;

        static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        void block(const uint8_t *p) {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) | (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                    (static_cast<uint32_t>(p[i * 4 + 2]) << 8) | static_cast<uint32_t>(p[i * 4 + 3]);
            for (int i = 16; i < 64; ++i) {
                const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
            uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
            for (int i = 0; i < 64; ++i) {
                const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                const uint32_t ch = (e & f) ^ (~e & g);
                const uint32_t t1 = h + s1 + ch + K[i] + w[i];
                const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t t2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            m_h[0] += a;
            m_h[1] += b;
            m_h[2] += c;
            m_h[3] += d;
            m_h[4] += e;
            m_h[5] += f;
            m_h[6] += g;
            m_h[7] += h;
        }

        void byte(uint8_t v) {
            m_buf[m_bufLen++] = v;
            m_total++;
            if (m_bufLen == 64) {
                block(m_buf);
                m_bufLen = 0;
            }
        }

        [[nodiscard]] std::string finishHex() {
            const uint64_t bits = m_total * 8;
            byte(0x80);
            while (m_bufLen != 56)
                byte(0x00);
            for (int i = 7; i >= 0; --i)
                byte(static_cast<uint8_t>(bits >> (i * 8)));
            std::ostringstream oss;
            for (uint32_t v : m_h)
                oss << std::hex << std::setw(8) << std::setfill('0') << v;
            return oss.str();
        }
    };

    // ---------------------------------------------------------------- dataset handling
    std::vector<std::string> readLines(const std::filesystem::path &path) {
        std::vector<std::string> lines;
        std::ifstream file(path);
        if (!file.is_open())
            return lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::vector<std::string> sentencesOf(const std::vector<std::string> &lines) {
        std::vector<std::string> out;
        for (const auto &line : lines) {
            auto sep = line.find(u8"▁");
            if (sep != std::string::npos) {
                out.push_back(line.substr(0, sep));
                continue;
            }
            sep = line.find('|');
            if (sep == std::string::npos)
                continue;
            out.push_back(line.substr(0, sep));
        }
        return out;
    }

    struct Sink {
        std::ostream *out;
        Sha256 sha;
        uint64_t fnv = 1469598103934665603ull;  // FNV-1a 64 offset basis
        uint64_t bytes = 0;
        uint64_t entries = 0;
        uint64_t lines = 0;

        void put(char c) {
            if (out)
                out->put(c);
            sha.update(&c, 1);
            fnv ^= static_cast<unsigned char>(c);
            fnv *= 1099511628211ull;
            bytes++;
        }
        void put(const std::string &s) {
            if (!s.empty()) {
                if (out)
                    out->write(s.data(), static_cast<std::streamsize>(s.size()));
                sha.update(s.data(), s.size());
                for (char c : s) {
                    fnv ^= static_cast<unsigned char>(c);
                    fnv *= 1099511628211ull;
                }
                bytes += s.size();
            }
        }
        void line() {
            put('\n');
            lines++;
        }
    };

    void dumpEngine(Sink &sink, const std::string &tag, bool candidates, const std::vector<std::string> &sentences,
                    const cpp_pinyin::v2::Engine *man, const cpp_pinyin::v2::Engine *can,
                    cpp_pinyin::v2::EngineConfig mcfg, cpp_pinyin::v2::EngineConfig ccfg) {
        if (man)
            mcfg.includeCandidates = candidates;
        if (can)
            ccfg.includeCandidates = candidates;
        // The structured reading is asked for on BOTH sides (S8), for two reasons:
        //
        //   1. The switch is the only thing that decides whether S7's code runs at all, so a run with
        //      it OFF would execute none of it -- and then "the dump still matches" would be a
        //      statement about the code that no longer exists. With it ON, every one of the 5,384,712
        //      rows goes through `attachWordReading` / the phrase attach.
        //   2. The four columns this tool writes (`text`, `rendered`, `failure_kind`, `candidates`)
        //      must be identical whichever way the switch is set -- the structure is data, not a
        //      rendering input. Running the whole corpus on the ON side turns that from a claim into a
        //      measurement: the digest must still equal CPP_PINYIN2_FROZEN_DUMP_SHA256, whose value
        //      and whose location in tools/CMakeLists.txt are untouched by this change.
        mcfg.includeReading = true;
        ccfg.includeReading = true;
        for (const auto &sent : sentences) {
            const auto res = (man ? man->query(sent, mcfg) : can->query(sent, ccfg)).value();
            for (const auto &syl : res) {
                sink.put(tag);
                sink.put('\t');
                sink.put(candidates ? "1" : "0");
                sink.put('\t');
                // S7 renamed the row's fields; the BYTES are the same three columns as before, and
                // that is the point of the mapping. `text` is the old `character`, `rendered` is
                // the old `reading` (the one rendered string the row has), and `failure_kind` is
                // the old `failed` flag -- `Code::Ok` meaning "this row has a reading", which is
                // exactly the old `failed == false`. The candidate column is untouched: it is
                // still read from `Segment::candidates`.
                sink.put(syl.text);
                sink.put('\t');
                sink.put(syl.rendered);
                sink.put('\t');
                sink.put(syl.failure_kind == cpp_pinyin::v2::Code::Ok ? "0" : "1");
                sink.put('\t');
                for (size_t i = 0; i < syl.candidates.size(); ++i) {
                    if (i)
                        sink.put(',');
                    sink.put(syl.candidates[i]);
                }
                sink.line();
                sink.entries++;
            }
        }
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    int selfTest() {
        struct Case {
            const char *input;
            const char *expect;
        };
        const Case cases[] = {
            {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        };
        bool ok = true;
        for (const auto &c : cases) {
            Sha256 sha;
            sha.update(c.input, std::strlen(c.input));
            const auto got = sha.hex();
            const bool pass = got == c.expect;
            ok = ok && pass;
            std::cout << (pass ? "OK   " : "FAIL ") << "sha256(\"" << c.input << "\") = " << got << "\n";
        }
        // Streaming split across update() calls must equal a single-shot update.
        Sha256 oneShot;
        oneShot.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
        Sha256 chunked;
        for (int i = 0; i < 56; ++i) {
            const char c = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"[i];
            chunked.update(&c, 1);
        }
        const bool splitOk = oneShot.hex() == chunked.hex();
        ok = ok && splitOk;
        std::cout << (splitOk ? "OK   " : "FAIL ") << "byte-at-a-time == single-shot\n";
        std::cout << (ok ? "SELF_TEST PASS\n" : "SELF_TEST FAIL\n");
        return ok ? 0 : 1;
    }

}  // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == "--self-test")
        return selfTest();

    std::string outPath, expect;
    bool quiet = false;
    std::vector<std::string> positional;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size())
            outPath = args[++i];
        else if (args[i] == "--expect" && i + 1 < args.size())
            expect = lower(args[++i]);
        else if (args[i] == "--quiet")
            quiet = true;
        else
            positional.push_back(args[i]);
    }
    if (positional.size() != 2) {
        std::cerr << "usage: dump_equivalence <dictRoot> <testDataDir> [--out FILE] [--expect SHA256] [--quiet]\n"
                     "       dump_equivalence --self-test\n";
        return 2;
    }
    const std::filesystem::path dictRoot = positional[0];
    const std::filesystem::path dataDir = positional[1];

    std::ofstream file;
    std::ostream *out = nullptr;
    if (!outPath.empty()) {
        file.open(outPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "cannot open " << outPath << "\n";
            return 2;
        }
        out = &file;
    }
    Sink sink{out, {}};

    auto manOpen = openDialect(dictRoot, "mandarin");
    auto canOpen = openDialect(dictRoot, "cantonese");
    if (!manOpen.ready() || !canOpen.ready()) {
        std::cerr << "engine not ready: " << manOpen.error << " | " << canOpen.error << "\n";
        return 4;
    }
    cpp_pinyin::v2::Engine *man = &*manOpen.engine;
    cpp_pinyin::v2::Engine *can = &*canOpen.engine;

    cpp_pinyin::v2::EngineConfig mTone;
    mTone.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    mTone.markNeutralAs5 = true;
    cpp_pinyin::v2::EngineConfig mNormal;
    mNormal.style = cpp_pinyin::v2::MandarinStyle::NORMAL;
    cpp_pinyin::v2::EngineConfig cTone;
    // The sentinel, NOT the number: this is what makes the frozen digest cover the default-style path.
    // `resolveStyle` turns -1 into the dialect's own default (the `styles[].isDefault` entry: src/Cantonese.cpp marks
    // "tone3") so the Cantonese columns are byte-for-byte what the explicit TONE3 produced -- the point is not to
    // change the digest but to make it depend on `resolveStyle` at all. If that resolution broke, the
    // Cantonese columns would flip to NORMAL (alias 0) and the digest would go red.
    cTone.style = -1;
    cpp_pinyin::v2::EngineConfig cNormal;
    cNormal.style = cpp_pinyin::v2::CantoneseStyle::NORMAL;

    // THE COVERAGE BOUNDARY OF THIS TOOL, written down because the frozen dump's authority comes from
    // knowing exactly what it pins. Input: the four corpora listed below -- that is the whole input
    // surface, no corpus is discovered at run time. Output: the four columns the Sink writes (`text`,
    // `rendered`, `failure_kind`, `candidates`), for both dialects and for includeCandidates in {false,
    // true}. NOT covered: `Engine::readings()` and `to_simplified()` are never called here (0 occurrences
    // in this file), and the gold set under tests/data/verified/ is not an input either. So a byte-identical
    // dump pins those four columns over those four corpora, and nothing more.
    const char *names[] = {"cpp_dataset.txt", "op_lab.txt", "jyutping_test.txt", "jyutping_untone_test.txt"};
    const char *tags[] = {"cpp", "oplab", "jyut", "jyut5"};
    std::vector<std::vector<std::string>> sets;
    for (const char *name : names) {
        auto lines = readLines(dataDir / name);
        if (lines.empty()) {
            // Not a warning: a gate that silently measures nothing is worse than no gate (T-01).
            std::cerr << "dataset missing or empty: " << (dataDir / name) << "\n";
            return 3;
        }
        sets.push_back(sentencesOf(lines));
        if (sets.back().empty()) {
            std::cerr << "dataset parsed to zero sentences: " << (dataDir / name) << "\n";
            return 3;
        }
    }

    for (size_t i = 0; i < 4; ++i) {
        for (int cand = 0; cand <= 1; ++cand) {
            switch (i) {
            case 0:
                dumpEngine(sink, tags[i], cand != 0, sets[i], man, nullptr, mTone, cTone);
                break;
            case 1:
                dumpEngine(sink, tags[i], cand != 0, sets[i], man, nullptr, mNormal, cTone);
                break;
            case 2:
                dumpEngine(sink, tags[i], cand != 0, sets[i], nullptr, can, mNormal, cTone);
                break;
            default:
                dumpEngine(sink, tags[i], cand != 0, sets[i], nullptr, can, mNormal, cNormal);
                break;
            }
        }
    }
    if (file.is_open())
        file.flush();

    const std::string digest = sink.sha.hex();
    if (!quiet) {
        std::cout << "ENTRIES " << sink.entries << "\n";
        std::cout << "LINES " << sink.lines << "\n";
        std::cout << "BYTES " << sink.bytes << "\n";
        std::cout << "FNV64 " << sink.fnv << "\n";
        std::cout << "SHA256 " << digest << "\n";
    }
    if (!expect.empty()) {
        if (digest == expect) {
            std::cout << "EQUIVALENT\n";
            return 0;
        }
        std::cout << "MISMATCH expected " << expect << " got " << digest << "\n";
        return 1;
    }
    return 0;
}
