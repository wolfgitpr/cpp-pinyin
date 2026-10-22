#pragma once

// The capability mask, in ONE place.
//
// Rule: an option a dialect does not support is IGNORED, never reinterpreted. If the caller asks for
// `vToU` on a dialect whose `supportsVToU` is false, the request must behave exactly as if it had
// not been made -- not as "the other mode", not as an error.
//
// WHY THIS FILE EXISTS (2026-10-19 audit, F28): the expression was written out four times --
// `Engine::query` (both overloads), `Engine::readings`, and `cppp2_render_syllable`, the last one
// inside the C face, which renders a single syllable BY HAND instead of going through
// `Engine::query`. Each copy carried a comment explaining the same rule, which is the signal that
// the rule itself had no home. A fifth entry point would have had to remember to copy it too.
//
// Internal (under src/), not public: putting it in `ext/Dialect.h` would add a public name to the
// frozen 38-name list for something no consumer needs, and `core/` cannot hold it because it must
// include `ext/Dialect.h` -- that is the tier order, not an accident.

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
