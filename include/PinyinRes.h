#ifndef G2PRES_H
#define G2PRES_H

#include <string>

namespace Pinyin
{
    struct PinyinRes
    {
        std::string lyric;
        std::string pinyin;
        std::vector<std::string> candidates;
        bool error = true;
    };

    class PinyinResVector : public std::vector<PinyinRes>
    {
    public:
        inline std::vector<std::string> toStdVector() const
        {
            std::vector<std::string> result;
            result.reserve(this->size());
            for (const auto& res : *this)
            {
                result.emplace_back(res.error ? res.lyric : res.pinyin);
            }
            return result;
        }

        inline std::string toStdStr(const std::string& delimiter = " ") const
        {
            std::string result;
            bool first = true;

            for (const auto& res : *this)
            {
                if (!first)
                {
                    result += delimiter;
                }
                result += res.error ? res.lyric : res.pinyin;
                first = false;
            }

            return result;
        }
    };
}
#endif //G2PRES_H
