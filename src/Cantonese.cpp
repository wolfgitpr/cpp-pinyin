
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/dialects/CanTone.h>

#include "BuiltinDialects.h"

namespace cpp_pinyin::v2
{
    namespace detail
    {
        Dialect makeCantoneseDialect() {
            Dialect dialect;
            dialect.id = "cantonese";
            dialect.dictSubDir = "cantonese";
            // CanTone does not override ToneConverter::normalizeReading: Cantonese tables already
            // use jyutping tone digits, so the identity map is correct (Mandarin's tone3 -> marked
            // rewrite would turn jyut6 into jyu6t6 / gwóng and similar garbage).
            dialect.tone = CanTone{};
            dialect.supportsVToU = false;
            dialect.supportsNeutralTone5 = false;
            // Cantonese occupies THREE numbers of the shared domain, and they are not consecutive:
            // 0 (normal), 3 (first-letter) and 8 (tone3) -- see `CantoneseStyle`. The gaps are not
            // an oversight and must not be "tidied up": the numbers are the C ABI's domain, shared
            // with Mandarin, where 1 means "tone", 2 means "tone2" and so on. TONE3 is the default,
            // as the `EngineConfig` this dialect used to be reached through defaulted to.
            // tests/cpp/test_style_table.cpp pins all of it, including that "tone" does NOT resolve
            // here.
            dialect.styles = {
                {"normal", static_cast<int>(CanTone::Style::NORMAL), false},
                {"first-letter", static_cast<int>(CanTone::Style::FIRST_LETTER), false},
                {"tone3", static_cast<int>(CanTone::Style::TONE3), true},
            };
            // NO tone-sandhi pass, and leaving it at `None` is a decision rather than an omission: 不/一 do
            // not exist in Cantonese, and its 變調 is lexical tone change rather than the Mandarin rules.
            // That is what makes the C entry refuse instead of rewriting jyutping with rules that do not
            // apply here.
            return dialect;
        }
    }  // namespace detail

}  // namespace cpp_pinyin::v2
