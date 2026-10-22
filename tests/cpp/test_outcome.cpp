// The 2.0 face's error channel: `Code`, `codeText` and `Outcome<T>` (declared in core/Types.h).
//
// Why these assertions exist rather than "it compiles": `Code` is a FROZEN value domain (the plan says
// new codes may only be appended, the 2.0 plan's section 修订层 B -- now docs/CONTRACT.md; that ledger was
// retired in the 2026-10-09 documentation cut), so its numbers are
// pinned here; and `codeText` is the single authority for the wording the C face will mirror as
// `cppp2_code_text`, so "every code has a text, and no two codes share one" is exactly the property
// that keeps a binding from inventing its own reason strings (docs/CONTRACT.md; the design ledger that
// said it at :144 was retired in the 2026-10-09 documentation cut).

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <cpp-pinyin2/core/Types.h>

using cpp_pinyin::v2::Code;
using cpp_pinyin::v2::Error;
using cpp_pinyin::v2::ErrorException;
using cpp_pinyin::v2::Outcome;

namespace
{
    struct MoveOnly {
        explicit MoveOnly(int v) : value(v) {}
        MoveOnly(MoveOnly &&) noexcept = default;
        MoveOnly &operator=(MoveOnly &&) noexcept = default;
        MoveOnly(const MoveOnly &) = delete;
        MoveOnly &operator=(const MoveOnly &) = delete;

        int value = 0;
    };
}  // namespace

TEST_CASE("Code is a frozen append-only domain (currently 0..5) with no Unsupported", "[outcome][contract]") {
    // Appending is allowed; renumbering and reviving `Unsupported` are not (it had no producer and no
    // consumer, and the contract already spells "unsupported" as "the input is returned unchanged").
    STATIC_REQUIRE(static_cast<int>(Code::Ok) == 0);
    STATIC_REQUIRE(static_cast<int>(Code::InvalidArgument) == 1);
    STATIC_REQUIRE(static_cast<int>(Code::NotReady) == 2);
    STATIC_REQUIRE(static_cast<int>(Code::Load) == 3);
    STATIC_REQUIRE(static_cast<int>(Code::Internal) == 4);
    // Appended 2026-10-18 (D4): a dictionary that asks for a newer library. It sits AFTER every code that
    // existed then, which is the whole of the append-only rule.
    STATIC_REQUIRE(static_cast<int>(Code::DictIncompatible) == 5);

    // A default-constructed `Error` is the one that no operation produced.
    const Error fresh;
    REQUIRE(fresh.code == Code::Ok);
    REQUIRE(fresh.detail.empty());
}

TEST_CASE("codeText covers every code, and no two codes share one", "[outcome][contract]") {
    const Code all[] = {Code::Ok,   Code::InvalidArgument, Code::NotReady,
                        Code::Load, Code::Internal,        Code::DictIncompatible};
    std::set<std::string> texts;
    for (const Code code : all) {
        const std::string text = cpp_pinyin::v2::codeText(code);
        INFO("code = " << static_cast<int>(code));
        REQUIRE_FALSE(text.empty());
        texts.insert(text);
    }
    REQUIRE(texts.size() == sizeof(all) / sizeof(all[0]));

    // A value outside the declared domain still answers with a string instead of crashing: a
    // binding that reads a code written by a NEWER library must not take the process down.
    const std::string unknown = cpp_pinyin::v2::codeText(static_cast<Code>(99));
    REQUIRE_FALSE(unknown.empty());
    REQUIRE(texts.count(unknown) == 0);
}

TEST_CASE("Outcome carries either a value or an error, never both", "[outcome]") {
    const Outcome<int> ok(7);
    REQUIRE(ok.has_value());
    REQUIRE(static_cast<bool>(ok));
    REQUIRE(ok.value() == 7);
    REQUIRE(ok.value_or_throw() == 7);

    const Outcome<int> bad(Error{Code::Load, "failed to load: word.txt (under /nowhere)"});
    REQUIRE_FALSE(bad.has_value());
    REQUIRE_FALSE(static_cast<bool>(bad));
    REQUIRE(bad.error().code == Code::Load);
    REQUIRE(bad.error().detail == "failed to load: word.txt (under /nowhere)");
}

TEST_CASE("value_or_throw throws ErrorException and keeps the code", "[outcome]") {
    const Outcome<int> bad(Error{Code::NotReady, "unknown dialect: nope"});

    try {
        (void)bad.value_or_throw();
        FAIL("value_or_throw() on an error must throw");
    }
    catch (const ErrorException &exc) {
        REQUIRE(exc.error().code == Code::NotReady);
        REQUIRE(exc.error().detail == "unknown dialect: nope");
        // `what()` is the detail, because that is what a generic handler prints.
        REQUIRE(std::string(exc.what()) == "unknown dialect: nope");
    }

    // A caller that only knows about std types can still catch it.
    REQUIRE_THROWS_AS(bad.value_or_throw(), std::runtime_error);
    REQUIRE_NOTHROW(Outcome<int>(7).value_or_throw());
}

TEST_CASE("Asking for the wrong side throws; the failure itself never does", "[outcome][contract]") {
    // docs/CONTRACT.md section 2, "Exceptions": a fallible call answers an `Outcome` and throws
    // nothing by itself -- asking for the side that is not there is a programming error. `value()`
    // on a failure throws `ErrorException` (it has a `Code` to carry), while `error()` on a success
    // throws `std::bad_variant_access`: there is deliberately no `Code` for "wrong question",
    // because the domain is frozen and a caller's bug is not a library outcome.
    const Outcome<int> ok(7);
    REQUIRE_THROWS_AS(ok.error(), std::bad_variant_access);

    const Outcome<int> bad(Error{Code::Load, "failed"});
    REQUIRE_THROWS_AS(bad.value(), ErrorException);  // D12: no bare std::get escape any more
    REQUIRE_THROWS_AS(bad.value_or_throw(), ErrorException);

    // `Outcome<void>` has no alternative to get wrong, so `error()` is a plain accessor there.
    const Outcome<void> done;
    REQUIRE(done.has_value());
    REQUIRE(done.error().code == Code::Ok);
}

TEST_CASE("Outcome moves a move-only value out instead of copying it", "[outcome]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible<MoveOnly>::value);
    STATIC_REQUIRE(std::is_move_constructible<Outcome<MoveOnly>>::value);

    Outcome<MoveOnly> ok(MoveOnly{11});
    REQUIRE(ok.has_value());
    const MoveOnly taken = std::move(ok).value();
    REQUIRE(taken.value == 11);

    // The throwing path must not need a copy of the value either.
    Outcome<MoveOnly> bad(Error{Code::Internal, "callback threw"});
    try {
        (void)std::move(bad).value_or_throw();
        FAIL("value_or_throw() on an error must throw");
    }
    catch (const ErrorException &exc) {
        REQUIRE(exc.error().code == Code::Internal);
    }
}
