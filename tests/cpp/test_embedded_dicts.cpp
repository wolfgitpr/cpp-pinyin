// D3 (docs/DECISIONS.md section 4.1): a build with CPP_PINYIN2_EMBED_DICTS carries the
// tables itself, so a host may pass a dictionary root that does not exist -- the single-file deployment case
// (one .exe, an AppImage, a Unity/UE plugin, a mobile bundle).
//
// Two halves, and both matter:
//   * the embedded build MUST work with no resource directory at all (otherwise the feature is decoration);
//   * a build without the tables MUST still refuse, so this test cannot quietly turn into "everything works".
// The preprocessor picks a half from the macro the library publishes; in a non-embedded build the second one
// is compiled and the first is not a test at all.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <filesystem>
#include <string>

namespace
{
    /// A root that cannot exist: the test must not depend on a path someone forgot to clean up.
    std::filesystem::path absentRoot() {
        std::error_code ec;
        const std::filesystem::path root =
            std::filesystem::temp_directory_path(ec) / "cpp-pinyin2-embedded-absent-root";
        if (!ec)
            std::filesystem::remove_all(root, ec);
        return root;
    }
}  // namespace

#ifdef CPP_PINYIN2_EMBED_DICTS

TEST_CASE("D3: an embedded build opens with no dictionary root on disk", "[embed][engine]") {
    const cpp_pinyin::v2::Registry registry;
    const std::filesystem::path root = absentRoot();
    REQUIRE_FALSE(std::filesystem::exists(root));

    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
        cpp_pinyin::v2::Engine::open(registry, root, "mandarin");
    REQUIRE(opened.has_value());

    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Result> result = opened.value().query("中国");
    REQUIRE(result.has_value());
    CHECK(result.value().join() == "zhōng guó");
}

TEST_CASE("D3: the embedded build serves both dialects", "[embed][engine]") {
    const cpp_pinyin::v2::Registry registry;
    const std::filesystem::path root = absentRoot();
    for (const char *dialect : {"mandarin", "cantonese"}) {
        const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
            cpp_pinyin::v2::Engine::open(registry, root, dialect);
        REQUIRE(opened.has_value());
        CHECK(opened.value().query("你好").has_value());
    }
}

#else

TEST_CASE("D3: a build without embedded tables refuses a root that does not exist", "[embed][engine]") {
    // The counterpart of the case above. Without it, "an embedded build works" could be satisfied by a library
    // that silently invented dictionaries in every configuration.
    const cpp_pinyin::v2::Registry registry;
    const std::filesystem::path root = absentRoot();
    const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
        cpp_pinyin::v2::Engine::open(registry, root, "mandarin");
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == cpp_pinyin::v2::Code::Load);
}

#endif
