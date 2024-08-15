#include "ManTest.h"

#include <assert.h>
#include <chrono>
#include <iostream>

#include "Common.h"

namespace Test
{
    ManTest::ManTest() :
        g2p_zh(std::make_unique<Pinyin::Pinyin>()) {}

    ManTest::~ManTest() = default;

    bool ManTest::apiTest() const {
        assert(g2p_zh->isPolyphonic("的"));
        assert(!g2p_zh->isPolyphonic("犬"));
        assert(g2p_zh->tradToSim("臺") == "台");
        assert(g2p_zh->tradToSim("犬") == "犬");
        assert(g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::NORMAL).at(0) == "xiu");
        assert(g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE).at(0) == "xiù");
        assert(g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE2).at(0) == "xiu4");
        assert(g2p_zh->getDefaultPinyin("擎", Pinyin::ManTone::Style::TONE2).at(0) == "qi2ng");
        assert(g2p_zh->getDefaultPinyin("秀", Pinyin::ManTone::Style::TONE3).at(0) == "xiu4");
        assert(g2p_zh->getDefaultPinyin("虐", Pinyin::ManTone::Style::TONE3, true).at(0) == "nüe4");
        assert(g2p_zh->getDefaultPinyin("虐", Pinyin::ManTone::Style::TONE3, false).at(0) == "nve4");
        assert(g2p_zh->getDefaultPinyin("了", Pinyin::ManTone::Style::TONE3, false, false).at(0) == "le");
        assert(g2p_zh->getDefaultPinyin("了", Pinyin::ManTone::Style::TONE3, false, true).at(0) == "le5");
        return true;
    }

    bool ManTest::batchTest(bool resDisplay) const {
        size_t count = 0;
        size_t error = 0;

        const auto dataLines = readData("testData/op_lab.txt");

        const auto start = std::chrono::high_resolution_clock::now();
        for (const auto &line : dataLines) {
            const auto keyValuePair = Pinyin::split(line, "|");

            if (keyValuePair.size() == 2) {
                const std::string &hans = keyValuePair[0];
                const std::string &pinyin = keyValuePair[1];

                auto result = g2p_zh->hanziToPinyin(hans, Pinyin::ManTone::Style::NORMAL, Pinyin::Error::Default, false,
                                                    true).
                                      toStdStr();

                auto words = Pinyin::split(pinyin, " ");
                const auto wordSize = words.size();
                count += wordSize;

                bool diff = false;
                auto resWords = Pinyin::split(result, " ");
                for (int i = 0; i < wordSize; i++) {
                    const auto expectedWords = Pinyin::split(words[i], "/");
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

        std::cout << "batchTest: success" << std::endl;
        std::cout << "batchTest time: " << duration << "ms" << std::endl;
        std::cout << "错误率: " << percentage << "%" << std::endl;
        std::cout << "错误数: " << error << std::endl;
        std::cout << "总字数: " << count << std::endl;
        return true;
    }
}
