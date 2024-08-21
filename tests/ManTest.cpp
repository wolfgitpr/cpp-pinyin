#include "ManTest.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "Common.h"

namespace Test
{
    ManTest::ManTest() :
        g2p_zh(std::make_unique<Pinyin::Pinyin>()) {}

    ManTest::~ManTest() = default;

    bool ManTest::apiTest() const {
        if (!g2p_zh->isPolyphonic("的"))
            std::cerr << "Polyphonic test failed." << std::endl;
        if (g2p_zh->isPolyphonic("犬"))
            std::cerr << "Polyphonic test failed." << std::endl;
        if (g2p_zh->tradToSim("臺") != "台")
            std::cerr << "Traditional to Simplified test failed." << std::endl;
        if (g2p_zh->tradToSim("犬") != "犬")
            std::cerr << "Traditional to Simplified test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::NORMAL).at(0) != "xiu")
            std::cerr << "Pinyin Style::NORMAL test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE).at(0) != "xiù")
            std::cerr << "Pinyin Style::TONE test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE2).at(0) != "xiu4")
            std::cerr << "Pinyin Style::TONE2 test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("擎", Pinyin::ManTone::Style::TONE2).at(0) != "qi2ng")
            std::cerr << "Pinyin Style::TONE2 test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE3).at(0) != "xiu4")
            std::cerr << "Pinyin Style::TONE3 test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("虐", Pinyin::ManTone::Style::TONE3, true).at(0) != "nüe4")
            std::cerr << "V-to-U test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("虐", Pinyin::ManTone::Style::TONE3, false).at(0) != "nve4")
            std::cerr << "V-to-U test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("了", Pinyin::ManTone::Style::TONE3, false, false).at(0) != "le")
            std::cerr << "Neutral tone test failed." << std::endl;
        if (g2p_zh->getDefaultPinyin("了", Pinyin::ManTone::Style::TONE3, false, true).at(0) != "le5")
            std::cerr << "Neutral tone test failed." << std::endl;
        return true;
    }

    bool ManTest::toneBatchTest(bool resDisplay) const {
        size_t count = 0;
        size_t error = 0;

        const auto dataLines = readData("testData/cpp_dataset.txt");

        const auto start = std::chrono::high_resolution_clock::now();
        for (const auto &line : dataLines) {
            const auto keyValuePair = split(line, "▁");

            if (keyValuePair.size() == 3) {
                const std::string &hans = keyValuePair[0];
                const int loc = std::stoi(keyValuePair[1]);
                const std::string &pinyin = keyValuePair[2];

                auto resWords = g2p_zh->hanziToPinyin(hans, Pinyin::ManTone::Style::TONE3, Pinyin::Error::Default,
                                                      false, true).
                                        toStdVector();
                count++;

                bool diff = false;
                if (resWords[loc] != pinyin) {
                    diff = true;
                    error++;
                }

                if (resDisplay && diff) {
                    std::cout << "text: " << hans << std::endl;
                    std::cout << "raw: " << pinyin << std::endl;
                    std::cout << "loc: " << loc << std::endl;
                    std::cout << "res: " << resWords[loc] << std::endl;
                }
            } else {
                std::cerr << "Invalid line format: " << line << std::endl;
            }
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        const double percentage = (static_cast<double>(error) / static_cast<double>(count)) * 100.0;

        std::cout << "toneBatchTest: success" << std::endl;
        std::cout << "toneBatchTest time: " << duration << "ms" << std::endl;
        std::cout << "错误率: " << percentage << "%" << std::endl;
        std::cout << "错误数: " << error << std::endl;
        std::cout << "总句数: " << count << std::endl;
        return true;
    }

    bool ManTest::unToneBatchTest(bool resDisplay) const {
        size_t count = 0;
        size_t error = 0;

        const auto dataLines = readData("testData/op_lab.txt");

        const auto start = std::chrono::high_resolution_clock::now();
        for (const auto &line : dataLines) {
            const auto keyValuePair = split(line, "|");

            if (keyValuePair.size() == 2) {
                const std::string &hans = keyValuePair[0];
                const std::string &pinyin = keyValuePair[1];

                auto result = g2p_zh->hanziToPinyin(hans, Pinyin::ManTone::Style::NORMAL, Pinyin::Error::Default, false,
                                                    true).
                                      toStdStr();

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
}
