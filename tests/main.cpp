#include <filesystem>

#include "G2pglobal.h"
#include "Pinyin.h"
#include <iostream>

#include "JyuptingTest.h"
#include "ManTest.h"
#include "../src/util/ManTone.h"

using namespace Test;

int main(int argc, char *argv[]) {
    const std::string applicationDirPath = std::filesystem::current_path().string();
    Pinyin::setDictionaryPath(applicationDirPath + "\\dict");
    std::cout << "Path: " << applicationDirPath + "\\dict" << std::endl;

    const ManTest manTest;
    std::cout << "Pinyin G2P test: " << std::endl;
    std::cout << "--------------------" << std::endl;
    std::cout << "apiTest: " << manTest.apiTest() << std::endl;
    // manTest.batchTest();
    std::cout << "--------------------\n" << std::endl;

    // const JyuptingTest jyuptingTest;
    // std::cout << "Cantonese G2P test:" << std::endl;
    // std::cout << "--------------------" << std::endl;
    // jyuptingTest.batchTest();
    // std::cout << "--------------------\n" << std::endl;

    std::cout << "G2P mix test:" << std::endl;
    std::cout << "--------------------" << std::endl;
    const auto g2p_man = std::make_unique<Pinyin::Pinyin>();

    const std::string raw2 =
        "举杯あャ坐ュ饮放あ歌竹林间/清风拂 面悄word然xax asx a xxs拨？Q！动初弦/便推开烦恼与尘喧/便还是当时的少年";
    const auto res1 = g2p_man->hanziToPinyin(raw2, Pinyin::ManTone::Style::TONE3, Pinyin::Error::Ignore, false,
                                             true).
                               toStdStr();

    std::cout << res1 << std::endl;

    std::cout << "--------------------" << std::endl;
    const auto res2 =
        g2p_man->hanziToPinyin(raw2, Pinyin::ManTone::Style::TONE3, Pinyin::Error::Ignore, false, true).
                 toStdStr();

    std::cout << res2 << std::endl;
    return 0;
}
