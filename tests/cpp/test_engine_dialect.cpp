// Tests for the dialect plugin path: `Dialect` + caller-owned `Registry` + `Engine::open`. Three pins:
// (1) the built-in facades did not move -- `Mandarin`/`Cantonese` now forward to `Engine`, so these cases
//     compare the two doors byte for byte on a small sample (candidates and the failed flag included); the
//     frozen corpus numbers would show such drift only after a full run, and only in aggregate.
// (2) an out-of-repo dialect works: the probe dictionary is written into the temp directory (nothing
//     third-language enters `res/dict`), and its converter WRAPS every reading -- "output == input" cannot
//     distinguish "the slot was read" from "the slot was empty and the reading passed through".
// (3) nothing is process-global: every case builds its own engines from its own roots and compares against
//     a single-threaded baseline; the concurrency case constructs engines (which resolves an id through the
//     registry) from four threads at once.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>
#include "dialect_fixtures.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    /// Wraps every reading in angle brackets. Unmistakable on purpose: if the engine consulted
    /// some other converter (or none), the reading comes back unwrapped and the check fails.
    class WrapTone final : public cpp_pinyin::v2::ToneConverter {
    public:
        enum { NORMAL = 0 };

        WrapTone() { m_converts[NORMAL] = wrap; }

        static std::u16string wrap(const std::u16string &pinyin, bool, bool) {
            std::u16string out;
            out.reserve(pinyin.size() + 2);
            out += u'<';
            out += pinyin;
            out += u'>';
            return out;
        }
    };

    /// Echoes back which switches reached the converter, so a capability flag can be tested by
    /// observing whether the reading changed -- not by reading the flag back.
    class FlagTone final : public cpp_pinyin::v2::ToneConverter {
    public:
        enum { NORMAL = 0 };

        FlagTone() { m_converts[NORMAL] = echo; }

        static std::u16string echo(const std::u16string &pinyin, bool v_to_u, bool neutral_tone5) {
            std::u16string out = v_to_u ? u"V:" : u"v:";
            out += pinyin;
            out += neutral_tone5 ? u":n5" : u":plain";
            return out;
        }
    };

    /// A minimal but *legal* dictionary: four required files, each with at least one record that
    /// parses (`EngineImpl::init` treats an empty file as a load failure), the phrase key inside
    /// the image of `trans_word.txt`, and the phrase starting character present in
    /// `phrases_map.txt`, which is the gate that opens the sliding window at all.
    fs::path writeProbeDict() {
        const fs::path root = fs::temp_directory_path() / "cpp_pinyin_plugin_tests";
        const fs::path dir = root / "probe";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(dir);

        const auto put = [&dir](const char *name, const char *body)
        {
            std::ofstream out(dir / name, std::ios::binary);
            REQUIRE(out.good());
            out << body;
        };
        put("word.txt", u8"你:hau1,hau9\n好:hau2\n");
        put("phrases_dict.txt", u8"你好:hau3,hau4\n");
        put("phrases_map.txt", u8"你:2\n");
        put("trans_word.txt", u8"妳:你\n");
        return root;
    }

    cpp_pinyin::v2::Dialect probeDialect(bool supports_v_to_u) {
        cpp_pinyin::v2::Dialect dialect;
        dialect.id = "probe";
        dialect.dictSubDir = "probe";
        dialect.tone = WrapTone{};
        dialect.supportsVToU = supports_v_to_u;
        dialect.supportsNeutralTone5 = supports_v_to_u;
        return dialect;
    }

    /// Everything a caller can observe about a result, in one comparable string.
    std::string digest(const cpp_pinyin::v2::Result &result) {
        std::string out;
        for (const auto &syllable : result) {
            out += syllable.text;
            out += '\x1f';
            out += syllable.rendered;
            out += '\x1f';
            for (const auto &candidate : syllable.candidates) {
                out += candidate;
                out += '\x1e';
            }
            out += '\x1d';
            out += (syllable.failure_kind != cpp_pinyin::v2::Code::Ok) ? 'F' : 'O';
            out += '\n';
        }
        return out;
    }

    std::vector<std::string> sampleTexts() {
        return {u8"你好世界", u8"银行行长", u8"中国人民", u8"著", u8"行", u8"长", u8"hello 你好", u8"。【】", u8"𠀋"};
    }
}  // namespace

TEST_CASE("Engine by id reproduces the built-in facades", "[engine][builtin]") {
    const fs::path root = fs::current_path() / "dict";

    const cpp_pinyin::v2::Registry registry;
    cpp_pinyin::v2::Engine mandarin = cpp_pinyin::v2::Engine::open(registry, root, "mandarin").value();
    cpp_pinyin::v2::Engine cantonese = cpp_pinyin::v2::Engine::open(registry, root, "cantonese").value();

    // The descriptions the built-ins register must be the ones the facades use.
    CHECK(mandarin.dialect().id == "mandarin");
    CHECK(mandarin.dialect().dictSubDir == "mandarin");
    CHECK(cantonese.dialect().id == "cantonese");
    CHECK(cantonese.dialect().dictSubDir == "cantonese");

    SECTION("mandarin: every style, every sample, byte for byte") {
        cpp_pinyin::v2::Engine facade = cpp_test::openDialect("mandarin", root);

        for (int style = 0; style < 9; ++style) {
            cpp_pinyin::v2::EngineConfig engine_cfg;
            engine_cfg.style = style;
            cpp_pinyin::v2::EngineConfig facade_cfg;
            facade_cfg.style = static_cast<cpp_pinyin::v2::MandarinStyle>(style);

            for (const auto &text : sampleTexts()) {
                INFO("style=" << style << " text=" << text);
                CHECK(digest(mandarin.query(text, engine_cfg).value()) ==
                      digest(facade.query(text, facade_cfg).value()));
                CHECK(mandarin.readings(text, engine_cfg).value() == facade.readings(text, facade_cfg).value());
                CHECK(mandarin.toSimplified(text).value_or_throw() == facade.toSimplified(text).value_or_throw());
                CHECK(mandarin.isPolyphonic(text) == facade.isPolyphonic(text));
            }
        }
    }

    SECTION("cantonese: every style, every sample, byte for byte") {
        cpp_pinyin::v2::Engine facade = cpp_test::openDialect("cantonese", root);

        for (int style : {0, 3, 8}) {
            cpp_pinyin::v2::EngineConfig engine_cfg;
            engine_cfg.style = style;
            cpp_pinyin::v2::EngineConfig facade_cfg;
            facade_cfg.style = static_cast<cpp_pinyin::v2::CantoneseStyle>(style);

            for (const auto &text : sampleTexts()) {
                INFO("style=" << style << " text=" << text);
                CHECK(digest(cantonese.query(text, engine_cfg).value()) ==
                      digest(facade.query(text, facade_cfg).value()));
                CHECK(cantonese.readings(text, engine_cfg).value() == facade.readings(text, facade_cfg).value());
                CHECK(cantonese.toSimplified(text).value_or_throw() == facade.toSimplified(text).value_or_throw());
                CHECK(cantonese.isPolyphonic(text) == facade.isPolyphonic(text));
            }
        }
    }

    SECTION("vector and streaming overloads agree with the facades") {
        cpp_pinyin::v2::Engine facade = cpp_test::openDialect("mandarin", root);
        // Both styles set explicitly on purpose: the two config types have DIFFERENT defaults
        // (EngineConfig defaults to TONE, EngineConfig to 0), so comparing defaults would be
        // comparing NORMAL against TONE and would fail for the wrong reason.
        cpp_pinyin::v2::EngineConfig engine_cfg;
        engine_cfg.style = static_cast<int>(cpp_pinyin::v2::MandarinStyle::TONE);
        cpp_pinyin::v2::EngineConfig facade_cfg;
        facade_cfg.style = cpp_pinyin::v2::MandarinStyle::TONE;

        const std::vector<std::string> chars{u8"你", u8"好", u8"w", u8"o", u8"r", u8"l", u8"d"};
        CHECK(digest(mandarin.query(chars, engine_cfg).value()) == digest(facade.query(chars, facade_cfg).value()));
    }
}

TEST_CASE("Engine reports an unknown dialect instead of falling back", "[engine][error]") {
    const fs::path root = fs::current_path() / "dict";
    const cpp_pinyin::v2::Registry registry;

    CHECK(registry.resolve("no_such_dialect") == nullptr);
    CHECK(registry.resolve("mandarin") != nullptr);

    // `open` CLASSIFIES the failure instead of handing back a dead object (S11a): the outcome holds
    // no value, `error().code` says which kind of failure it is, and `error().detail` names the id.
    // This is the strengthening of what used to be "isReady() is false and lastError() names it" --
    // same facts, now carried by the one error channel instead of a second query back into the
    // registry (which is exactly the judgement that has moved into `open`).
    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
        cpp_pinyin::v2::Engine::open(registry, root, "no_such_dialect");
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == cpp_pinyin::v2::Code::NotReady);
    CHECK(opened.error().detail.find("no_such_dialect") != std::string::npos);

    // The remaining state with no engine underneath is a moved-from object, and every accessor has
    // to stay usable there too: no silent Mandarin fallback, no crash. `query` says WHY it cannot
    // answer, instead of returning an empty result a caller cannot tell from a successful query
    // that matched nothing (that is the second negative case, strengthened).
    cpp_pinyin::v2::Engine engine = cpp_pinyin::v2::Engine::open(registry, root, "mandarin").value();
    cpp_pinyin::v2::Engine owner(std::move(engine));
    const auto textRows = engine.query(u8"你好");
    CHECK_FALSE(textRows.has_value());
    CHECK(textRows.error().code == cpp_pinyin::v2::Code::NotReady);
    const auto charRows = engine.query(std::vector<std::string>{u8"你"});
    CHECK_FALSE(charRows.has_value());
    CHECK(charRows.error().code == cpp_pinyin::v2::Code::NotReady);
    const auto singleReadings = engine.readings(u8"你");
    // N17: readings() has an error channel now, so the moved-from case is a NAMED failure rather than
    // an empty result -- the same treatment query() gets two lines up (S11a).
    CHECK_FALSE(singleReadings.has_value());
    CHECK(singleReadings.error().code == cpp_pinyin::v2::Code::NotReady);
    const auto simplified = engine.toSimplified(u8"你");
    CHECK_FALSE(simplified.has_value());
    CHECK(simplified.error().code == cpp_pinyin::v2::Code::NotReady);
    CHECK_FALSE(engine.isPolyphonic(u8"你"));
    CHECK(engine.dialect().id.empty());
    const auto loaded = engine.loadUserDict(root / "mandarin" / "user_dict.txt");
    CHECK_FALSE(loaded.has_value());
    CHECK(loaded.error().code == cpp_pinyin::v2::Code::NotReady);
}

TEST_CASE("a dialect description drives the engine end to end", "[engine][plugin]") {
    const fs::path root = writeProbeDict();

    SECTION("the converter is consulted, not bypassed") {
        cpp_pinyin::v2::Engine engine(probeDialect(false), root);

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = WrapTone::NORMAL;
        cfg.includeCandidates = false;

        // Two characters, one phrase entry: proves the phrase layer ran, and the wrapping proves
        // the phrase's readings went through this dialect's converter.
        const cpp_pinyin::v2::Result result = engine.query(u8"你好", cfg).value();
        REQUIRE(result.size() == 2);
        CHECK(result[0].text == u8"你");
        CHECK(result[0].rendered == "<hau3>");
        CHECK(result[1].text == u8"好");
        CHECK(result[1].rendered == "<hau4>");

        CHECK(engine.readings(u8"你", cfg).value() == std::vector<std::string>{"<hau1>", "<hau9>"});
    }

    SECTION("empty slots pass readings through -- the negative control") {
        cpp_pinyin::v2::Dialect plain = probeDialect(false);
        plain.tone = cpp_pinyin::v2::ToneConverter{};  // every style slot empty
        cpp_pinyin::v2::Engine engine(plain, root);

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = WrapTone::NORMAL;
        cfg.includeCandidates = false;

        const cpp_pinyin::v2::Result result = engine.query(u8"你好", cfg).value();
        REQUIRE(result.size() == 2);
        CHECK(result[0].rendered == "hau3");
        CHECK(result[1].rendered == "hau4");
    }

    SECTION("candidates go through the converter as well") {
        cpp_pinyin::v2::Engine engine(probeDialect(false), root);
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = WrapTone::NORMAL;
        cfg.includeCandidates = true;

        const cpp_pinyin::v2::Result result = engine.query(u8"你好", cfg).value();
        REQUIRE(result.size() == 2);
        CHECK(result[0].candidates == std::vector<std::string>{"<hau1>", "<hau9>"});
        CHECK(result[1].candidates == std::vector<std::string>{"<hau2>"});
    }

    SECTION("postProcess rewrites the finished result") {
        cpp_pinyin::v2::Dialect dialect = probeDialect(false);
        dialect.postProcess = [](cpp_pinyin::v2::Result &result)
        {
            for (size_t i = 0; i < result.size(); ++i)
                result[i].rendered += "!";
        };
        cpp_pinyin::v2::Engine engine(dialect, root);

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = WrapTone::NORMAL;
        const cpp_pinyin::v2::Result result = engine.query(u8"你好", cfg).value();
        REQUIRE(result.size() == 2);
        CHECK(result[0].rendered == "<hau3>!");
        CHECK(result[1].rendered == "<hau4>!");
    }

    SECTION("the same dialect reads two different roots at once") {
        cpp_pinyin::v2::Engine a(probeDialect(false), root);
        cpp_pinyin::v2::Engine b(probeDialect(false), root, "probe");
        CHECK(digest(a.query(u8"你好").value()) == digest(b.query(u8"你好").value()));

        cpp_pinyin::v2::Engine missing(probeDialect(false), root, "not_probe");
        // After a description-construction failure the only usable probe is the query itself: the error
        // channel gives NotReady (its wording stays pinned to the `open` channel).
        CHECK_FALSE(missing.query(u8"你好").has_value());
        CHECK(missing.query(u8"你好").error().code == cpp_pinyin::v2::Code::NotReady);
    }

    SECTION("a registered dialect is reachable by id") {
        cpp_pinyin::v2::Registry registry;
        REQUIRE(registry.add(
            "probe_registered", [] { return std::make_shared<const cpp_pinyin::v2::Dialect>(probeDialect(false)); },
            true));

        cpp_pinyin::v2::Engine engine = cpp_pinyin::v2::Engine::open(registry, root, "probe_registered").value();
        REQUIRE(engine.dialect().id == "probe");

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = WrapTone::NORMAL;
        cfg.includeCandidates = false;
        const cpp_pinyin::v2::Result result = engine.query(u8"你好", cfg).value();
        REQUIRE(result.size() == 2);
        CHECK(result[0].rendered == "<hau3>");

        // An id this registry does not know is an error that NAMES the id -- never a fallback to a
        // built-in dialect or to the last dialect that happened to be registered. The exact wording
        // is pinned on the error channel now (it used to be pinned on `lastError()`): the same
        // single producer, reached through `Outcome::error()` instead of a second accessor.
        const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> unknown =
            cpp_pinyin::v2::Engine::open(registry, root, "definitely_not_registered");
        REQUIRE_FALSE(unknown.has_value());
        CHECK(unknown.error().code == cpp_pinyin::v2::Code::NotReady);
        CHECK(unknown.error().detail == "unknown dialect: definitely_not_registered");
    }
}

TEST_CASE("dialect capability flags gate the two switches", "[engine][plugin]") {
    const fs::path root = writeProbeDict();

    SECTION("vToU reaches the converter only when the dialect supports it") {
        cpp_pinyin::v2::Dialect on = probeDialect(true);
        on.tone = FlagTone{};
        cpp_pinyin::v2::Engine with_flag(on, root);

        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = FlagTone::NORMAL;
        cfg.includeCandidates = false;

        cfg.vToU = false;
        CHECK(with_flag.query(u8"你好", cfg).value()[0].rendered == "v:hau3:plain");
        cfg.vToU = true;
        CHECK(with_flag.query(u8"你好", cfg).value()[0].rendered == "V:hau3:plain");

        cpp_pinyin::v2::Dialect off = probeDialect(false);
        off.tone = FlagTone{};
        cpp_pinyin::v2::Engine without_flag(off, root);

        cfg.vToU = false;
        const std::string before = without_flag.query(u8"你好", cfg).value()[0].rendered;
        cfg.vToU = true;
        CHECK(without_flag.query(u8"你好", cfg).value()[0].rendered == before);
        CHECK(before == "v:hau3:plain");
    }

    SECTION("markNeutralAs5 is gated the same way") {
        cpp_pinyin::v2::Dialect on = probeDialect(true);
        on.tone = FlagTone{};
        cpp_pinyin::v2::Engine with_flag(on, root);
        cpp_pinyin::v2::EngineConfig cfg;
        cfg.style = FlagTone::NORMAL;
        cfg.includeCandidates = false;

        cfg.markNeutralAs5 = false;
        CHECK(with_flag.query(u8"你好", cfg).value()[0].rendered == "v:hau3:plain");
        cfg.markNeutralAs5 = true;
        CHECK(with_flag.query(u8"你好", cfg).value()[0].rendered == "v:hau3:n5");

        cpp_pinyin::v2::Dialect off = probeDialect(false);
        off.tone = FlagTone{};
        cpp_pinyin::v2::Engine without_flag(off, root);

        cfg.markNeutralAs5 = false;
        const std::string before = without_flag.query(u8"你好", cfg).value()[0].rendered;
        cfg.markNeutralAs5 = true;
        CHECK(without_flag.query(u8"你好", cfg).value()[0].rendered == before);
    }
}

TEST_CASE("registry semantics", "[engine][registry]") {
    // A registry the CALLER declares -- there is no process-level one, and constructing this object is
    // what puts the built-in dialects on board.
    cpp_pinyin::v2::Registry registry;

    const std::vector<std::string> ids = registry.ids();
    CHECK(std::find(ids.begin(), ids.end(), "mandarin") != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), "cantonese") != ids.end());
    CHECK(std::is_sorted(ids.begin(), ids.end()));  // ids() order is a contract, not registration order

    // Two registries share nothing: an id added to one is invisible to the other, which is what
    // "no process-level registry" has to mean to be worth anything.
    cpp_pinyin::v2::Registry other;
    CHECK(other.resolve("registry_probe") == nullptr);

    CHECK(registry.resolve("definitely_not_registered") == nullptr);

    const auto original = []
    {
        auto dialect = std::make_shared<cpp_pinyin::v2::Dialect>();
        dialect->id = "registry_probe";
        return dialect;
    };
    const auto replacement = []
    {
        auto dialect = std::make_shared<cpp_pinyin::v2::Dialect>();
        dialect->id = "registry_probe_replaced";
        return dialect;
    };

    REQUIRE(registry.add("registry_probe", original, true));
    CHECK(registry.add("registry_probe", original, false) == false);  // id taken, replace not asked
    CHECK(registry.resolve("registry_probe")->id == "registry_probe");

    // A resolved description is a snapshot: replacing the registration must not mutate it, so an
    // engine built from it cannot change behaviour underneath its caller.
    const auto held = registry.resolve("registry_probe");
    REQUIRE(registry.add("registry_probe", replacement, true));
    CHECK(held->id == "registry_probe");
    CHECK(registry.resolve("registry_probe")->id == "registry_probe_replaced");

    CHECK(registry.add("", original, true) == false);
    CHECK(registry.add("registry_probe_empty_creator", cpp_pinyin::v2::DialectCreator{}, true) == false);
}

TEST_CASE("no shared state between instances, across threads", "[engine][thread]") {
    const fs::path root = writeProbeDict();
    const fs::path dictDir = fs::current_path() / "dict";
    const auto texts = sampleTexts();

    // Single-threaded baselines, one per engine each worker will build for itself.
    const cpp_pinyin::v2::Registry registry;
    auto mandarin =
        std::make_unique<cpp_pinyin::v2::Engine>(cpp_pinyin::v2::Engine::open(registry, dictDir, "mandarin").value());
    auto cantonese =
        std::make_unique<cpp_pinyin::v2::Engine>(cpp_pinyin::v2::Engine::open(registry, dictDir, "cantonese").value());
    auto probe = std::make_unique<cpp_pinyin::v2::Engine>(probeDialect(false), root);

    std::vector<std::string> mandarin_baseline;
    std::vector<std::string> cantonese_baseline;
    std::vector<std::string> probe_baseline;
    for (const auto &text : texts) {
        mandarin_baseline.push_back(digest(mandarin->query(text).value()));
        cantonese_baseline.push_back(digest(cantonese->query(text).value()));
        probe_baseline.push_back(digest(probe->query(text).value()));
    }

    constexpr int kThreads = 4;
    constexpr int kIters = 40;
    std::atomic<int> mismatches{0};
    std::atomic<int> exceptions{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(
            [&, t]
            {
                try {
                    // Constructing the engine is what resolves an id through the registry, so this
                    // also exercises concurrent lookups on one registry shared by four threads.
                    const bool useCantonese = (t % 2) == 1;
                    cpp_pinyin::v2::Engine own = useCantonese
                        ? cpp_pinyin::v2::Engine::open(registry, dictDir, "cantonese").value()
                        : cpp_pinyin::v2::Engine::open(registry, dictDir, "mandarin").value();
                    cpp_pinyin::v2::Engine ownProbe(probeDialect(false), root);
                    if (own.dialect().id.empty() || ownProbe.dialect().id.empty()) {
                        ++mismatches;
                        return;
                    }
                    for (int i = 0; i < kIters; ++i) {
                        const size_t k = static_cast<size_t>((i + t) % texts.size());
                        const std::string &expected = useCantonese ? cantonese_baseline[k] : mandarin_baseline[k];
                        if (digest(own.query(texts[k]).value()) != expected)
                            ++mismatches;
                        if (digest(ownProbe.query(texts[k]).value()) != probe_baseline[k])
                            ++mismatches;
                    }
                }
                catch (...) {
                    ++exceptions;
                }
            });
    }
    for (auto &thread : threads)
        thread.join();

    CHECK(mismatches.load() == 0);
    CHECK(exceptions.load() == 0);

    // The long-lived instances must be undamaged too.
    for (size_t k = 0; k < texts.size(); ++k) {
        CHECK(digest(mandarin->query(texts[k]).value()) == mandarin_baseline[k]);
        CHECK(digest(cantonese->query(texts[k]).value()) == cantonese_baseline[k]);
    }
}

TEST_CASE("instances do not share loaded user dictionaries", "[engine][isolation]") {
    const fs::path dictDir = fs::current_path() / "dict";
    const fs::path userDict = fs::temp_directory_path() / "cpp_pinyin_plugin_tests" / "user_dict.txt";
    fs::create_directories(userDict.parent_path());
    {
        std::ofstream out(userDict, std::ios::binary);
        REQUIRE(out.good());
        out << u8"陟罚臧否:zhi1 fa1 zang1 pi3\n";
    }

    const cpp_pinyin::v2::Registry registry;
    cpp_pinyin::v2::Engine a(cpp_pinyin::v2::Engine::open(registry, dictDir, "mandarin").value());
    cpp_pinyin::v2::Engine b(cpp_pinyin::v2::Engine::open(registry, dictDir, "mandarin").value());

    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = static_cast<int>(cpp_pinyin::v2::MandarinStyle::TONE3);
    cfg.includeCandidates = false;

    const std::string before = b.query(u8"陟罚臧否", cfg).value().join();
    REQUIRE(a.query(u8"陟罚臧否", cfg).value().join() == before);

    REQUIRE(a.loadUserDict(userDict));
    const std::string after = a.query(u8"陟罚臧否", cfg).value().join();
    CHECK(after != before);
    CHECK(after == "zhi1 fa1 zang1 pi3");
    CHECK(b.query(u8"陟罚臧否", cfg).value().join() == before);
}

// N14: the second parameter of the callback is documented as an index into `text`, so that is the only
// thing a caller can do with it: `text[position]`. It used to be the index of the character in the
// HANZI sequence instead -- a different number as soon as anything between two hanzi does not become a
// syllable (a space does not). The two parameters must therefore share ONE coordinate system: a UTF-8
// byte offset into `text`. The text below has a space on purpose, and the cases below check the three
// things a byte offset has to satisfy -- inside the string, on a character boundary, and pointing AT
// the character the callback was asked about.
TEST_CASE("The disambiguation callback indexes text in the units the text is given in", "[engine][callback]") {
    cpp_pinyin::v2::Engine engine = cpp_test::mandarin();

    // `行` is polyphonic, and `1` is neither a hanzi nor skippable, so it stays in `text` as its own
    // token: the text is `行` (3 bytes) + `1` (1 byte) + `行` (3 bytes). The LAST character therefore
    // has hanzi index 2 but byte offset 5, and no phrase window can resolve it -- nothing follows it --
    // so it does reach the callback. (No whitespace is involved on purpose: whitespace is dropped from
    // `text` itself, because the text handed to the callback is built from the KEPT tokens. The two
    // numbers disagree from the second character on, which is the whole point.)
    const std::string text = u8"行1行";

    std::vector<std::size_t> offsets;
    std::vector<std::string> asked;

    engine.setDisambiguationCallback(
        [&](std::string_view t, std::size_t position, std::string_view hanzi,
            const std::vector<std::string> &) -> std::string
        {
            offsets.push_back(position);
            asked.emplace_back(hanzi);

            INFO("text=" << t << " position=" << position << " hanzi=" << hanzi);
            // 1. It is a legal index into the text it was handed ...
            REQUIRE(position < t.size());
            // 2. ... and it is a character boundary, not the middle of a multi-byte character ...
            REQUIRE((static_cast<unsigned char>(t[position]) & 0xC0) != 0x80);
            // 3. ... and what it points at is the character the callback is being asked about.
            //    `hanzi` is that character's own text, so its byte length is the length to compare.
            CHECK(t.substr(position, hanzi.size()) == hanzi);
            return {};
        });

    const auto rows = engine.query(text, cpp_pinyin::v2::EngineConfig{});
    REQUIRE(rows.has_value());

    // The callback has to have been reached, or the checks above were never executed and this case
    // would pass while testing nothing.
    REQUIRE_FALSE(offsets.empty());
    CHECK(asked.size() == offsets.size());
}

// N16: this overload's elements are characters (the parameter is `chars` in both declarations), and a
// caller that hands it a word gets a refusal rather than a silent reshape. Measured before the check
// existed: exactly one call site in the tree passed a multi-character element, and it was this file's
// parity case above (an input choice, not a contract).
TEST_CASE("query(vector<string>) refuses an element that is not one character", "[engine][n16]") {
    const fs::path root = fs::current_path() / "dict";
    const cpp_pinyin::v2::Registry registry;
    const cpp_pinyin::v2::Engine engine = cpp_pinyin::v2::Engine::open(registry, root, "mandarin").value();

    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Result> two =
        engine.query(std::vector<std::string>{u8"a", u8"bc"}, cpp_pinyin::v2::EngineConfig{});
    REQUIRE_FALSE(two.has_value());
    CHECK(two.error().code == cpp_pinyin::v2::Code::InvalidArgument);

    // The index is named, so a caller with a long vector does not have to guess which element it was.
    INFO("detail: " << two.error().detail);
    CHECK(two.error().detail.find("element 1") != std::string::npos);

    // And one character per element is still accepted: the refusal is about shape, not about content.
    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Result> ok =
        engine.query(std::vector<std::string>{u8"你", u8"好"}, cpp_pinyin::v2::EngineConfig{});
    REQUIRE(ok.has_value());
}

// N15/C-13: the predicate is the union of the two phrase layers. The comment on
// EngineImpl::hasPolyphonic calls checking only the built-in layer "the gate defect", so a user-dict
// phrase start is exactly the case that separates the two readings of the predicate.
TEST_CASE("isPolyphonic counts a phrase start from the user layer too", "[engine][n15]") {
    const fs::path root = fs::current_path() / "dict";
    const cpp_pinyin::v2::Registry registry;
    cpp_pinyin::v2::Engine engine = cpp_pinyin::v2::Engine::open(registry, root, "mandarin").value();

    const fs::path scratch = fs::temp_directory_path() / "cpp-pinyin2-n15-userdict.txt";
    {
        std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << "\u4e00\u65e6: yi2 dan4" << std::endl;
    }

    const bool before = engine.isPolyphonic("\u4e00");
    INFO("isPolyphonic(\u4e00) before the user layer: " << before);
    REQUIRE(engine.loadUserDict(scratch, true).has_value());
    CHECK(engine.isPolyphonic("\u4e00"));
    // Only the first character is examined: the rest of the argument is ignored either way.
    CHECK(engine.isPolyphonic("\u4e00\u65e6") == engine.isPolyphonic("\u4e00"));
    std::error_code ec;
    fs::remove(scratch, ec);
}
