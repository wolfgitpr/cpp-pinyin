// R13 (docs/STABILITY.md section 4.5): a dialect directory states the data format it was written in, and the
// engine checks that marker BEFORE it reads any table. The load-bearing case is the third: a directory with
// no marker is v1 heritage plain text and must still load -- hand-built dictionaries are not expected to
// grow one.
//
// The channel is this repository's usual one (test_engine_dialect.cpp): `open` returns an `Outcome`, and a
// refusal is `has_value() == false` with `error().code` / `error().detail`. Nothing here relies on an
// exception and nothing loads a full dictionary -- a directory with a marker but no tables fails later, in
// the layer that reads tables. The two refusal cases REQUIRE the marker wording, not merely "an error
// happened": an earlier draft asserted on exceptions, this path threw none, and the cases measured nothing.
// A directory the engine never opens fails loudly.

#include <catch_amalgamated.hpp>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

    /// The engine resolves the dialect subdirectory as `effectiveSubdir` -- the dialect id when the dialect
    /// declares no `dictSubDir` -- so the fixtures below live under `mandarin/`.
    struct Scratch {
        std::filesystem::path root;

        Scratch() {
            static int counter = 0;
            root = std::filesystem::temp_directory_path() / ("cpp-pinyin2-format-" + std::to_string(++counter));
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root / "mandarin");
        }

        ~Scratch() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        void marker(const std::string &text) const { std::ofstream(root / "mandarin" / "format.txt") << text; }

        void dialectJson() const { std::ofstream(root / "mandarin" / "dialect.json") << "{}\n"; }

        /// A manifest that declares a library floor, as the generator writes it.
        void dialectJsonMin(const std::string &version) const {
            std::ofstream(root / "mandarin" / "dialect.json")
                << "{\n    \"id\":  \"mandarin\",\n    \"min_library\":  \"" << version << "\"\n}\n";
        }

        /// The `detail` of a refusal, or an empty string when `open` succeeded.
        std::string detail() const {
            const cpp_pinyin::v2::Registry registry;
            const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
                cpp_pinyin::v2::Engine::open(registry, root, "mandarin");
            return opened.has_value() ? std::string{} : opened.error().detail;
        }

        /// The `Code` of a refusal, or `Code::Ok` when `open` succeeded.
        cpp_pinyin::v2::Code code() const {
            const cpp_pinyin::v2::Registry registry;
            const cpp_pinyin::v2::Outcome<cpp_pinyin::v2::Engine> opened =
                cpp_pinyin::v2::Engine::open(registry, root, "mandarin");
            return opened.has_value() ? cpp_pinyin::v2::Code::Ok : opened.error().code;
        }
    };

    constexpr const char *kMarkerWording = "format marker";

}  // namespace

TEST_CASE("R13: the dialect directory states the data format it was written in", "[dict-format]") {
    SECTION("a supported marker is accepted by the check") {
        Scratch scratch;
        scratch.marker("2\n");
        CHECK(scratch.detail().find(kMarkerWording) == std::string::npos);
    }

    SECTION("a marker newer than the supported one is refused, naming value and limit") {
        Scratch scratch;
        scratch.marker("3\n");
        const std::string detail = scratch.detail();
        REQUIRE(detail.find(kMarkerWording) != std::string::npos);
        CHECK(detail.find("mandarin") != std::string::npos);  // which directory
        CHECK(detail.find("3") != std::string::npos);  // what it found
        CHECK(detail.find("2") != std::string::npos);  // what this library supports
    }

    SECTION("a marker that is not a number is refused") {
        Scratch scratch;
        scratch.marker("v2\n");
        const std::string detail = scratch.detail();
        REQUIRE(detail.find(kMarkerWording) != std::string::npos);
        CHECK(detail.find("not a number") != std::string::npos);
    }

    SECTION("a directory carrying dialect.json but no marker is refused") {
        Scratch scratch;
        scratch.dialectJson();
        const std::string detail = scratch.detail();
        REQUIRE(detail.find(kMarkerWording) != std::string::npos);
        CHECK(detail.find("dialect.json") != std::string::npos);
    }

    SECTION("a directory with neither marker nor dialect.json is v1 heritage and passes the check") {
        Scratch scratch;
        CHECK(scratch.detail().find(kMarkerWording) == std::string::npos);
    }
}

TEST_CASE("D4: a manifest may declare the library version its data needs", "[dict-format]") {
    // The code matters as much as the wording: a host reading `error().code` must be able to tell
    // "upgrade the library" from "the data is broken" without parsing English.
    SECTION("a floor at or below this library is accepted") {
        Scratch scratch;
        scratch.marker("2\n");
        scratch.dialectJsonMin("1.0.0");
        // Not `Code::Ok`: the fixture carries no tables, so the LOAD stage after this check still fails.
        // What is being asserted is that the floor added no refusal of its own.
        CHECK(scratch.code() != cpp_pinyin::v2::Code::DictIncompatible);
        CHECK(scratch.detail().find("requires library") == std::string::npos);
    }

    SECTION("a floor above this library is refused with its OWN code, naming both versions") {
        Scratch scratch;
        scratch.marker("2\n");
        scratch.dialectJsonMin("99.0.0");
        REQUIRE(scratch.code() == cpp_pinyin::v2::Code::DictIncompatible);
        const std::string detail = scratch.detail();
        CHECK(detail.find("99.0.0") != std::string::npos);  // what the data asks for
        CHECK(detail.find("requires library") != std::string::npos);
    }

    SECTION("a manifest with no floor makes no claim (hand-built directories stay usable)") {
        Scratch scratch;
        scratch.marker("2\n");
        scratch.dialectJson();  // "{}"
        CHECK(scratch.code() != cpp_pinyin::v2::Code::DictIncompatible);
        CHECK(scratch.detail().find("requires library") == std::string::npos);
    }

    SECTION("a floor that is not a version is a LOAD failure, not an incompatibility") {
        Scratch scratch;
        scratch.marker("2\n");
        scratch.dialectJsonMin("next release");
        REQUIRE(scratch.code() == cpp_pinyin::v2::Code::Load);
        CHECK(scratch.detail().find("not a version") != std::string::npos);
    }
}
