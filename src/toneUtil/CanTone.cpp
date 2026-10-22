#include <cpp-pinyin2/dialects/CanTone.h>

namespace cpp_pinyin::v2
{
    // Cantonese's "tone removal" is just dropping the trailing digit: no ü notation and no
    // neutral-tone 5 to append.
    std::u16string CanTone::tone3ToNormal(const std::u16string &pinyin, bool /*v_to_u*/,
                                          bool /*neutral_tone_with_five*/) {
        if (pinyin.empty())
            return pinyin;
        return {pinyin.begin(), pinyin.end() - 1};
    }

    std::u16string CanTone::tone3ToFirstLetter(const std::u16string &pinyin, bool v_to_u, bool neutral_tone_with_five) {
        const auto normal = tone3ToNormal(pinyin, v_to_u, neutral_tone_with_five);
        if (normal.empty())
            return normal;
        return std::u16string(1, normal[0]);
    }
}  // namespace cpp_pinyin::v2
