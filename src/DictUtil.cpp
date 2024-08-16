#include "DictUtil.h"

#include <iostream>
#include <fstream>
#include <sstream>

#include "StringUtil.h"

namespace Pinyin
{
    static std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
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

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  u32strHashMap<u32str, u32str> &resultMap, const char &sep1) {
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
                resultMap[utf8strToU32str(key)] = utf8strToU32str(value);
            }
        }
        return true;
    }

    bool loadDict(const std::string &dict_dir, const std::string &fileName,
                  u32strHashMap<u32str, u32strVec> &resultMap, const char &sep1,
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
                u32strVec u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(utf8strToU32str(str));
                }
                resultMap[utf8strToU32str(key)] = u8strlist;
            }
        }
        return true;
    }

    bool loadAdditionalDict(const std::string &dict_dir, const std::string &fileName,
                            u32strHashMap<u32str, u32strVec> &resultMap, const char &sep1,
                            const std::string &sep2,
                            const std::function<u32str(const u32str &pinyin)> &converterForDefaultPinyin) {
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
                u32strVec u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(converterForDefaultPinyin(utf8strToU32str(str)));
                }
                resultMap[utf8strToU32str(key)] = u8strlist;
            }
        }
        return true;
    }
} // Pinyin
