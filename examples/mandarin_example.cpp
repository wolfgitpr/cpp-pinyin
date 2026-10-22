#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
    // The library has no implicit dictionary path on purpose: `Engine::open` is told where the tables are.
    // This example is the layer that decides, and it follows the precedence the bindings use (D1 of
    // docs/DECISIONS.md section 4.1): CPP_PINYIN2_DICT_DIR when set (a deployment
    // override), otherwise `dict/` under the working directory (what the build tree provides).
    const char *dictDirEnv = std::getenv("CPP_PINYIN2_DICT_DIR");
    const std::filesystem::path dictDir = (dictDirEnv != nullptr && *dictDirEnv != '\0')
        ? std::filesystem::path(dictDirEnv)
        : std::filesystem::current_path() / "dict";

    cpp_pinyin::v2::Registry registry;
    auto opened = cpp_pinyin::v2::Engine::open(registry, dictDir, "mandarin");
    if (!opened) {
        std::cerr << "Failed to load dictionaries: " << opened.error().detail << std::endl;
        return 1;
    }
    cpp_pinyin::v2::Engine g2p = std::move(opened.value());

    // ---- Example 1: Basic conversion (TONE style) ----
    std::cout << "=== TONE style ===" << std::endl;
    cpp_pinyin::v2::Result res = g2p.query("中国人民").value();
    std::cout << res.join() << std::endl;  // zhōng guó rén mín

    // ---- Example 2: TONE3 style with config ----
    std::cout << "\n=== TONE3 style ===" << std::endl;
    cpp_pinyin::v2::EngineConfig cfg;
    cfg.style = cpp_pinyin::v2::MandarinStyle::TONE3;
    auto res2 = g2p.query("你好世界", cfg).value();
    std::cout << res2.join() << std::endl;  // ni3 hao3 shi4 jie4

    // ---- Example 3: Polyphonic disambiguation ----
    std::cout << "\n=== Polyphonic disambiguation ===" << std::endl;
    std::cout << g2p.query("银行行长", cfg).value().join() << std::endl;
    // yin2 hang2 hang2 zhang3

    // ---- Example 4: Iterate over results ----
    std::cout << "\n=== Iterate results ===" << std::endl;
    for (const auto &item : res) {
        std::cout << item.text << " -> " << item.rendered;
        if (!item.candidates.empty()) {
            std::cout << "  [";
            for (size_t i = 0; i < item.candidates.size(); i++) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << item.candidates[i];
            }
            std::cout << "]";
        }
        std::cout << std::endl;
    }

    // ---- Example 5: Options ----
    std::cout << "\n=== Options ===" << std::endl;
    cfg.vToU = true;
    std::cout << "vToU=true:    " << g2p.query("虐", cfg).value().join() << std::endl;  // nüe4
    cfg.vToU = false;
    cfg.markNeutralAs5 = true;
    std::cout << "neutral=5:    " << g2p.query("还是还给他", cfg).value().join() << std::endl;
    // hai2 shi5 huan2 gei3 ta1

    // ---- Example 6: All styles ----
    std::cout << "\n=== All styles ===" << std::endl;
    auto showStyle = [&](const std::string &label, cpp_pinyin::v2::MandarinStyle s)
    {
        cpp_pinyin::v2::EngineConfig c;
        c.style = s;
        std::cout << label << ": " << g2p.query("中国", c).value().join() << std::endl;
    };
    showStyle("NORMAL", cpp_pinyin::v2::MandarinStyle::NORMAL);
    showStyle("TONE", cpp_pinyin::v2::MandarinStyle::TONE);
    showStyle("TONE2", cpp_pinyin::v2::MandarinStyle::TONE2);
    showStyle("TONE3", cpp_pinyin::v2::MandarinStyle::TONE3);
    showStyle("FIRST_LETTER", cpp_pinyin::v2::MandarinStyle::FIRST_LETTER);
    showStyle("BOPOMOFO", cpp_pinyin::v2::MandarinStyle::BOPOMOFO);

    // ---- Example 7: Helper methods ----
    std::cout << "\n=== Helpers ===" << std::endl;
    // `toSimplified` answers an `Outcome`: `.value_or_throw()` reads it and throws the library's
    // `ErrorException` on a not-ready engine, which is what an example should show.
    std::cout << "toSimplified(國): " << g2p.toSimplified("國").value_or_throw() << std::endl;
    std::cout << "isPolyphonic(行): " << std::boolalpha << g2p.isPolyphonic("行") << std::endl;

    // ---- Example 8: Readings ----
    std::cout << "\n=== Readings ===" << std::endl;
    auto r = g2p.readings("行", cfg).value();
    for (const auto &s : r) {
        std::cout << "  " << s << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
