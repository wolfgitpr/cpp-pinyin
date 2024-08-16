#include "Common.h"

#include <codecvt>
#include <iostream>
#include <fstream>

#include "../src/StringUtil.h"

namespace Test
{
    std::vector<std::string> readData(const std::string &filename) {
        std::vector<std::string> dataLines;

#ifdef _WIN32
        // Convert the UTF-8 string to a wide string
        std::ifstream file(Pinyin::utf8ToWide(filename));
#else
        std::ifstream file(filename);
#endif

        std::string line;

        if (file.is_open()) {
            while (std::getline(file, line)) {
                dataLines.push_back(line);
            }
            file.close();
        } else {
            std::cerr << "Unable to open file: " << filename << std::endl;
        }

        return dataLines;
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
}
