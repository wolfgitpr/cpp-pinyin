# 词典缺口：金标接受、但词典里没有的读音

来源：`tools/corpus/cell_audit.py`（把每个金标读音与该字的词典读音集合逐一对账）。
这些位置**不是引擎缺陷**——引擎用词典里有的那个读音照样算对（行级 `strict` 不受影响）——
而是**词典缺条目**：审音两轮给出的可接受读音里，有一个是标准又音/口语音变，词典没有。
补词典时应该按这张表补，补完把对应行重新冻结即可（数字只会变好）。

| 字 | 金标接受 | 词典现有 | 语料 | 位置数 | 例 |
|---|---|---|---|---:|---|
| 晟 | sheng4 | cheng2 | mandarin_tone | 55 | cm:73828 |
| 隗 | wei3 | kui2 | mandarin_tone | 36 | cm:78937 |
| 啊 | a5 | a1 | mandarin_tone | 32 | cm:61548 |
| 覃 | qin2 | tan2 | mandarin_tone | 24 | cm:75793 |
| 啊 | a4 | a1 | mandarin_tone | 18 | cm:61550 |
| 崴 | wei1 | wai3 | mandarin_tone | 14 | cm:76398 |
| 埏 | yan2 | shan1 | mandarin_tone | 13 | cm:78734 |
| 咥 | die2 | xi4 | mandarin_tone | 10 | cm:78775 |
| 瑱 | tian4 | zhen4 | mandarin_tone | 10 | cm:79096 |
| 裨 | pi2 | bi4 | mandarin_tone | 7 | cm:59086 |
| 嗯 | en4 | en5/n4 | mandarin_tone | 7 | cm:75936 |
| 唉 | ai4 | ai1 | mandarin_tone | 5 | cm:75612 |
| 柏 | baak3 | paak3 | cantonese_tone | 3 | ct:964 |
| 嗯 | en1 | en5/n4 | mandarin_tone | 2 | cm:75937 |
| 垛 | duo4 | duo3 | mandarin_tone | 2 | cm:76243 |
| 噏 | ngap1 | ap1 | cantonese_tone | 2 | ct:900 |
| 埗 | bou2 | bou6 | cantonese_tone | 2 | ct:1235 |
| 剩 | zing6 | sing6 | cantonese_tone | 2 | ct:1481 |
| 甥 | saang1 | sang1 | cantonese_tone | 2 | ct:2528 |
| 睏 | fan3 | kwan3 | cantonese_tone | 2 | ct:3413 |
| 幢 | zhuang4 | chuang2 | mandarin_tone | 1 | cm:59739 |
| 呵 | a1 | he1 | mandarin_tone | 1 | cm:65448 |
| 哦 | o5 | e2/o4 | mandarin_tone | 1 | cm:74330 |
| 囤 | dun4 | tun2 | mandarin_tone | 1 | cm:75400 |
| 喂 | wei2 | wei4 | mandarin_tone | 1 | cm:76737 |
| 婆 | po1 | po2/po4 | cantonese_tone | 1 | ct:68 |
| 喇 | laa1 | laa3 | cantonese_tone | 1 | ct:73 |
| 冇 | mou2 | mou5/ou5 | cantonese_tone | 1 | ct:137 |
| 呃 | ngaak1 | aak1 | cantonese_tone | 1 | ct:877 |
| 莆 | pou2 | fu2/pou4 | cantonese_tone | 1 | ct:894 |
| 鹿 | luk6 | luk2 | cantonese_tone | 1 | ct:1600 |
| 隙 | gwik1 | kwik1 | cantonese_tone | 1 | ct:2858 |
| 搜 | sau1 | sau2 | cantonese_tone | 1 | ct:2894 |
| 燕 | jin1 | jin3 | cantonese_tone | 1 | ct:3164 |
| 啱 | aam1 | ngaam1 | cantonese_tone | 1 | ct:3193 |
| 凹 | aau3 | nap1 | cantonese_tone | 1 | ct:3762 |
| 率 | leot6 | leot2/seot1 | cantonese_tone | 1 | ct:5921 |
| 喱 | nei1 | le1/lei1/lei2 | cantonese_tone | 1 | ct:5997 |
| 抹 | mut3 | maat3 | cantonese_tone | 1 | ct:6336 |

合计 **39 组 / 267 个位置**。

复现：`python tools/corpus/cell_audit.py && python tools/corpus/dict_gaps.py`
