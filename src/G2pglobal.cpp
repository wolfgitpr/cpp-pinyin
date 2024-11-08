#include <cpp-pinyin/G2pglobal.h>

#include <memory>
#include <unordered_set>

namespace Pinyin
{
    class G2pGlobal {
    public:
        std::filesystem::path path;
    };

    auto m_global = std::make_unique<G2pGlobal>();

    std::filesystem::path dictionaryPath() {
        return m_global->path;
    }

    void setDictionaryPath(const std::filesystem::path &dir) {
        m_global->path = dir;
    }

    bool isLetter(const char16_t &c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    bool isHanzi(const char16_t &c) {
        return c >= 0x4e00 && c <= 0x9fa5;
    }

    bool isKana(const char16_t &c) {
        return (c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF);
    }

    bool isDigit(const char16_t &c) {
        return c >= '0' && c <= '9';
    }

    bool isSpace(const char16_t &c) {
        return c == ' ';
    }

    bool isSpecialKana(const char16_t &c) {
        static const std::unordered_set<char16_t> specialKana = {
            u'ャ', u'ュ', u'ョ', u'ゃ', u'ゅ', u'ょ',
            u'ァ', u'ィ', u'ゥ', u'ェ', u'ォ', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ'
        };
        return specialKana.find(c) != specialKana.end();
    }
}
