// A dialect defined entirely outside cpp-pinyin2: one translation unit, no repository source, the library
// reached through the installed package. The executable form of the claim in docs/Custom Chinese Dialect.md
// -- adding a dialect is a description plus a translation unit, not a patch to the library.
//
// What it demonstrates, in the order the code does it:
//   1. a `ToneConverter` subclass with its OWN style numbering (an out-of-tree dialect cannot extend
//      `MandarinStyle`/`CantoneseStyle`, which is why `ToneConverter` carries no styles of its own).
//   2. a `Dialect` registered into a registry the caller owns: the id is reachable exactly because `main`
//      put it into the registry it then hands to `Engine::open` -- no process-level table.
//   3. two engines over the SAME dictionary root (built-in dialect and this one), the point of the root
//      being a constructor argument: no process-wide "current dictionary".
//
// The style here is deliberately mechanical (ASCII letters upper-cased, tone marks left alone): the subject
// is the plumbing, and a transform the reader can redo by eye is easier to trust than a clever one nobody
// can check. A real dialect's converter lives in its own `*Tone.cpp`.

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace
{

    // Tone-marked pinyin is ASCII apart from the marked vowels, so upper-casing the ASCII code units
    // leaves `ǐ`, `ǎ`, `ǒ`, `ǔ` and `ü` as they are: the output stays readable and the transform stays
    // verifiable by looking at it.
    //
    // The signature is `ToneConverter::ConvertFunc` exactly -- pinyin, plus the two switches every style
    // receives whether it uses them or not. This style ignores both; a real dialect uses them for `u`/`v`
    // spelling and for the neutral-tone marker.
    std::u16string upperAscii(const std::u16string &pinyin, bool /*v_to_u*/, bool /*neutral_tone_with_five*/) {
        std::u16string out;
        out.reserve(pinyin.size());
        for (char16_t unit : pinyin) {
            if (unit < 0x80) {
                out.push_back(static_cast<char16_t>(std::toupper(static_cast<int>(unit))));
            } else {
                out.push_back(unit);
            }
        }
        return out;
    }

    class DemoUpperTone final : public cpp_pinyin::v2::ToneConverter {
    public:
        enum Style { NORMAL = 0 };

        DemoUpperTone() { m_converts[static_cast<int>(NORMAL)] = &upperAscii; }
    };

    std::shared_ptr<const cpp_pinyin::v2::Dialect> makeDemoDialect() {
        auto dialect = std::make_shared<cpp_pinyin::v2::Dialect>();
        dialect->id = "demo-upper";
        // The tables are the ones this repository ships, so the example runs wherever the dictionaries do.
        // A real out-of-tree dialect points this at its own directory name under the dictionary root.
        dialect->dictSubDir = "mandarin";
        dialect->tone = DemoUpperTone{};
        return dialect;
    }

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: plugin_dialect <dictionary root>\n";
        return 2;
    }
    const std::filesystem::path dictRoot(argv[1]);

    // The registry this program owns. Constructing it registers the built-in dialects; the one below
    // adds ours next to them, in the same table, through the same entry point.
    cpp_pinyin::v2::Registry registry;
    if (!registry.add("demo-upper", makeDemoDialect)) {
        std::cerr << "registering 'demo-upper' was refused\n";
        return 1;
    }

    // `open` is the entry point that reports WHY it could not build an engine (S11a): an outcome with
    // no value carries the classification and the detail, so a failure is reported here -- where the
    // caller can act on it -- instead of surfacing at the next accessor.
    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> builtIn =
        cpp_pinyin::v2::Engine::open(registry, dictRoot, "mandarin");
    if (!builtIn) {
        std::cerr << "the built-in dialect did not load: " << builtIn.error().detail << "\n";
        return 1;
    }
    // An id the registry does not know is an error, never a fallback to a default dialect -- if
    // registration above had not happened, this is where it would show.
    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> demo =
        cpp_pinyin::v2::Engine::open(registry, dictRoot, "demo-upper");
    if (!demo) {
        std::cerr << "the registered dialect did not resolve: " << demo.error().detail << "\n";
        return 1;
    }

    cpp_pinyin::v2::EngineConfig config;  // style 0 for both engines -- but read what that means two lines down
    const std::string text = "你好世界";
    const std::string fromBuiltIn = builtIn.value().query(text, config).value().join();
    const std::string fromPlugin = demo.value().query(text, config).value().join();

    std::cout << "built-in : " << fromBuiltIn << "\n";
    std::cout << "plugin   : " << fromPlugin << "\n";

    // The two outputs differ by more than the transform, and that is the fact worth reading here: a
    // style number means whatever the dialect defining it says it means. Mandarin's slot 0 turns the
    // tone-marked dictionary reading into unmarked pinyin; this dialect's slot 0 passes the reading
    // through and upper-cases the ASCII. So the expectation below is this dialect's OWN invariant --
    // the converter ran and left no lowercase ASCII -- rather than a comparison between the two
    // dialects, which would be comparing two different conventions.
    if (fromPlugin.empty()) {
        std::cerr << "the registered dialect produced no output at all\n";
        return 1;
    }
    for (char byte : fromPlugin) {
        if (byte >= 'a' && byte <= 'z') {
            std::cerr << "the dialect's converter did not reach the result: \"" << fromPlugin
                      << "\" still contains lowercase ASCII, so no dialect-specific conversion ran\n";
            return 1;
        }
    }
    const auto syllables = [](const std::string &s)
    { return static_cast<size_t>(std::count(s.begin(), s.end(), ' ')); };
    if (syllables(fromPlugin) != syllables(fromBuiltIn)) {
        std::cerr << "the dialect changed the shape of the result: " << syllables(fromPlugin)
                  << " syllable separators against the built-in dialect's " << syllables(fromBuiltIn) << "\n";
        return 1;
    }
    if (fromPlugin == fromBuiltIn) {
        std::cerr << "the plugin output equals the built-in output, so no dialect-specific conversion "
                     "ran\n";
        return 1;
    }

    const auto ids = registry.ids();
    std::cout << "registered ids:";
    for (const auto &id : ids) {
        std::cout << ' ' << id;
    }
    std::cout << "\nRESULT: OK\n";
    return 0;
}
