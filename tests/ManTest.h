#ifndef DATASET_TOOLS_MANTEST_H
#define DATASET_TOOLS_MANTEST_H

#include "G2pglobal.h"
#include "Pinyin.h"

namespace Test
{
    class ManTest
    {
    public:
        explicit ManTest();
        ~ManTest();
        bool apiTest() const;
        bool batchTest(bool resDisplay = false) const;

    private:
        std::unique_ptr<Pinyin::Pinyin> g2p_zh;
    };
} // Test

#endif // DATASET_TOOLS_MANTEST_H
