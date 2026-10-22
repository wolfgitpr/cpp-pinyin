# Dictionary issues found while building the v2 verified sets

These are engine-side defects. None of them is a verified defect and none of them was changed here; they are listed
because the whole-verified work turned them up, with the evidence that identifies them.

## res/dict/mandarin/user_dict.txt contradicts the main table

| entry | user_dict says | main table / standard says |
| --- | --- | --- |
| 中都 | zhong1 dou1 | zhong1 du1 (du1 = capital city) |
| 上都 | shang4 dou1 | shang4 du1 |
| 国都 | guo2 dou1 | guo2 du1 |
| 澄清 | cheng2 qing1 | deng4 qing1 (cheng2 qing1 also exists, so this one needs a ruling rather than a fix) |
| 今朝 | jin2 zhao1 | jin1 zhao1 |

In every case the corpus writes the string across a word boundary (上/都, 国/都, 今/朝), so the wrong entry never
affected the verified.

## res/dict/mandarin/phrases_dict.txt wrong readings

| entry | phrases_dict says | standard | occurrences in the verified that exposed it |
| --- | --- | --- | --- |
| 勃艮第 | bo2 ge3 di4 | bo2 gen4 di4 (Burgundy) | 168 |
| 湮灭 | yin1 mie4 | yan1 mie4 | 47 |
| 拖拽 | tuo1 ye4 | tuo1 zhuai4 (ye4 is a variant of 曳) | 44 |
| 佛罗里达 | fu2 luo2 li3 da2 | fo2 luo2 li3 da2 (Florida) | 29 |
| 琢磨 | zuo2 mo4 | zhuo2 mo2 (carve) or zuo2 mo5 (ponder) | 20 |
| 瓦隆 | wa3 long1 | wa3 long2 (Wallonia) | 16 |
| 楞严 | leng4 yan2 | leng2 yan2 | 11 |
| 高中 | gao1 zhong4 | gao1 zhong1 (senior high school) | 8 |
| 曾参 | zeng1 shen1 | correct as a word (the disciple), but the corpus only contains 曾 + 参与/参加, so it produces a false conflict | 10 |

Where the two disagreed, the verified was checked against the standard reading every time, and in every case the verified
was right and the dictionary entry was wrong. That is worth recording because it reverses the natural assumption:
for this corpus the phrase table is the less reliable of the two.

## What is not on this list

都会 as du1 hui4 is correct for the word "metropolis"; the corpus only writes 都 + 会, so it is not a defect.
转向 zhuǎn xiàng and zhuàn xiàng are both standard depending on sense, so it needs no entry change.
