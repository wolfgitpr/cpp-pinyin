#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/Types.h>

namespace cpp_pinyin::v2
{
    /// Which tone-sandhi pass a dialect declares -- the ONE place that answers "does this dialect want
    /// tone sandhi, and with which rules?". `None` (the default) means "declares no pass". Tone sandhi is
    /// a property OF A DIALECT: every entry point asks the dialect instead of assuming Mandarin's rules.
    ///
    /// Forward compatibility: a second pass is a NEW enumerator plus a NEW C symbol
    /// (`cppp2_apply_<dialect>_sandhi`; CONTRACT section 1 allows adding symbols inside 2.x), never a
    /// change to an existing signature. Cantonese's 變調 is lexical tone change, not the 不/一 rules, so
    /// it will not reuse `ToneSandhiOptions`.
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

    // The Mandarin tone-sandhi post-processing step. Rewrites `rendered` and, in the C++ `Result`, the
    // structured field that describes the rewrite (`Segment::reading.tone`, written even when the caller
    // did not ask for structured readings -- it is the rule's own output, not a re-derivation).
    //
    // NOT rewritten: tokenisation, phrase matching, disambiguation, `text` / `candidates` /
    // `failure_kind`. The row count does not change.
    //
    // THE C FACE IS DIFFERENT ON PURPOSE: `cppp2_apply_tone_sandhi` refreshes `rendered` only and leaves
    // `cppp2_segment::syllables[k].tone` at the table value (that struct is a read-only view of
    // dictionary data, `include/cpp-pinyin2/ext/c_api.h`), so a C consumer reads the tone from `rendered`.
    //
    // Scope: consecutive non-failed rows -- punctuation, non-Hanzi and the end of the result are
    // boundaries. Only readings shaped like "zhong1" / "ong4" (TONE3 / FINALS_TONE3) are rewritten; under
    // other styles the tone cannot be recovered, so they are kept. Returns the number rewritten.
    //
    // ! MANDARIN RESULTS ONLY. Cantonese jyutping has the same "letters + trailing digit" shape, but the
    // 不/一 rules do not apply there. This takes a `Result` and not an `Engine` precisely because a
    // `Result` carries no dialect and cannot refuse one; an `Engine` overload would be a public-surface
    // addition (`tests/abi/expected_public_members.txt`).
    //
    // Differs from pypinyin's `contrib.tone_sandhi` on purpose: it applies the third-tone run rule only
    // when the run ends at a segment boundary (its defect), and "一 + numeral/一 -> first tone" follows
    // the documented rule it does not implement.
    CPP_PINYIN2_EXPORT size_t applyMandarinToneSandhi(Result &result, const ToneSandhiOptions &options = {});
}  // namespace cpp_pinyin::v2
