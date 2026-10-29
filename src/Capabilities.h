#pragma once

// The capability mask, in ONE place.
//
// Rule: an option a dialect does not support is IGNORED, never reinterpreted. Asking for `vToU` on a
// dialect whose `supportsVToU` is false must behave exactly as if the request had not been made -- not as
// "the other mode", not as an error.
//
// WHY THIS FILE EXISTS: the expression had been copy-pasted into every entry point (`Engine::query` both
// overloads, `Engine::readings`, and `cppp2_render_syllable` -- the last one inside the C face, which
// renders a single syllable by hand). Each copy carried a comment explaining the same rule, which is the
// signal that the rule itself had no home.
//
// Internal (under src/), not public: in `ext/Dialect.h` it would add a public name to the frozen list
// (`tests/abi/expected_public_names.v2.txt`) for something no consumer needs, and `core/` cannot hold it:
// the rule needs `ext/Dialect.h`, which a `core/` file may not include (`tools/check_layers.py`).

#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Dialect.h>

namespace cpp_pinyin::v2
{
    namespace detail
    {
        struct EffectiveCaps {
            bool vToU;
            bool neutralTone5;
        };

        /// The caller's switches, with the dialect's capabilities applied.
        [[nodiscard]] inline EffectiveCaps effectiveCaps(const Dialect &dialect, const EngineConfig &cfg) noexcept {
            return EffectiveCaps{cfg.vToU && dialect.supportsVToU, cfg.markNeutralAs5 && dialect.supportsNeutralTone5};
        }
    }  // namespace detail
}  // namespace cpp_pinyin::v2
