
#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/dialects/ManTone.h>

#include "BuiltinDialects.h"

namespace cpp_pinyin::v2
{
    namespace detail
    {
        Dialect makeMandarinDialect() {
            Dialect dialect;
            dialect.id = "mandarin";
            dialect.dictSubDir = "mandarin";
            // ManTone must be in place before the engine loads anything: it decides both the output
            // styles and the tone3 -> tone-marked normalization of user_dict.txt, and the
            // dictionaries are loaded inside the engine's constructor.
            dialect.tone = ManTone{};
            dialect.supportsVToU = true;
            dialect.supportsNeutralTone5 = true;
            // The style identity (N3): the nine names a caller may write, and which one a caller
            // gets when it asks for nothing. The numbers are `ManTone::Style`'s -- Types.h owns
            // them -- and the default is TONE, which is what the `EngineConfig` this dialect used
            // to be reached through defaulted to. The table is checked by
            // tests/cpp/test_style_table.cpp, not by rendering: a wrong name renders fine.
            dialect.styles = {
                {"normal", static_cast<int>(ManTone::Style::NORMAL), false},
                {"tone", static_cast<int>(ManTone::Style::TONE), true},
                {"tone2", static_cast<int>(ManTone::Style::TONE2), false},
                {"first-letter", static_cast<int>(ManTone::Style::FIRST_LETTER), false},
                {"bopomofo", static_cast<int>(ManTone::Style::BOPOMOFO), false},
                {"initials", static_cast<int>(ManTone::Style::INITIALS), false},
                {"finals", static_cast<int>(ManTone::Style::FINALS), false},
                {"finals-tone3", static_cast<int>(ManTone::Style::FINALS_TONE3), false},
                {"tone3", static_cast<int>(ManTone::Style::TONE3), false},
            };
            // Tone sandhi is a dialect fact, declared here instead of being inferred by every caller: this
            // is what makes `cppp2_apply_tone_sandhi(engine, ...)` ask instead of assume (core/ToneSandhi.h).
            dialect.toneSandhi = ToneSandhiPass::Mandarin;
            return dialect;
        }
    }  // namespace detail

}  // namespace cpp_pinyin::v2
