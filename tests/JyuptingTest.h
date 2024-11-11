#ifndef DATASET_TOOLS_JYUPTINGTEST_H
#define DATASET_TOOLS_JYUPTINGTEST_H

#include <cpp-pinyin/Jyutping.h>

namespace Test
{
    class JyuptingTest
    {
    public:
        explicit JyuptingTest();
        ~JyuptingTest();
        bool toneBatchTest(const bool& resDisplay = false) const;
        bool untoneBatchTest(const bool& resDisplay = false) const;

    private:
        std::unique_ptr<Pinyin::Jyutping> g2p_can;
    };
} // Test

#endif // DATASET_TOOLS_JYUPTINGTEST_H
