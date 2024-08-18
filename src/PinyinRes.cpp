#include <cpp-pinyin/PinyinRes.h>

namespace Pinyin
{
    // Convert PinyinResVector to std::vector<std::string>
    std::vector<std::string> PinyinResVector::toStdVector() const {
        std::vector<std::string> result;
        result.reserve(this->size());
        for (const auto &res : *this) {
            result.emplace_back(res.error ? res.hanzi : res.pinyin);
        }
        return result;
    }

    // Convert PinyinResVector to std::string with delimiter
    std::string PinyinResVector::toStdStr(const std::string &delimiter) const {
        std::string result;
        bool first = true;

        for (const auto &res : *this) {
            if (!first) {
                result += delimiter;
            }
            result += res.error ? res.hanzi : res.pinyin;
            first = false;
        }

        return result;
    }
}
