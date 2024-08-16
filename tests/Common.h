#ifndef DATASET_TOOLS_COMMON_H
#define DATASET_TOOLS_COMMON_H

#include <vector>
#include <string>

namespace Test
{
    std::vector<std::string> readData(const std::string &filename);
    std::vector<std::string> split(const std::string &s, const std::string &delimiter);
}

#endif // DATASET_TOOLS_COMMON_H
