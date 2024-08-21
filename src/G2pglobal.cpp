#include <cpp-pinyin/G2pglobal.h>

#include <memory>
#include <unordered_set>

namespace Pinyin
{
    class G2pGlobal {
    public:
        std::string path;
    };

    auto m_global = std::make_unique<G2pGlobal>();

    std::string dictionaryPath() {
        return m_global->path;
    }

    void setDictionaryPath(const std::string &dir) {
        m_global->path = dir;
    }

    bool isLetter(char32_t c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    bool isHanzi(char32_t c) {
        return c >= 0x4e00 && c <= 0x9fa5;
    }

    bool isKana(char32_t c) {
        return (c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF);
    }

    bool isDigit(char32_t c) {
        return c >= '0' && c <= '9';
    }

    bool isSpace(char32_t c) {
        return c == ' ';
    }

    bool isSpecialKana(char32_t c) {
        static const std::unordered_set<char32_t> specialKana = {
            U'ャ', U'ュ', U'ョ', U'ゃ', U'ゅ', U'ょ',
            U'ァ', U'ィ', U'ゥ', U'ェ', U'ォ', U'ぁ', U'ぃ', U'ぅ', U'ぇ', U'ぉ'
        };
        return specialKana.find(c) != specialKana.end();
    }
}
