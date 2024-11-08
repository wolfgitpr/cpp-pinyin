#ifndef U16STR_H
#define U16STR_H

#include <string>
#include <vector>
#include <unordered_map>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::string CPP_PINYIN_EXPORT u16strToUtf8str(const std::u16string &u16str);
    std::u16string CPP_PINYIN_EXPORT utf8strToU16str(const std::string &utf8str);

    class CPP_PINYIN_EXPORT u16str : public std::u16string {
    public:
        using std::u16string::u16string;

        u16str(const std::u16string &other) :
            std::u16string(other) {}

        u16str(std::u16string &&other) noexcept :
            std::u16string(std::move(other)) {}

        inline std::string encodeUtf8() const {
            return Pinyin::u16strToUtf8str(*this);
        }
    };

    using u16strVec = std::vector<u16str>;

    // overload std::u16string hash function
    struct u16str_hash {
        std::size_t operator()(const char16_t &key) const {
            return std::hash<char16_t>()(key);
        }

        std::size_t operator()(const u16str &key) const {
            return std::hash<std::u16string>()(key);
        }

        std::size_t operator()(const u16strVec &vec) const {
            std::size_t seed = vec.size();
            for (auto &i : vec)
                seed ^= std::hash<std::u16string>()(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    // extend u16strHashMap
    template <typename T, typename T1>
    using u16strHashMap = std::unordered_map<T, T1, u16str_hash>;
}
#endif //U16STR_H
