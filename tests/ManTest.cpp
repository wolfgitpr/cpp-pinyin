#include "ManTest.h"

#include <chrono>
#include <iostream>

#include "Common.h"

namespace Test
{
    ManTest::ManTest() : g2p_zh(std::make_unique<Pinyin::Pinyin>())
    {
    }

    ManTest::~ManTest() = default;

    bool ManTest::apiTest() const
    {
        if (!g2p_zh->isPolyphonic("的"))
            return false;
        if (g2p_zh->isPolyphonic("犬"))
            return false;
        if (g2p_zh->tradToSim("臺") != "台")
            return false;
        if (g2p_zh->tradToSim("犬") != "犬")
            return false;
        if (!(g2p_zh->getDefaultPinyin("杆").at(0) == "gan3" && g2p_zh->getDefaultPinyin("杆").at(1) == "gan1"))
            return false;
        if (g2p_zh->getDefaultPinyin("杆", Pinyin::ManTone::Style::NORMAL).at(0) != "gan")
            return false;
        return true;
    }

    bool ManTest::batchTest(bool resDisplay) const
    {
        size_t count = 0;
        size_t error = 0;

        const auto dataLines = readData("testData/op_lab.txt");

        const auto start = std::chrono::high_resolution_clock::now();
        for (const auto& line : dataLines)
        {
            const auto keyValuePair = Pinyin::split(line, "|");

            if (keyValuePair.size() == 2)
            {
                const std::string& hans = keyValuePair[0];
                const std::string& pinyin = keyValuePair[1];

                auto result = g2p_zh->hanziToPinyin(hans, Pinyin::ManTone::Style::NORMAL, Pinyin::Error::Default, false, true).
                                      toStdStr();

                auto words = Pinyin::split(pinyin, " ");
                const auto wordSize = words.size();
                count += wordSize;

                bool diff = false;
                auto resWords = Pinyin::split(result, " ");
                for (int i = 0; i < wordSize; i++)
                {
                    const auto expectedWords = Pinyin::split(words[i], "/");
                    if (std::find(expectedWords.begin(), expectedWords.end(), resWords[i]) == expectedWords.end())
                    {
                        diff = true;
                        error++;
                    }
                }

                if (resDisplay && diff)
                {
                    std::cout << "text: " << hans << std::endl;
                    std::cout << "raw: " << pinyin << std::endl;
                    std::cout << "res: " << result << std::endl;
                }
            }
            else
            {
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
