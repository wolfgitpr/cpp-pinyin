#ifndef G2PRES_H
#define G2PRES_H

#include <string>
#include <vector>

#include <cpp-pinyin/PinyinGlobal.h>

namespace Pinyin
{
    struct CPP_PINYIN_EXPORT PinyinRes {
        std::string hanzi;
        std::string pinyin;
        std::vector<std::string> candidates; //  Candidate pinyin of Polyphonic Characters.
        bool error = true; //  Whether the conversion failed.
    };

    class CPP_PINYIN_EXPORT PinyinResVector : public std::vector<PinyinRes> {
    public:
        // Convert PinyinResVector to std::vector<std::string>
        std::vector<std::string> toStdVector() const;

        // Convert PinyinResVector to std::string with delimiter
        std::string toStdStr(const std::string &delimiter = " ") const;
    };
}
#endif //G2PRES_H
