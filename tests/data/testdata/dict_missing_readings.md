# 词典缺读音清单（由金标集实证）

> 与此前那张 `dict_gaps.md` 的区别：那张是 `cell_audit.py` 的口径（金标**接受**的又音，引擎用词典里已有的音照样算对，行级 `strict` 不受影响）；这一张是 **DICT_GAP** 口径（该字候选表里根本没有这个音，引擎永远给不出来，行级判据直接记错）。两张都要补，口径不同。

数据来源：`tests/verified/` 里的 **DICT_GAP** 位置——「金标给出的读音不在这一个字自己的候选表里，引擎永远给不出来」的位置。工具：`tools/corpus/gap_support.py`。

- 位置总数：**2326**（306 组「数据集 + 字 + 读音」）
- 其中 **670 个（165 组）能在词典自己的词组条目里找到同一个读音作证**——这类是**字条目不完整**：引擎的单字路径只按字条目的候选给音，而同一个读音在词组条目里其实有；
- 剩下 1656 个没有任何条目作证：**排名靠前的 35 组（约 1,300 个位置）在第十四轮已逐组复核**，第十八轮又把长尾（计数 ≤17：116 组 / 427 个位置）逐组复核，**也全部判对**——读到的都是专名或古读的实例：万俟 mò、澹台 tán、铫期 yáo、谷蠡王 lù/lí、坚镡 tán、单父 fǔ、饮马 yìn、力能扛鼎 gāng、恶心 ě、圩日 xū、边徼 jiào、海蜇 zhē 蜇人。所以这一类的**全部**位置结论都是「金标对、词典缺」，建议按下面的表补字条目。

优先补前几组——每组补一个读音就能消掉上百个位置：

| 数据集 | 字 | 读音 | 位置数 | 词典里的作证 |
|---|---|---|---:|---|
| `mandarin_tone` | 镐 | hao4 | 159 | （无，靠裁定者复核） |
| `mandarin_tone` | 幢 | zhuang4 | 141 | （无，靠裁定者复核） |
| `mandarin_tone` | 啊 | a5 | 75 | （无，靠裁定者复核） |
| `mandarin_tone` | 晟 | sheng4 | 70 | （无，靠裁定者复核） |
| `mandarin_tone` | 裨 | pi2 | 68 | （无，靠裁定者复核） |
| `mandarin_tone` | 子 | zi3 | 64 | phrases_dict.txt 一甲子、phrases_dict.txt 不法分子、phrases_dict.txt 中子弹 |
| `mandarin_tone` | 句 | gou1 | 58 | （无，靠裁定者复核） |
| `cantonese_tone` | 埗 | bou2 | 58 | （无，靠裁定者复核） |
| `mandarin_tone` | 丽 | li2 | 56 | phrases_dict.txt 高勾丽、phrases_dict.txt 高丽参、phrases_dict.txt 高丽 |
| `mandarin_tone` | 隗 | wei3 | 54 | （无，靠裁定者复核） |
| `mandarin_tone` | 将 | jiang1 | 53 | phrases_dict.txt 将信将疑、phrases_dict.txt 大厦将倾、phrases_dict.txt 将今论古 |
| `mandarin_tone` | 僮 | zhuang4 | 48 | （无，靠裁定者复核） |
| `cantonese_tone` | 呀 | aa4 | 48 | （无，靠裁定者复核） |
| `mandarin_tone` | 钌 | liao3 | 47 | （无，靠裁定者复核） |
| `mandarin_tone` | 閤 | ge2 | 43 | （无，靠裁定者复核） |
| `mandarin_tone` | 巂 | xi1 | 43 | （无，靠裁定者复核） |
| `mandarin_tone` | 缪 | miao4 | 35 | （无，靠裁定者复核） |
| `mandarin_tone` | 覃 | qin2 | 34 | （无，靠裁定者复核） |
| `mandarin_tone` | 率 | shuai4 | 32 | phrases_dict.txt 率尔操觚、phrases_dict.txt 率兽食人、phrases_dict.txt 率由旧章 |
| `cantonese_tone` | 呀 | aa3 | 31 | （无，靠裁定者复核） |
| `mandarin_tone` | 圜 | yuan2 | 29 | （无，靠裁定者复核） |
| `mandarin_tone` | 都 | du1 | 28 | phrases_dict.txt 三都县、phrases_dict.txt 伏都教、phrases_dict.txt 内比都 |
| `mandarin_tone` | 为 | wei4 | 25 | phrases_dict.txt 舍己为公、phrases_dict.txt 为什么、phrases_dict.txt 为的是 |
| `mandarin_tone` | 番 | pan1 | 25 | （无，靠裁定者复核） |
| `mandarin_tone` | 鬲 | li4 | 25 | （无，靠裁定者复核） |
| `mandarin_tone` | 垛 | duo4 | 25 | （无，靠裁定者复核） |
| `mandarin_tone` | 瑱 | tian4 | 25 | （无，靠裁定者复核） |
| `mandarin_tone` | 仇 | qiu2 | 23 | （无，靠裁定者复核） |
| `mandarin_tone` | 俟 | qi2 | 23 | （无，靠裁定者复核） |
| `cantonese_tone` | 呢 | ne1 | 23 | （无，靠裁定者复核） |
| `mandarin_tone` | 颉 | xie2 | 19 | （无，靠裁定者复核） |
| `mandarin_tone` | 地 | di4 | 17 | phrases_dict.txt 一席之地、phrases_dict.txt 一败涂地、phrases_dict.txt 三地门 |
| `mandarin_tone` | 廑 | jin3 | 17 | （无，靠裁定者复核） |
| `mandarin_tone` | 訾 | zi1 | 17 | （无，靠裁定者复核） |
| `mandarin_tone` | 锔 | ju2 | 15 | （无，靠裁定者复核） |
| `mandarin_tone` | 絜 | jie2 | 15 | （无，靠裁定者复核） |
| `mandarin_tone` | 镝 | di2 | 15 | （无，靠裁定者复核） |
| `mandarin_tone` | 圩 | xu1 | 14 | （无，靠裁定者复核） |
| `mandarin_tone` | 缊 | yun4 | 14 | （无，靠裁定者复核） |
| `mandarin_tone` | 夫 | fu1 | 13 | phrases_dict.txt 凡夫俗子、phrases_dict.txt 列别杰夫、phrases_dict.txt 夫妻相 |
| `mandarin_tone` | 降 | xiang2 | 13 | phrases_dict.txt 誓死不降、phrases_dict.txt 降龙伏虎、phrases_dict.txt 受降 |
| `mandarin_tone` | 任 | ren2 | 13 | phrases_dict.txt 任丘市、phrases_dict.txt 任达华、phrases_dict.txt 任安 |
| `mandarin_tone` | 大 | da4 | 12 | phrases_dict.txt 出大差、phrases_dict.txt 嘉应大学、phrases_dict.txt 地大物博 |
| `mandarin_tone` | 了 | le5 | 12 | phrases_dict.txt 歇了吧、phrases_dict.txt 为了、word.txt 了 |
| `mandarin_tone` | 长 | zhang3 | 10 | phrases_dict.txt 不长眼睛、phrases_dict.txt 典狱长、phrases_dict.txt 列车长 |
| `mandarin_tone` | 馕 | nang2 | 10 | （无，靠裁定者复核） |
| `mandarin_tone` | 当 | dang1 | 9 | phrases_dict.txt 丁零当啷、phrases_dict.txt 当世之冠、phrases_dict.txt 当世冠 |
| `mandarin_tone` | 恶 | e3 | 9 | （无，靠裁定者复核） |
| `mandarin_tone` | 相 | xiang4 | 9 | phrases_dict.txt 三十二相、phrases_dict.txt 三相点、phrases_dict.txt 八相成道 |
| `mandarin_tone` | 蠡 | li2 | 9 | （无，靠裁定者复核） |
| `mandarin_tone` | 喔 | wo1 | 9 | （无，靠裁定者复核） |
| `mandarin_tone` | 只 | zhi3 | 8 | phrases_dict.txt 只不过、phrases_dict.txt 只欠东风、phrases_dict.txt 只争旦夕 |
| `mandarin_tone` | 是 | shi4 | 8 | phrases_dict.txt 不是吗、phrases_dict.txt 多得是、phrases_dict.txt 是非之地 |
| `mandarin_tone` | 谷 | lu4 | 8 | （无，靠裁定者复核） |
| `mandarin_tone` | 莳 | shi4 | 8 | （无，靠裁定者复核） |
| `mandarin_tone` | 徼 | jiao4 | 8 | （无，靠裁定者复核） |
| `mandarin_tone` | 镡 | tan2 | 8 | （无，靠裁定者复核） |
| `mandarin_tone` | 杻 | niu3 | 8 | （无，靠裁定者复核） |
| `mandarin_tone` | 蜇 | zhe1 | 7 | （无，靠裁定者复核） |
| `mandarin_tone` | 中 | zhong1 | 7 | phrases_dict.txt 中子弹、phrases_dict.txt 中山狼传、phrases_dict.txt 中微子 |

## 复现

```
python tools/corpus/gap_support.py
```

注：普通话词典用**声调符号**写读音（`子:zi,zǐ,zì`），且**无声调音节表示轻声**；金标用 TONE3 数字（`zi3`、轻声 `5`）。工具在比对前做了这层归一化——第一版没归一化，把 2,000 多个位置全判成「无证」，那是个假象，记在这里以免重犯。
