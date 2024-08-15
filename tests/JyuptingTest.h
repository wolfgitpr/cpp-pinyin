#ifndef DATASET_TOOLS_JYUPTINGTEST_H
#define DATASET_TOOLS_JYUPTINGTEST_H

#include "G2pglobal.h"
#include "Jyutping.h"

namespace Test
{
    class JyuptingTest
    {
    public:
        explicit JyuptingTest();
        ~JyuptingTest();
        bool batchTest(const bool& resDisplay = false) const;

    private:
        std::unique_ptr<Pinyin::Jyutping> g2p_can;
    };
} // Test

#endif // DATASET_TOOLS_JYUPTINGTEST_H
