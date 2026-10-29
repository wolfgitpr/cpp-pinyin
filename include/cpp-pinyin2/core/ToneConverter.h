#pragma once

#include <array>

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <string>

namespace cpp_pinyin::v2
{
    class CPP_PINYIN2_EXPORT ToneConverter {
    public:
        // A `Style` enum (NORMAL / TONE) used to live here. Nothing in or out of this repository
        // named it, and it was a second, smaller copy of the per-dialect numbering that Types.h owns
        // -- deleted 2026-10-01; the style enums collapsed into Types.h, and no decision number survives for it.
        using ConvertFunc = std::u16string(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_five);

        /// Signature of the hook that turns a reading taken from an external dictionary
        /// (``user_dict.txt``, a custom dialect's own tables) into the form this dialect stores
        /// internally. Deliberately a function pointer and not a virtual: the engine keeps an
        /// *object* copy of the converter, and a value copy slices to this base subobject, so a
        /// virtual would silently fall back to the base implementation instead of the dialect's.
        using NormalizeFunc = std::u16string(const std::u16string &pinyin);

        static constexpr int MAX_STYLE = 9;  // styles 0..8

        ToneConverter() : m_normalize(&identityNormalize) { m_converts.fill(nullptr); }
        virtual ~ToneConverter() {}

        /// Converts one internal-form reading into `style`.
        ///
        /// FAILURE MODE (it has none, and that is the contract): `m_converts` is a 0..`MAX_STYLE` table of
        /// function pointers a dialect fills for the styles it supports, and an unset slot returns `str`
        /// UNCHANGED -- so a caller asking for a style the dialect does not declare gets their own string
        /// back, not an empty string and not a throw. `CanTone` relies on this: it registers only styles 2
        /// and 3 while `CantoneseStyle::TONE3` (8) exists as a value, so asking Cantonese for TONE3 is "no
        /// conversion", not a failure. `style` outside `[0, MAX_STYLE]` is likewise returned unchanged.
        std::u16string convert(std::u16string str, int style, bool v_to_u = false,
                               bool neutral_tone_with_five = false) const;

        /// Normalizes one external reading. The base implementation is the identity map, which is
        /// correct for a dialect whose dictionaries already use the internal form (Cantonese
        /// jyutping keeps its tone digits, so CanTone needs no override). Mandarin overrides it to
        /// turn ``"zhi4"`` into ``"zhì"``.
        [[nodiscard]] std::u16string normalizeReading(const std::u16string &pinyin) const {
            return m_normalize(pinyin);
        }

    protected:
        std::array<ConvertFunc *, MAX_STYLE> m_converts;
        NormalizeFunc *m_normalize;

    private:
        static std::u16string identityNormalize(const std::u16string &pinyin) { return pinyin; }
    };
}  // namespace cpp_pinyin::v2
