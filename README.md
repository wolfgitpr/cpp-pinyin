# cpp-pinyin

## Intro

cpp-pinyin is a lightweight Chinese/Cantonese to Pinyin library.

Chinese dialects can be used to create their own dictionaries using makedict.

Initial version algorithm reference [zh_CN](https://github.com/ZiQiangWang/zh_CN), and undergo significant optimization.

[pinyin-makedict](https://github.com/wolfgitpr/pinyin-makedict) is the tool for creating Chinese/Cantonese dictionaries.

## Feature

+ Interface reference [pypinyin](https://github.com/mozillazg/python-pinyin)

+ Only Unicode within the range of  [ 0x4E00 - 0x9FFF ]  is supported.

+ Segmentation for heteronym words.

+ Support Traditional and Simplified Chinese.

+ Speed is very fast, about 500,000 words/s.

+ Achieved an accuracy rate of 99.9% on a 200000 word Lyrics-Pinyin test set Without-Tone.

+ The With-Tone test on CPP_Dataset(about 79k sentences) achieved an accuracy of 90.3%, while the accuracy of pypinyin
  was approximately 87%.

## Usage

```c++
#include <filesystem>

const std::string applicationDirPath = std::filesystem::current_path().string();
IKg2p::setDictionaryPath(applicationDirPath + "\\dict");  // Set dictionary path.

const auto g2p_man = std::make_unique<Pinyin::Pinyin>(); // or Pinyin::Jyutping
const std::string raw = "明月@1几32时有##一";
const std::string tar = "ming yue yi ji san er shi you yi";
const auto g2pRes = g2p_man->hanziToPinyin(raw, false, true, IKg2p::errorType::Ignore);
const std::string res = IKg2p::join(g2p_man->resToStringList(g2pRes), " ");
```

## Doc

```c++
//  include/PinyinRes.h
struct PinyinRes
{
    std::string hanzi;                      //  utf-8 std::string
    std::string pinyin;                     //  utf-8 std::string
    std::vector<std::string> candidates;    //  Candidate pinyin of Polyphonic Characters.
    bool error = true;                      //  Whether the conversion failed.
};

class PinyinResVector : public std::vector<PinyinRes>
{
public:
    //  Convert to utf-8 std::string vector.
    inline std::vector<std::string> toStdVector() const;
    //  Convert to utf-8 std::string with delimiter(default: " ").
    inline std::string toStdStr(const std::string& delimiter = " ");
};

//  include/ChineseG2p.h
  enum class Error {
      // Keep original characters
      Default = 0,
      // Ignore this character (do not export)
      Ignore = 1
  };

/*
    @param hans : raw utf-8 std::string.
    @param ManTone::Style : Preserve the pinyin tone.
    @param errorType : Ignore words that have failed conversion. Default: Keep original.
    @param v_to_u : Convert v to ü. Default: false.
    @param neutral_tone_with_five : Use 5 as neutral tone. Default: false.
    @return PinyinResVector.
*/
PinyinResVector hanziToPinyin(const std::string &hans,
                              ManTone::Style style = ManTone::Style::TONE,
                              Error error = Error::Default, bool v_to_u = false,
                              bool neutral_tone_with_five = false) const;

/*
    @param hans : raw utf-8 std::string vector, each element of the vector is a character.
    ...
    @return PinyinResVector.
*/
PinyinResVector hanziToPinyin(const std::vector<std::string> &hans,
                              ManTone::Style style = ManTone::Style::TONE,
                              Error error = Error::Default, bool v_to_u = false,
                              bool neutral_tone_with_five = false) const;

//  Convert to Simplified Chinese.  utf-8 std::string
std::string tradToSim(const std::string& hanzi) const;

//  Determine if it is a polyphonic character.
bool isPolyphonic(const std::string& hanzi) const;

//  Get a pronunciation list.
std::vector<std::string> getDefaultPinyin(const std::string &hanzi,
                                          ManTone::Style style = ManTone::Style::TONE,
                                          bool v_to_u = false, bool neutral_tone_with_five = false) const;
```

## Open-source softwares used

+ [zh_CN](https://github.com/ZiQiangWang/zh_CN)
  The core algorithm source has been further tailored to the dictionary in this project.

+ [opencpop](http://wenet.org.cn/opencpop/)
  The test data source.

+ [M4Singer](https://github.com/M4Singer/M4Singer)
  The test data source.

+ [cc-edict](https://cc-cedict.org/wiki/)
  The dictionary source.

+ [pinyin](https://github.com/kfcd/pinyin)
  The fan-jian dictionary source.

+ [cpp_dataset](https://github.com/kakaobrain/g2pm/tree/master/data)
  The cpp_dataset source.

## Contributors

+ Sine Striker
