#pragma once

#include <string>

#include <cpp-pinyin2/core/PinyinGlobal.h>

// The 2.0 face's structured reading ("data, not rendering").
// WHY THIS IS `cpp_pinyin` AND NOT `Pinyin`: the beta face still declared its own `Pinyin::Syllable`
// (`core/Types.h`) -- the element of a *rendered* result -- and declaring this one alongside it would not
// have added a second Syllable, it would REDEFINE that type. That element type is now
// `cpp_pinyin::v2::Segment` (`core/Types.h`), so the two names no longer collide; the namespace stays as
// the API plan fixed it (authority: docs/CONTRACT.md section 1; entry-type spelling: `cpp_pinyin::v2::Engine`).

namespace cpp_pinyin::v2
{
    // Tone provenance
    enum class ToneKind {
        // The table entry was read as-is; nothing about it was decided while rendering.
        unspecified = 0,
        // The base (citation) tone, i.e. the reading the character table carries.
        base = 1,
        // Reserved for a reading that a tone-change rule produced. No data in this repository is
        // annotated that way today, so nothing produces this value yet.
        sandhi = 2
    };

    // Which table the reading was read from: the four sources the engine actually consults, in priority
    // order; they are NOT a listing of render styles. KNOWN GAP, stated so a caller does not have to
    // discover it: only the first two have a producer today -- the phrase layers (`phrases_dict.txt` + the
    // user dictionary) are merged into one table before anything can tell which layer a row came from
    // (`src/dict/PhraseLayers.cpp`), so a user-dictionary reading is reported as `phrase`; `user_dict` and
    // `plugin` are therefore never produced -- not "not yet observed", but not implemented.
    enum class ReadingSource { dictionary = 0, phrase = 1, user_dict = 2, plugin = 3 };

    /// THE CONTRACT, stated in full here and nowhere else (authoritative wording, do not reword;
    /// `docs/CONTRACT.md` §3 indexes these four rules and names this block as the normative text;
    /// tests/cpp/test_syllable_contract.cpp fails if one of the four sentences goes away):
    /// 1. 「tone
    /// 的**唯一权威来源是词表原始注音**（粤语末位数字、普通话调号；无调号＝轻声）；**禁止**从转换后的输出字符串解析；**NORMAL
    /// 风格下也必须填**（NORMAL 去调只影响渲染，不影响结构化 `tone`）。」
    /// 2. 「`tone_kind` 描述**读到的表项本身**，不是渲染过程；数据无法标注變調时**允许全 `unspecified`**，且 **API
    /// 不得据此改变任何输出字节**；`sandhi` 值保留备用。」
    /// 3. 「普通话轻声的两种编码（`de` 无调号 / `de5`）＝**同一表层**，输出一律报实际编号：`markNeutralAs5 = false` 报
    /// `0`（或省略）、`= true` 报 `5`；**不得**让两条来源走不同路径。」
    /// 4. 「粤语**入声不新增相位**：1/3/6 调即含 `-p/-t/-k` 韵尾；方言表层编号**直用数据 1..6**，不得引入 7/8/9。」
    /// `tone` is a dialect-internal tone-category number, NOT a cross-dialect comparable value (Mandarin
    /// 1..4, Cantonese 1..6): never compare, order or average it across dialects; the authority is the
    /// dialect's own surface.
    struct CPP_PINYIN2_EXPORT Syllable {
        // The initial; "" for a zero-initial syllable ("阿", "衣", "知" follow the same rule as the beta
        // face).
        std::string initial;
        // The canonical final: tone stripped, ü written as v. The tone number travels in `tone` and is
        // never read back out of this field.
        std::string fin;
        // The row's ORIGINAL spelling in the dictionary (UTF-8, byte for byte, tone marks and trailing
        // digits included) -- the IDENTITY of this struct. `initial`/`fin` are a LOSSY PROJECTION of it
        // (the seven readings whose projection collides are listed in `core/Types.h`), so a consumer that
        // has to render again must use this field rather than infer it. Empty for an empty reading.
        std::string spelling;
        // The dialect-internal tone category. Its authority and its value range are contract clauses 1, 3
        // and 4 above.
        int tone = 0;
        // The annotation category of the reading inside its own table entry; it is not produced by
        // rendering and takes no part in it.
        ToneKind tone_kind = ToneKind::unspecified;
        // Which table layer the reading was read from.
        ReadingSource source = ReadingSource::dictionary;
    };
}  // namespace cpp_pinyin::v2
