#ifndef DATASET_TOOLS_MANTEST_H
#define DATASET_TOOLS_MANTEST_H

#include <cpp-pinyin/Pinyin.h>

namespace Test
{
    class ManTest {
    public:
        explicit ManTest();
        ~ManTest();
        bool apiTest() const;
        bool toneBatchTest(bool resDisplay = false) const;
        bool unToneBatchTest(bool resDisplay = false) const;

    private:
        std::unique_ptr<Pinyin::Pinyin> g2p_zh;
    };
} // Test

#endif // DATASET_TOOLS_MANTEST_H
