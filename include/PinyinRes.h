#ifndef G2PRES_H
#define G2PRES_H

#include <string>

namespace Pinyin
{
    struct PinyinRes
    {
        std::string hanzi;
        std::string pinyin;
        std::vector<std::string> candidates;    //  Candidate pinyin of Polyphonic Characters.
        bool error = true;                      //  Whether the conversion failed.
    };

    class PinyinResVector : public std::vector<PinyinRes>
    {
    public:
        // Convert PinyinResVector to std::vector<std::string>
        inline std::vector<std::string> toStdVector() const
        {
            std::vector<std::string> result;
            result.reserve(this->size());
            for (const auto& res : *this)
            {
                result.emplace_back(res.error ? res.hanzi : res.pinyin);
            }
            return result;
        }

        // Convert PinyinResVector to std::string with delimiter
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
                result += res.error ? res.hanzi : res.pinyin;
                first = false;
            }

            return result;
        }
    };
}
#endif //G2PRES_H
