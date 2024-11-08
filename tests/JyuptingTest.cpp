#include "JyuptingTest.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "Common.h"

namespace Test
{
    JyuptingTest::JyuptingTest() :
        g2p_can(std::make_unique<Pinyin::Jyutping>()) {}

    JyuptingTest::~JyuptingTest() = default;

    bool JyuptingTest::unToneBatchTest(const bool &resDisplay) const {
        size_t count = 0;
        size_t error = 0;

        const auto dataLines = readData("testData/jyutping_test.txt");

        const auto start = std::chrono::high_resolution_clock::now();

        for (const auto &line : dataLines) {
            const auto keyValuePair = split(line, "|");

            if (keyValuePair.size() == 2) {
                const std::string &hans = keyValuePair[0];
                const std::string &pinyin = keyValuePair[1];

                const auto result = g2p_can->hanziToPinyin(hans, Pinyin::CanTone::Style::NORMAL, Pinyin::Error::Default,
                                                           false).toStdStr();

                auto words = split(pinyin, " ");
                const auto wordSize = words.size();
                count += wordSize;

                bool diff = false;
                auto resWords = split(result, " ");

                for (int i = 0; i < wordSize; i++) {
                    const auto expectedWords = split(words[i], "/");
                    if (std::find(expectedWords.begin(), expectedWords.end(), resWords[i]) == expectedWords.end()) {
                        diff = true;
                        error++;
                    }
                }

                if (resDisplay && diff) {
                    std::cout << "text: " << hans << std::endl;
                    std::cout << "raw: " << pinyin << std::endl;
                    std::cout << "res: " << result << std::endl;
                }
            } else {
                std::cerr << "Invalid line format: " << line << std::endl;
            }
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        const double percentage = (static_cast<double>(error) / static_cast<double>(count)) * 100.0;

        std::cout << "unToneBatchTest: success" << std::endl;
        std::cout << "unToneBatchTest time: " << duration << "ms" << std::endl;
        std::cout << "错误率: " << percentage << "%" << std::endl;
        std::cout << "错误数: " << error << std::endl;
        std::cout << "总字数: " << count << std::endl;
        return true;
    }
} // Test
