#include "DictUtil.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>

#include "cpp-pinyin/U16Str.h"

namespace Pinyin
{
    // Helper function to read and open file
    static std::ifstream openFile(const std::filesystem::path &dict_dir) {
#ifdef _WIN32
        const std::wstring wdict_dir = dict_dir.wstring();
        return std::ifstream(wdict_dir.c_str());
#else
        return std::ifstream(dict_dir.c_str());
#endif
    }

    // Helper function to trim whitespace from a string
    static void trim(std::string &str) {
        str.erase(0, str.find_first_not_of(" \t\r\n"));
        str.erase(str.find_last_not_of(" \t\r\n") + 1);
    }

    // Common function for reading lines and processing key-value pairs
    template <typename K, typename V, typename KeyFunc, typename ValueFunc>
    static bool processFile(std::ifstream &file, std::unordered_map<K, V> &resultMap,
                            const char &sep1, KeyFunc keyProcessor, ValueFunc valueProcessor) {
        if (!file.is_open()) {
            std::cerr << "Error: Unable to open file" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            trim(line);
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, sep1) && std::getline(iss, value)) {
                resultMap[keyProcessor(key)] = valueProcessor(value);
            }
        }
        return true;
    }

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

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<std::u16string, std::u16string> &resultMap, const char &sep1) {
        std::ifstream file = openFile(dict_dir);
        return processFile(file, resultMap, sep1,
                           [](const std::string &key) { return utf8strToU16str(key); },
                           [](const std::string &value) { return utf8strToU16str(value); });
    }

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<std::u16string, std::vector<std::u16string>> &resultMap, const char &sep1,
                  const std::string &sep2) {
        std::ifstream file = openFile(dict_dir);
        return processFile(file, resultMap, sep1,
                           [](const std::string &key) { return utf8strToU16str(key); },
                           [&sep2](const std::string &value)
                           {
                               std::vector<std::u16string> u8strlist;
                               for (const auto &str : split(value, sep2)) {
                                   if (!str.empty())
                                       u8strlist.emplace_back(utf8strToU16str(str));
                               }
                               return u8strlist;
                           });
    }

    bool loadAdditionalDict(const std::filesystem::path &dict_dir,
                            std::unordered_map<std::u16string, std::vector<std::u16string>> &resultMap,
                            const char &sep1,
                            const std::string &sep2,
                            const std::function<std::u16string(const std::u16string &pinyin)> &
                            converterForDefaultPinyin) {
        std::ifstream file = openFile(dict_dir);
        return processFile(file, resultMap, sep1,
                           [](const std::string &key) { return utf8strToU16str(key); },
                           [&sep2, &converterForDefaultPinyin](const std::string &value)
                           {
                               std::vector<std::u16string> u8strlist;
                               for (const auto &str : split(value, sep2)) {
                                   if (!str.empty())
                                       u8strlist.emplace_back(converterForDefaultPinyin(utf8strToU16str(str)));
                               }
                               return u8strlist;
                           });
    }
} // namespace Pinyin
