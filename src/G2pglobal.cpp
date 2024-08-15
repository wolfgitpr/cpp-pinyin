#include "G2pglobal.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "StringUtil.h"
#include "../src/util/ManToneUtil.h"

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

    std::vector<std::string> toStdList(const u8stringlist &input) {
        std::vector<std::string> result;
        result.reserve(input.size());
        for (const auto &item : input) {
            result.emplace_back(item.c_str());
        }
        return result;
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

    std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
        std::vector<std::string> tokens;
        if (delimiter.empty()) {
            for (char c : s) {
                tokens.emplace_back(1, c);
            }
        } else {
            std::string::size_type start = 0;
            std::string::size_type end = s.find(delimiter);
            while (end != std::string::npos) {
                tokens.push_back(s.substr(start, end - start));
                start = end + delimiter.size();
                end = s.find(delimiter, start);
            }
            tokens.push_back(s.substr(start));
        }
        return tokens;
    }

    std::string join(const std::vector<std::string> &v, const std::string &delimiter) {
        if (v.empty())
            return {};

        std::string res;
        for (int i = 0; i < v.size() - 1; ++i) {
            res.append(v[i]);
            res.append(delimiter);
        }
        res.append(v.back());
        return res;
    }

    u8stringlist splitString(const u8string &input) {
        u8stringlist res;
        int pos = 0;
        while (pos < input.length()) {
            const auto currentChar = input[pos];
            if (isLetter(currentChar)) {
                const int start = pos;
                while (pos < input.length() && isLetter(input[pos])) {
                    pos++;
                }
                res.push_back(input.substr(start, pos - start));
            } else if (isHanzi(currentChar) || isDigit(currentChar) || !isSpace(currentChar)) {
                res.push_back(input.substr(pos, 1));
                pos++;
            } else if (isKana(currentChar)) {
                const int length = (pos + 1 < input.length() && isSpecialKana(input[pos + 1])) ? 2 : 1;
                res.push_back(input.substr(pos, length));
                pos += length;
            } else {
                pos++;
            }
        }
        return res;
    }

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  std::unordered_map<std::string, std::vector<std::string>> &resultMap, const char &sep1,
                  const std::string &sep2) {
#ifdef _WIN32
        std::ifstream file(Pinyin::utf8ToWide(dict_dir + "/" + fileName));
#else
        std::ifstream file(dict_dir + "/" + fileName);
#endif
        if (!file.is_open()) {
            std::cout << fileName << " error: Unable to open file" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, sep1) && std::getline(iss, value)) {
                std::vector<std::string> strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        strlist.emplace_back(str);
                }
                resultMap[key] = strlist;
            }
        }
        return true;
    }

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  std::unordered_map<u8string, u8string> &resultMap, const char &sep1,
                  const std::string &sep2) {
#ifdef _WIN32
        std::ifstream file(Pinyin::utf8ToWide(dict_dir + "/" + fileName));
#else
        std::ifstream file(dict_dir + "/" + fileName);
#endif
        if (!file.is_open()) {
            std::cout << fileName << " error: Unable to open file" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, sep1) && std::getline(iss, value)) {
                resultMap[key] = value;
            }
        }
        return true;
    }

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  std::unordered_map<u8string, u8stringlist> &resultMap, const char &sep1,
                  const std::string &sep2) {
#ifdef _WIN32
        std::ifstream file(Pinyin::utf8ToWide(dict_dir + "/" + fileName));
#else
        std::ifstream file(dict_dir + "/" + fileName);
#endif
        if (!file.is_open()) {
            std::cerr << fileName << " error: Unable to open file" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, sep1) && std::getline(iss, value)) {
                u8stringlist u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(str);
                }
                resultMap[key] = u8strlist;
            }
        }
        return true;
    }

    bool loadUserDict(const std::string &dict_dir, const std::string &fileName,
                      std::unordered_map<u8string, u8stringlist> &resultMap, const char &sep1,
                      const std::string &sep2) {
#ifdef _WIN32
        std::ifstream file(Pinyin::utf8ToWide(dict_dir + "/" + fileName));
#else
        std::ifstream file(dict_dir + "/" + fileName);
#endif
        if (!file.is_open()) {
            std::cerr << fileName << " error: Unable to open file" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, sep1) && std::getline(iss, value)) {
                u8stringlist u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(tone3ToTone(str));
                }
                resultMap[key] = u8strlist;
            }
        }
        return true;
    }
}
