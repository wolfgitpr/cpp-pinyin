#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    /// Which tone-sandhi pass a dialect declares -- the ONE place that answers "does this dialect want
    /// tone sandhi, and with which rules?", so no caller has to assume the Mandarin rules apply everywhere.
    /// `None` (the default) means "this dialect declares no tone-sandhi pass".
    ///
    /// Why this exists at all: `ToneSandhiOptions` and `applyMandarinToneSandhi` are Mandarin's pass, and
    /// until 2026-10-18 the C entry `cppp2_apply_tone_sandhi` had no dialect parameter at all -- it applied
    /// the 不/一 rules to whatever result it was handed, and the only thing standing between a Cantonese
    /// caller and a wrong answer was one sentence in a comment. Tone sandhi is a property OF A DIALECT, so
    /// the dialect declares it and every entry point asks the dialect instead of assuming.
    ///
    /// Forward compatibility, deliberately: a second pass is a NEW enumerator plus a NEW C symbol
    /// (`cppp2_apply_<dialect>_sandhi`; CONTRACT section 1 allows adding symbols inside 2.x) -- never a
    /// change to an existing signature. Cantonese's 變調 is lexical tone change rather than the
    /// third-tone/不/一 rules, so it will not reuse `ToneSandhiOptions` either.
    enum class ToneSandhiPass {
        None,
        Mandarin,
    };

    // Switches for Mandarin tone sandhi. All three rules are on by default and can be disabled
    // individually.
    struct CPP_PINYIN2_EXPORT ToneSandhiOptions {
        // A run of third tones: every one but the last becomes second (你好 -> ní hǎo, 老鼠 -> láo shǔ).
        bool thirdTone = true;
        // 不 + fourth tone -> second (不是 -> bú shì); otherwise -> fourth (不好 -> bù hǎo).
        bool bu = true;
        // 一 + fourth tone -> second (一定 -> yí dìng); 一 + first/second/third -> fourth (一天 -> yì tiān);
        // at the end or before a numeral -> first (万一 -> wàn yī, 一一 -> yī yī).
        bool yi = true;
    };

    // The Mandarin tone-sandhi post-processing step. It rewrites `rendered` and, in the C++ `Result`, the one
    // structured field that describes the rewrite: `Segment::reading.tone` -- written even when the caller did
    // not ask for structured readings, because that field is the rule's own output, not a re-derivation.
    //
    // NOT rewritten: tokenisation, phrase matching and disambiguation are not re-run, and `text` /
    // `candidates` / `failure_kind` are left alone. The row count does not change.
    //
    // THE C FACE IS DIFFERENT ON PURPOSE: `cppp2_apply_tone_sandhi` refreshes `rendered` only and leaves
    // `cppp2_segment::syllables[k].tone` at the table value (that struct is a read-only view of dictionary
    // data, `c_api.h`), so a C consumer reads the post-sandhi tone out of `rendered`.
    //
    // Scope: consecutive non-failed rows -- punctuation, non-Hanzi, and the end of the result all count as
    // boundaries. Only readings shaped like "zhong1"/"le5"/"ong4" (TONE3 / FINALS_TONE3) are rewritten;
    // under the other styles (TONE, TONE2, NORMAL, BOPOMOFO, ...) the tone cannot be recovered reliably, so
    // those readings are kept as they are. Returns the number of rewritten rows.
    //
    // ! Applies to MANDARIN results only. Cantonese jyutping happens to have the same "letters plus a
    // trailing digit" shape, but the 不/一 rules do not apply there and using this would change readings to
    // the wrong ones.
    //
    // WHY THIS TAKES A `Result` AND NOT AN `Engine` (2026-10-20, audit H06): `Result` carries no dialect, so this
    // function CANNOT refuse a Cantonese result the way `cppp2_apply_tone_sandhi` does -- that entry takes the
    // engine for exactly this reason. An `Engine`-taking overload here would be a public-surface addition
    // (`tests/abi/expected_public_members.txt`) nobody has asked for, so the precondition above -- MANDARIN
    // only -- is the answer instead. The `options` default already gives the shorter spelling a reader expects:
    // `applyMandarinToneSandhi(result)`.
    //
    // Deliberate differences from pypinyin's `contrib.tone_sandhi`: pypinyin applies the third-tone run only
    // when that run ends exactly at the end of a segment (an implementation defect), while this applies the
    // rule itself to every third-tone run; and "一 + numeral/一 -> first tone" follows the documented rule
    // (pypinyin does not implement that exception).
    CPP_PINYIN2_EXPORT size_t applyMandarinToneSandhi(Result &result, const ToneSandhiOptions &options = {});
}  // namespace cpp_pinyin::v2
