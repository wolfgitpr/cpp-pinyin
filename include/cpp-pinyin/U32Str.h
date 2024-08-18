#ifndef U32STR_H
#define U32STR_H

#include <string>
#include <unordered_map>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    std::string CPP_PINYIN_EXPORT u32strToUtf8str(const std::u32string &u32str);
    std::u32string CPP_PINYIN_EXPORT utf8strToU32str(const std::string &utf8str);

    class CPP_PINYIN_EXPORT u32str : public std::u32string {
    public:
        using std::u32string::u32string;

        u32str(const std::u32string &other) :
            std::u32string(other) {}

        u32str(std::u32string &&other) noexcept :
            std::u32string(std::move(other)) {}

        inline std::string encodeUtf8() const {
            return Pinyin::u32strToUtf8str(*this);
        }
    };

    using u32strVec = std::vector<u32str>;

    // overload std::u32string hash function
    struct u32str_hash {
        std::size_t operator()(const char32_t &key) const {
            return std::hash<char32_t>()(key);
        }

        std::size_t operator()(const u32str &key) const {
            return std::hash<std::u32string>()(key);
        }

        std::size_t operator()(const u32strVec &vec) const {
            std::size_t seed = vec.size();
            for (auto &i : vec)
                seed ^= std::hash<std::u32string>()(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    // extend u32strHashMap
    template <typename T, typename T1>
    using u32strHashMap = std::unordered_map<T, T1, u32str_hash>;
}
#endif //U32STR_H
