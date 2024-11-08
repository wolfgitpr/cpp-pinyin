#include "Common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>

namespace Test
{
    std::vector<std::string> readData(const std::filesystem::path &filepath) {
        std::vector<std::string> dataLines;
        dataLines.reserve(1000); // 提前预留空间，避免多次分配

#ifdef _WIN32
        // Convert the UTF-8 string to a wide string
        const std::wstring wdict_dir = filepath.wstring();
        std::ifstream file(wdict_dir);
#else
        std::ifstream file(filepath);
#endif

        if (file.is_open()) {
            std::string line;
            line.reserve(256);

            while (std::getline(file, line)) {
                dataLines.push_back(std::move(line));
            }
            file.close();
        } else {
            std::cerr << "Unable to open file: " << filepath << std::endl;
        }

        return dataLines;
    }

    std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
        std::vector<std::string> tokens;
        tokens.reserve(32);

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
}
