// The two out-of-line pieces of the 2.0 face's error channel (declared in core/Types.h): the single
// text authority for `Code`, and the exception `Outcome::value_or_throw()` throws. Out of line on
// purpose -- `codeText` must exist exactly once in the library, so no binding can compile in a
// second copy of the wording, and `ErrorException` derives from `std::runtime_error`, whose
// constructor belongs in a translation unit.

#include <cpp-pinyin2/core/Types.h>

#include <utility>

namespace cpp_pinyin::v2
{
    const char *codeText(Code code) noexcept {
        switch (code) {
        case Code::Ok:
            return "ok";
        case Code::InvalidArgument:
            return "invalid argument";
        case Code::NotReady:
            return "not ready";
        case Code::Load:
            return "load failed";
        case Code::Internal:
            return "internal error";
        case Code::DictIncompatible:
            return "dictionary incompatible";
        }
        // Reached only by a value cast into `Code` from outside its domain. Returning a string
        // rather than aborting keeps a hostile caller from turning a lookup into a crash, and the
        // text makes the unknown value visible instead of silent.
        return "unknown code";
    }

    ErrorException::ErrorException(Error error) : std::runtime_error(error.detail), m_error(std::move(error)) {}

    const Error &ErrorException::error() const noexcept { return m_error; }
}  // namespace cpp_pinyin::v2
