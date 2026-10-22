#include <algorithm>
#include <array>
#include <unordered_map>

#include <cpp-pinyin2/core/G2pglobal.h>
#include <cpp-pinyin2/dialects/ManTone.h>

#include "ManToneUtil.h"

namespace cpp_pinyin::v2
{
    // Local ASCII-letter test. cpp_pinyin::v2::isLetter() is an exported symbol (cpp-pinyin2/G2pglobal.h),
    // so calls to it from this translation unit cannot be inlined and cost one call per character
    // of every reading. Its definition is exactly the range test below (src/G2pglobal.cpp), so this
    // is equivalent for every possible char16_t.
    static inline bool isAsciiLetter(const char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

    // Pinyin initial -> Bopomofo initial, in the ORDER the reading splitter scans (zh/ch/sh before z/c/s, which is
    // what makes the two-letter initials win). This table is the ONE place that order is written: `bpmfInitials` and
    // `kPinyinInitials` below are both derived from it, because they used to be two hand-written copies of the same
    // 21 keys in one file -- a pair that could only ever drift apart (2026-10-21, plan B3/C4).
    struct InitialPair {
        const char16_t *pinyin;
        const char16_t *bopomofo;
    };
    static constexpr InitialPair kInitialTable[] = {
        {u"b", u"\u3105"},  {u"p", u"\u3106"},  {u"m", u"\u3107"}, {u"f", u"\u3108"}, {u"d", u"\u3109"},
        {u"t", u"\u310A"},  {u"n", u"\u310B"},  {u"l", u"\u310C"}, {u"g", u"\u310D"}, {u"k", u"\u310E"},
        {u"h", u"\u310F"},  {u"j", u"\u3110"},  {u"q", u"\u3111"}, {u"x", u"\u3112"}, {u"zh", u"\u3113"},
        {u"ch", u"\u3114"}, {u"sh", u"\u3115"}, {u"r", u"\u3116"}, {u"z", u"\u3117"}, {u"c", u"\u3118"},
        {u"s", u"\u3119"},
    };

    static constexpr size_t kInitialCount = sizeof(kInitialTable) / sizeof(kInitialTable[0]);

    // The map, built once from the table above.
    static const std::unordered_map<std::u16string, std::u16string> bpmfInitials = []
    {
        std::unordered_map<std::u16string, std::u16string> built;
        built.reserve(kInitialCount);
        for (const InitialPair &pair : kInitialTable) {
            built.emplace(std::u16string(pair.pinyin), std::u16string(pair.bopomofo));
        }
        return built;
    }();

    // The same initials as an ordered array for the splitter's prefix scan, taken straight from the table so the
    // scan order and the map cannot disagree.
    static constexpr std::array<const char16_t *, kInitialCount> kPinyinInitials = []
    {
        std::array<const char16_t *, kInitialCount> ordered{};
        for (size_t i = 0; i < kInitialCount; ++i) {
            ordered[i] = kInitialTable[i].pinyin;
        }
        return ordered;
    }();

    // Pinyin final -> Bopomofo final
    static const std::unordered_map<std::u16string, std::u16string> bpmfFinals = {
        {u"a", u"\u311A"},         {u"o", u"\u311B"},          {u"e", u"\u311C"},          {u"eh", u"\u311D"},
        {u"ai", u"\u311E"},        {u"ei", u"\u311F"},         {u"ao", u"\u3120"},         {u"ou", u"\u3121"},
        {u"an", u"\u3122"},        {u"en", u"\u3123"},         {u"ang", u"\u3124"},        {u"eng", u"\u3125"},
        {u"er", u"\u3126"},        {u"i", u"\u3127"},          {u"ia", u"\u3127\u311A"},   {u"io", u"\u3127\u311B"},
        {u"ie", u"\u3127\u311C"},  {u"iai", u"\u3127\u311E"},  {u"iao", u"\u3127\u3120"},  {u"iu", u"\u3127\u3121"},
        {u"ian", u"\u3127\u3122"}, {u"in", u"\u3127\u3123"},   {u"iang", u"\u3127\u3124"}, {u"ing", u"\u3127\u3125"},
        {u"u", u"\u3128"},         {u"ua", u"\u3128\u311A"},   {u"uo", u"\u3128\u311B"},   {u"uai", u"\u3128\u311E"},
        {u"ui", u"\u3128\u311F"},  {u"uan", u"\u3128\u3122"},  {u"un", u"\u3128\u3123"},   {u"uang", u"\u3128\u3124"},
        {u"ong", u"\u3128\u3125"}, {u"v", u"\u3129"},          {u"ve", u"\u3129\u311C"},   {u"van", u"\u3129\u3122"},
        {u"vn", u"\u3129\u3123"},  {u"iong", u"\u3129\u3125"},
    };

    // Bopomofo tone marks: 1=unmarked, 2=ˊ, 3=ˇ, 4=ˋ, 5=˙
    static const char16_t bpmfToneMarks[] = {0, 0, u'\u02CA', u'\u02C7', u'\u02CB', u'\u02D9'};

    static std::u16string pinyinToBopomofo(const std::u16string &tone3) {
        if (tone3.empty())
            return tone3;

        // Extract tone number from end
        int toneNum = 0;
        std::u16string syllable = tone3;
        if (!syllable.empty() && syllable.back() >= u'1' && syllable.back() <= u'5') {
            toneNum = syllable.back() - u'0';
            syllable.pop_back();
        }

        if (syllable.empty())
            return tone3;

        // Try to match initial (longest first: zh, ch, sh are 2 chars)
        std::u16string initial;
        std::u16string final_part = syllable;

        for (size_t len = 2; len >= 1; len--) {
            if (syllable.size() >= len) {
                auto prefix = syllable.substr(0, len);
                auto it = bpmfInitials.find(prefix);
                if (it != bpmfInitials.end()) {
                    initial = it->second;
                    final_part = syllable.substr(len);
                    break;
                }
            }
        }

        // Handle standalone finals (no initial): zhi->zh+i, chi->ch+i, shi->sh+i, ri->r+i,
        // zi->z+i, ci->c+i, si->s+i -- these have empty finals in bopomofo
        if (!initial.empty() && final_part == u"i") {
            // Check if this is zh/ch/sh/r/z/c/s + i (these use empty final)
            auto initKey = syllable.substr(0, syllable.size() - final_part.size());
            if (initKey == u"zh" || initKey == u"ch" || initKey == u"sh" || initKey == u"r" || initKey == u"z" ||
                initKey == u"c" || initKey == u"s") {
                final_part.clear();
            }
        }

        // Map final
        std::u16string bpmfFinal;
        if (!final_part.empty()) {
            auto it = bpmfFinals.find(final_part);
            if (it != bpmfFinals.end()) {
                bpmfFinal = it->second;
            } else {
                // Fallback: return original
                return tone3;
            }
        }

        // Build result: initial + final + tone mark
        std::u16string result = initial + bpmfFinal;
        if (toneNum >= 2 && toneNum <= 5) {
            result += bpmfToneMarks[toneNum];
        }
        // Tone 1 has no mark in bopomofo

        return result;
    }
    // Direct-indexed tone diacritic lookup.
    // Maps tone diacritic char16_t -> (base vowel, tone digit).
    // Covers U+00E0..U+01DC (253 entries). One outlier ḿ (U+1E3F) handled inline.
    struct ToneEntry {
        char16_t base;  // 0 = not a tone diacritic
        char16_t digit;
    };

    static constexpr char16_t TONE_TABLE_BASE = 0x00E0;
    static constexpr size_t TONE_TABLE_SIZE = 0x01DC - 0x00E0 + 1;  // 253

    static constexpr auto buildToneTable() {
        std::array<ToneEntry, TONE_TABLE_SIZE> t{};
        // à(E0) á(E1) è(E8) é(E9) ì(EC) í(ED) ò(F2) ó(F3) ù(F9) ú(FA)
        t[0xE0 - TONE_TABLE_BASE] = {u'a', u'4'};
        t[0xE1 - TONE_TABLE_BASE] = {u'a', u'2'};
        t[0xE8 - TONE_TABLE_BASE] = {u'e', u'4'};
        t[0xE9 - TONE_TABLE_BASE] = {u'e', u'2'};
        t[0xEC - TONE_TABLE_BASE] = {u'i', u'4'};
        t[0xED - TONE_TABLE_BASE] = {u'i', u'2'};
        t[0xF2 - TONE_TABLE_BASE] = {u'o', u'4'};
        t[0xF3 - TONE_TABLE_BASE] = {u'o', u'2'};
        t[0xF9 - TONE_TABLE_BASE] = {u'u', u'4'};
        t[0xFA - TONE_TABLE_BASE] = {u'u', u'2'};
        // ā(101) ē(113) ě(11B) ī(12B) ń(144) ň(148) ō(14D) ū(16B)
        t[0x101 - TONE_TABLE_BASE] = {u'a', u'1'};
        t[0x113 - TONE_TABLE_BASE] = {u'e', u'1'};
        t[0x11B - TONE_TABLE_BASE] = {u'e', u'3'};
        t[0x12B - TONE_TABLE_BASE] = {u'i', u'1'};
        t[0x144 - TONE_TABLE_BASE] = {u'n', u'2'};
        t[0x148 - TONE_TABLE_BASE] = {u'n', u'3'};
        t[0x14D - TONE_TABLE_BASE] = {u'o', u'1'};
        t[0x16B - TONE_TABLE_BASE] = {u'u', u'1'};
        // ǎ(1CE) ǐ(1D0) ǒ(1D2) ǔ(1D4) ǖ(1D6) ǘ(1D8) ǚ(1DA) ǜ(1DC)
        t[0x1CE - TONE_TABLE_BASE] = {u'a', u'3'};
        t[0x1D0 - TONE_TABLE_BASE] = {u'i', u'3'};
        t[0x1D2 - TONE_TABLE_BASE] = {u'o', u'3'};
        t[0x1D4 - TONE_TABLE_BASE] = {u'u', u'3'};
        t[0x1D6 - TONE_TABLE_BASE] = {u'v', u'1'};
        t[0x1D8 - TONE_TABLE_BASE] = {u'v', u'2'};
        t[0x1DA - TONE_TABLE_BASE] = {u'v', u'3'};
        t[0x1DC - TONE_TABLE_BASE] = {u'v', u'4'};
        // ǹ(1F9) is outside the table range, handled inline with ḿ(1E3F)
        return t;
    }

    static constexpr auto toneTable = buildToneTable();

    // Lookup tone diacritic -> (base, digit). Returns {0,0} if not a diacritic.
    static inline ToneEntry lookupTone(const char16_t ch) {
        // Fast path: most chars are ASCII letters, skip immediately
        if (ch < TONE_TABLE_BASE)
            return {0, 0};
        const auto idx = static_cast<size_t>(ch - TONE_TABLE_BASE);
        if (idx < TONE_TABLE_SIZE)
            return toneTable[idx];
        // Outliers beyond table range
        if (ch == 0x1F9)
            return {u'n', u'4'};  // ǹ
        if (ch == 0x1E3F)
            return {u'm', u'2'};  // ḿ
        return {0, 0};
    }

    // A toneless style does not use neutral_tone_with_five (a neutral tone has no mark to write), and
    // the parameter is left unnamed to say so.
    std::u16string ManTone::toneToNormal(const std::u16string &pinyin, const bool v_to_u,
                                         bool /*neutral_tone_with_five*/) {
        std::u16string result;
        result.reserve(pinyin.size());

        for (const char16_t &ch : pinyin) {
            if (isAsciiLetter(ch)) {
                result += ch;
            } else {
                const auto entry = lookupTone(ch);
                result += entry.base != 0 ? entry.base : ch;
            }
        }

        if (!v_to_u)
            std::replace(result.begin(), result.end(), u'ü', u'v');

        return result;
    }

    // A style that keeps the tone mark does not use neutral_tone_with_five (a neutral tone carries no
    // mark in the dictionary either).
    std::u16string ManTone::toneToTone(const std::u16string &pinyin, const bool v_to_u,
                                       bool /*neutral_tone_with_five*/) {
        if (v_to_u)
            return pinyin;

        std::u16string result;
        result.reserve(pinyin.size());

        for (const char16_t &ch : pinyin) {
            if (isAsciiLetter(ch)) {
                result += ch;
            } else {
                result += ch == u'ü' ? u'v' : ch;
            }
        }

        return result;
    }

    std::u16string ManTone::toneToFirstLetter(const std::u16string &pinyin, const bool v_to_u,
                                              bool neutral_tone_with_five) {
        const auto normal = toneToNormal(pinyin, v_to_u, neutral_tone_with_five);
        if (normal.empty())
            return normal;
        return std::u16string(1, normal[0]);
    }

    // The bopomofo style always uses the toned form; neither v_to_u nor neutral_tone_with_five takes
    // part (the latter's 5 is not used).
    std::u16string ManTone::toneToBopomofo(const std::u16string &pinyin, bool /*v_to_u*/,
                                           bool /*neutral_tone_with_five*/) {
        // First convert to tone3 format (e.g., "zhong1"), then map to bopomofo
        const auto tone3 = toneToTone3(pinyin, false, true);
        return pinyinToBopomofo(tone3);
    }

    // INITIALS / FINALS / FINALS_TONE3: ported from pypinyin 0.55.0 (style/_utils.py, style/_constants.py,
    // standard.py); 113 of the 115 reference syllables are byte-identical, the two exceptions being the ê
    // interjections the tables below have no entry for (no data/dialects file carries ê, so the frozen dump
    // is unaffected; the per-case evidence is tests/cpp/test_style_convert.cpp). Tables and
    // their order copied verbatim: that scan order is what makes zh/ch/sh win over z/c/s.
    // kPinyinInitials is DEFINED ABOVE, next to kInitialTable: that table is the one place the scan
    // order is written, and the array is derived from it.
    // pypinyin _INITIALS_NOT_STRICT: used only by the retry pass in splitFinals().
    static constexpr std::array<const char16_t *, 23> kPinyinInitialsLoose = {
        u"b", u"p", u"m",  u"f",  u"d",  u"t", u"n", u"l", u"g", u"k", u"h", u"j",
        u"q", u"x", u"zh", u"ch", u"sh", u"r", u"z", u"c", u"s", u"y", u"w",
    };
    // pypinyin _FINALS (37 entries, in file order).
    static const char16_t *const kPinyinFinals[] = {
        u"i",  u"u",   u"ü",    u"a",    u"ia",  u"ua",  u"o",    u"uo",  u"e",    u"ie",  u"üe", u"ai", u"uai",
        u"ei", u"uei", u"ao",   u"iao",  u"ou",  u"iou", u"an",   u"ian", u"uan",  u"üan", u"en", u"in", u"uen",
        u"ün", u"ang", u"iang", u"uang", u"eng", u"ing", u"ueng", u"ong", u"iong", u"er",  u"ê",
    };

    static inline size_t u16len(const char16_t *s) { return std::char_traits<char16_t>::length(s); }

    static inline bool startsWith(const std::u16string &s, const char16_t *prefix) {
        const size_t len = u16len(prefix);
        return s.size() >= len && s.compare(0, len, prefix, len) == 0;
    }

    // pypinyin _utils.get_initials(): the first table entry that prefixes the reading.
    template <size_t N>
    static std::u16string matchInitial(const std::u16string &reading, const std::array<const char16_t *, N> &table) {
        for (const char16_t *candidate : table)
            if (startsWith(reading, candidate))
                return candidate;
        return {};
    }

    static bool isKnownFinal(const std::u16string &finals) {
        for (const char16_t *candidate : kPinyinFinals)
            if (finals == candidate)
                return true;
        return false;
    }

    // "iu" -> "iou" and friends. pypinyin standard.convert_iou/uei/uen() use the regexes
    // ^([a-z]+)iu$ / ([a-z]+)ui$ / ([a-z]+)un$, i.e. a non-empty ASCII-letter prefix.
    static void restoreContraction(std::u16string &pinyin, const char16_t *suffix, const char16_t *replacement) {
        const size_t len = u16len(suffix);
        if (pinyin.size() <= len || pinyin.compare(pinyin.size() - len, len, suffix, len) != 0)
            return;
        for (size_t i = 0; i + len < pinyin.size(); ++i)
            if (!isAsciiLetter(pinyin[i]))
                return;
        pinyin.replace(pinyin.size() - len, len, replacement, u16len(replacement));
    }

    // pypinyin standard.convert_finals(): restore the canonical final before splitting it off.
    static std::u16string canonicalFinals(const std::u16string &toneless) {
        std::u16string converted = toneless;
        if (!converted.empty() && (converted[0] == u'y' || converted[0] == u'w')) {
            const std::u16string rest = converted.substr(1);
            const char16_t first = rest.empty() ? char16_t{0} : rest[0];
            if (converted[0] == u'y') {
                if (first == u'u' || first == u'ü')  // yu -> ü, yue -> üe, yuan -> üan, yun -> ün
                    converted = u"ü" + rest.substr(1);
                else if (first == u'i')  // yi -> i, yin -> in, ying -> ing
                    converted = rest;
                else  // ya -> ia, you -> iou, yang -> iang
                    converted = u"i" + rest;
            } else {
                if (first == u'u')  // wu -> u
                    converted = rest;
                else  // wa -> ua, wen -> uen, weng -> ueng
                    converted = u"u" + rest;
            }
            // pypinyin keeps the input when the rewrite is not a legal final ("yo" -> "yo").
            if (!isKnownFinal(converted))
                converted = toneless;
        }

        // ü after j/q/x: ju -> jü (pypinyin standard.convert_uv()).
        if (converted.size() >= 2 && (converted[0] == u'j' || converted[0] == u'q' || converted[0] == u'x') &&
            converted[1] == u'u')
            converted[1] = u'ü';

        restoreContraction(converted, u"iu", u"iou");
        restoreContraction(converted, u"ui", u"uei");
        restoreContraction(converted, u"un", u"uen");
        return converted;
    }

    // pypinyin _utils.get_finals(reading, strict=True) on a toneless, ü-spelled reading.
    static std::u16string splitFinals(const std::u16string &toneless) {
        const std::u16string converted = canonicalFinals(toneless);
        const std::u16string initial = matchInitial(converted, kPinyinInitials);
        std::u16string finals = converted.substr(initial.size());
        if (isKnownFinal(finals))
            return finals;
        // Retry with y/w counted as initials; this is what maps "yo" to "o".
        const std::u16string loose = matchInitial(converted, kPinyinInitialsLoose);
        finals = converted.substr(loose.size());
        return isKnownFinal(finals) ? finals : std::u16string();
    }

    std::u16string ManTone::toneToInitials(const std::u16string &pinyin, bool, bool) {
        // pypinyin runs this scan on the reading as given (not tone-stripped): an accented
        // letter such as ń (U+0144) therefore never matches the ASCII "n" -- reproduced here.
        return matchInitial(pinyin, kPinyinInitials);
    }

    std::u16string ManTone::toneToFinals(const std::u16string &pinyin, const bool v_to_u, bool) {
        std::u16string toneless;
        toneless.reserve(pinyin.size());
        for (const char16_t ch : pinyin) {
            const auto entry = lookupTone(ch);
            toneless += entry.base != 0 ? entry.base : ch;
        }
        // pypinyin: replace_symbol_to_no_symbol(pinyin).replace('v', 'ü')
        std::replace(toneless.begin(), toneless.end(), u'v', u'ü');

        std::u16string finals = splitFinals(toneless);
        if (finals.empty())
            return finals;
        if (!v_to_u)
            std::replace(finals.begin(), finals.end(), u'ü', u'v');
        return finals;
    }

    std::u16string ManTone::toneToFinals3(const std::u16string &pinyin, const bool v_to_u,
                                          const bool neutral_tone_with_five) {
        std::u16string finals = toneToFinals(pinyin, v_to_u, neutral_tone_with_five);
        if (finals.empty())
            return finals;

        char16_t digit = 0;
        for (const char16_t ch : pinyin) {
            const auto entry = lookupTone(ch);
            if (entry.base != 0) {
                digit = entry.digit;
                break;
            }
        }
        if (digit == 0) {
            if (!neutral_tone_with_five)
                return finals;
            digit = u'5';
        }
        finals += digit;
        return finals;
    }

    std::u16string ManTone::toneToTone2(const std::u16string &pinyin, const bool v_to_u,
                                        const bool neutral_tone_with_five) {
        std::u16string result;
        // No reserve(): a 3-5 character reading fits the string's small buffer (7 char16_t on
        // libstdc++, 8 on MSVC), so reserving was a no-op call on every conversion. Omitting a
        // reserve can only ever cost a reallocation, never change the result.

        for (const char16_t &ch : pinyin) {
            if (isAsciiLetter(ch)) {
                result += ch;
            } else {
                const auto entry = lookupTone(ch);
                if (entry.base != 0) {
                    result += entry.base;
                    if (!(!neutral_tone_with_five && entry.digit == u'5'))
                        result += entry.digit;
                } else {
                    if (!v_to_u && ch == u'ü') {
                        result += u'v';
                        continue;
                    }
                    result += ch;
                }
            }
        }
        return result;
    }


    std::u16string ManTone::toneToTone3(const std::u16string &pinyin, const bool v_to_u,
                                        const bool neutral_tone_with_five) {
        std::u16string result;
        // No reserve(): a 3-5 character reading fits the string's small buffer (7 char16_t on
        // libstdc++, 8 on MSVC), so reserving was a no-op call on every conversion. Omitting a
        // reserve can only ever cost a reallocation, never change the result.

        char16_t toneNumber = u'5';

        for (const char16_t &ch : pinyin) {
            if (isAsciiLetter(ch)) {
                result += ch;
            } else {
                const auto entry = lookupTone(ch);
                if (entry.base != 0) {
                    result += entry.base;
                    toneNumber = entry.digit;
                } else {
                    if (!v_to_u && ch == u'ü') {
                        result += u'v';
                        continue;
                    }
                    result += ch;
                }
            }
        }

        result += toneNumber;

        if (!neutral_tone_with_five && toneNumber == u'5')
            result.pop_back();
        return result;
    }

    // External readings are tone3 ("zhi4"); everything the engine stores and converts is
    // tone-marked ("zhì"), which is what tone3ToTone produces.
    //
    // This used to be hard-coded and shared by both dialects (EngineImpl passed tone3ToTone directly),
    // so a Cantonese user dictionary was rewritten by the Mandarin rules:
    // `jyut6` -> `jyu6t6` and `gwong2` -> `gwóng` (measured). The fix registers the hook per dialect
    // into ToneConverter::m_normalize -- Cantonese overrides nothing and so is the identity map.
    std::u16string ManTone::normalizeReading(const std::u16string &pinyin) { return tone3ToTone(pinyin); }
}  // namespace cpp_pinyin::v2
