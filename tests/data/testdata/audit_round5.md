# 第五轮：金标内部自洽修复波（2026-09-29）

前两轮审计靠人工逐句读，反复发现同一件事：**同一个词在同一句里出现两次，金标却给了两种读音**。这一轮把它做成机械筛查（`twin_check.py`，**只筛面不裁定**），再逐词裁定后统一修。

做法：把句子投影成汉字序列，取长度 2–5 的相邻 n-gram，找在同一句里出现≥2 次的词，逐位置比较金标单元格；同一行内只保留**极大**词（`僮族瑶族/僮族/瑶族` 只报一次）。分类：

| 类型 | 含义 | 本轮发现 |
|---|---|---:|
| disjoint | 两支读法**没有任何交集**，必有一处错 | 99 |
| subset | 一处只收了一个读音（收窄） | 82 |

共 **181 个位置**、**152 行**。逐词裁定（下节）后改了 **150 行**：fix 69、widen 136，另判「本来就对」2 个词。

**为什么这值得单独一轮**：第三、四轮审计的发现里，这一类占了绝大多数；而它**不依赖审计员是否恰好注意到**——机械筛一遍就能把整类穷举出来。而且它抓到的行很多是**从未被审计过**的行（第三轮只抽样 17.7%，第四轮只挑最烂的类）。

## 裁定的词（逐词一个决定，应用到该词所有被标记位置）

| 词 | 位置 | 裁定读音 | 动作 | 理由 | 涉及位置数 |
|---|---:|---|---|---|---:|
| 一幢层 | 1 | `zhuang4` | fix | 幢 as the classifier for buildings is zhuàng | 2 |
| 万俟丑奴 | 1 | `qi2` | fix | 万俟 is the surname Mòqí; 俟 is qí | 2 |
| 三幢 | 1 | `zhuang4` | fix | same: 三幢 | 1 |
| 世良田莳郎 | 3 | `shi4` | fix | 莳 in this name is shì | 1 |
| 中心杆 | 2 | `gan1` | fix | 杆 in 电线杆/中心杆 is gān (gǎn is 枪杆/杆秤) | 1 |
| 丰镐 | 1 | `hao4` | fix | same | 1 |
| 丰镐房 | 1 | `hao4` | fix | 丰镐 the Western-Zhou capital is hào | 1 |
| 为桂西僮族 | 3 | `zhuang4` | fix | same | 0 |
| 为美国 | 0 | `wei2` | fix | 认为 is rènwéi; 为 is wéi | 1 |
| 之子啊 | 2 | `a1/a5` | widen | same | 2 |
| 仇液 | 0 | `qiu2` | fix | same | 1 |
| 仇鸾 | 0 | `qiu2` | fix | 仇 as a surname is qiú | 1 |
| 侯瑱 | 1 | `tian4/zhen4` | widen | 瑱 in a personal name is tiàn or zhèn | 2 |
| 僮族 | 0 | `zhuang4` | fix | 僮族 is the pre-1965 name of 壮族, zhuàng | 4 |
| 僮族自治 | 0 | `zhuang4` | fix | same: 僮族自治县 zhuàng | 0 |
| 党啊 | 1 | `a1/a5` | widen | 啊 after a noun is the neutral particle; ā also occurs | 3 |
| 别墅幢 | 2 | `zhuang4` | fix | same: N幢别墅 | 1 |
| 到似 | 0 | `dou2/dou3` | widen | Cantonese 到 is dou3 with the colloquial 變調 dou2 | 1 |
| 化钌 | 1 | `liao3` | fix | 钌 the element ruthenium is liǎo (四氧化钌) | 3 |
| 单晟凌 | 1 | `cheng2/sheng4` | widen | 晟 is chéng in historical names, shèng in modern | 3 |
| 可怕啊 | 2 | `a1/a5` | widen | same | 1 |
| 台湾枇杷 | 3 | `pa2/pa5` | widen | same | 2 |
| 君子啊 | 2 | `a1/a5` | widen | same | 1 |
| 听啊 | 1 | `a1/a5` | widen | same | 2 |
| 咔喳 | 1 | `cha1/zha1` | widen | 喳 the onomatopoeia is zhā and chā | 1 |
| 哪呢 | 1 | `ni2` | fix | 呢 in this Buddhist transliteration is ní | 3 |
| 喇嘛还 | 2 | `huan2` | fix | 还俗 (to return to lay life) is huán sú | 1 |
| 多幢 | 1 | `zhuang4` | fix | same: 十多幢 | 1 |
| 大凹 | 1 | `wa1` | fix | 凹 in South-Western place names is wā | 1 |
| 头一个力 | 0 | `tou5` | fix | 斧头 is fǔ tou, with the neutral tone | 1 |
| 孔令晟 | 2 | `cheng2/sheng4` | widen | same | 4 |
| 孙晟 | 1 | `cheng2/sheng4` | widen | same | 2 |
| 差雅朴 | 0 | `cha1/chai1` | widen | 差 in this Tibetan transliteration: both are used | 1 |
| 巴萨哩 | 2 | `li1/li3` | widen | same | 4 |
| 幢建筑物 | 0 | `zhuang4` | fix | same (it follows a number here: 一幢建筑物) | 1 |
| 张光晟 | 2 | `cheng2/sheng4` | widen | same | 2 |
| 性歌圩 | 2 | `xu1` | fix | 歌圩: 圩 = 墟 (market) is xū | 2 |
| 我唔得啦 | 3 | `la1/la5` | widen | 啦 is lā and the neutral particle la | 1 |
| 拿撒勒 | 1 | `sa1/sa3` | widen | 撒 in 拿撒勒 (Nazareth) is sā; sǎ also occurs | 1 |
| 斧头 | 1 | `tou5` | fix | 斧头 is fǔ tou (现代汉语词典), the neutral tone | 2 |
| 新啊 | 1 | `a1/a5` | widen | same | 4 |
| 是会 | 0 | `shi4` | fix | the copula 是 is shì, not neutral | 1 |
| 有幢房子 | 1 | `zhuang4` | fix | same: 有N幢房子 | 1 |
| 末振将 | 2 | `jiang1/jiang4` | widen | 将 in a Xiongnu name is not settled | 2 |
| 杈子 | 0 | `cha1` | fix | 杈子 the forked tool is chā | 1 |
| 李晟 | 1 | `cheng2/sheng4` | widen | same | 3 |
| 李芾 | 1 | `fei4/fu2` | widen | 芾 is fú and fèi; the name does not settle it | 1 |
| 杨晟 | 1 | `cheng2/sheng4` | widen | same | 1 |
| 杨朝晟 | 2 | `cheng2/sheng4` | widen | same | 4 |
| 杨镐 | 1 | `hao4` | fix | 镐 in a personal name takes hào | 2 |
| 枇杷 | 1 | `pa2/pa5` | widen | 枇杷 is pípá with a common neutral second syllable | 25 |
| 枇杷螺 | 1 | `pa2/pa5` | widen | same | 10 |
| 柏路 | 0 | `bai3/bo2` | widen | 柏 is bǎi and bó (柏林/柏林) | 1 |
| 格哩 | 1 | `li1/li3` | widen | 哩 in Manchu/Tibetan transliteration: both are used | 4 |
| 桂西僮族自 | 2 | `zhuang4` | fix | same: 桂西僮族自治区 zhuàng | 0 |
| 每圜 | 1 | `yuan2` | fix | 圜 as the Korean currency is yuán (韩圜) | 1 |
| 汉姓为覃 | 3 | `qin2/tan2` | widen | same | 1 |
| 治廑 | 1 | `jin3` | fix | 廑 in this name is jǐn | 2 |
| 番禺 | 0 | `pan1` | fix | 番禺 is Pānyú | 2 |
| 的裨将 | 1 | `pi2` | fix | 裨将 the subordinate general is pí jiàng | 2 |
| 的裨将 | 2 | `jiang4` | fix | and 将 is jiàng in 裨将 | 2 |
| 缊纥提 | 0 | `yun4` | fix | 缊纥提: 缊 is yùn | 1 |
| 臭的 | 0 | `chou4` | fix | 无臭/恶臭 are chòu in the modern reading | 1 |
| 草垛 | 1 | `duo4` | fix | 垛 as a stack is duò | 1 |
| 蒲哩噜 | 1 | `li1/li3` | widen | same | 2 |
| 裁缝 | 1 | `feng5` | fix | 裁缝 the tailor is cái feng (the verb is féng) | 1 |
| 西僮族自治 | 1 | `zhuang4` | fix | same | 5 |
| 覃健 | 0 | `qin2/tan2` | widen | same | 2 |
| 覃家岗 | 0 | `qin2/tan2` | widen | same (a place name from the surname) | 1 |
| 覃氏 | 0 | `qin2/tan2` | widen | same | 1 |
| 覃问 | 0 | `qin2/tan2` | widen | the surname 覃 has both readings | 3 |
| 訾玉甫 | 0 | `zi1` | fix | 訾 as a surname is Zī | 1 |
| 贝子 | 1 | `zi3/zi5` | widen | 贝子 the Qing title: the neutral form is common | 2 |
| 超薄切片法 | 1 | `bao2/bo2` | widen | 薄 is báo in speech and bó in compounds | 1 |
| 越巂郡 | 1 | `xi1` | fix | 越巂郡 the Han commandery is Yuèxī | 2 |
| 跂踵 | 0 | `qi4` | fix | 跂踵 the one-legged creature is qì (跂望) | 2 |
| 跄浦口 | 0 | `qiang1/qiang4` | widen | 跄 is qiāng/qiàng; the place name does not settle it | 2 |
| 郭隗 | 1 | `kui2/wei3` | widen | same | 3 |
| 酸脚杆 | 2 | `gan1/gan3` | widen | 杆 is gān (a rod) and gǎn (a handle) | 1 |
| 长孙晟 | 2 | `cheng2/sheng4` | widen | same | 2 |
| 阆中 | 0 | `lang4` | fix | 阆中 is Làngzhōng | 1 |
| 阙特勤 | 0 | `que1/que4` | widen | 阙 is quē and què in names | 1 |
| 隗嚣 | 0 | `kui2/wei3` | widen | the surname 隗 has both readings | 14 |
| 隗嚣归 | 0 | `kui2/wei3` | widen | same | 2 |
| 隗顺 | 0 | `kui2/wei3` | widen | same | 1 |
| 隽新 | 0 | `juan4/jun4` | widen | 隽 is juàn and jùn; a school name does not settle it | 2 |
| 雀儿豆 | 1 | `er2/er5` | widen | 儿 in a plant name: full and neutral both occur | 1 |
| 靠闷酸 | 1 | `men1/men4` | widen | 闷 in this Zhuang word: both occur | 1 |
| 韦瑱 | 1 | `tian4/zhen4` | widen | same | 4 |
| 颉利 | 0 | `xie2` | fix | same | 1 |
| 颉利可汗 | 0 | `xie2` | fix | 颉利可汗 is Xiélì Kèhán; jié is 仓颉 | 1 |
| 饮鹿池 | 0 | `yin4` | fix | 饮 here means to water animals: yìn | 1 |
| 高句丽 | 1 | `gou1` | fix | 高句丽 is Gāogōulí; 句 is gōu | 6 |
| 高句丽 | 2 | `li2` | fix | and 丽 is lí in this name, not lì | 6 |
| 高句丽军 | 1 | `gou1` | fix | same name: 句 is gōu | 1 |
| 黄晟 | 1 | `cheng2/sheng4` | widen | same | 1 |

## 判定为「本来就对」的例外（检测器无法区分）

- **坊为**：作坊 fang（轻声）与酒坊 fáng 是两个词，两处都对
- **的铺**：商铺 pù 与铺路的铺 pū 是两个词，句子本身就是在对比这两种读法

这类例外说明机械筛查**不能自动改**：同一个字在不同词里本来就可能有不同读音，所以每一组都由人裁定，脚本只负责定位。

## 剩下的（**未解决，明确记账**）

修复后重跑检测器：**只剩 2 组**，而且两组都是**合法的**（`的铺`、`坊为`：同一个字在不同词里本来就该读不同音）——按这个判据，**金标里已不存在可机械检出的自相矛盾**。

### 这一轮我自己踩的坑（记下来，因为它代价最大）

`twin_check.py` 每个“位置对”只输出**一条**记录（`cell` 在 #i、`other` 在 #j）。第一遍我只对记录**自己那一侧**应用裁定，于是**恰好落在 `other` 侧的错读音全部漏修**：枇杷这一类记录挂在**宽**的一侧（`pa2/pa5`），而需要改的正是窄的一侧（`pa5`）。结果是第一遍改了 96 处、却留下 82 组自相矛盾（本文件头一版就是这么写的）。修法是**两个方向都出记录**（镜像记录，偏移取另一侧在词内的位置）再跑一遍：裁定从 96 处涨到 205 处，队列从 104 行降到 4 行。

教训：**“检测器说 A 与 B 不一致”不等于“要改的是 A”**。凡按“发现的记录”直接改的流水线都必须同时处理两侧，否则修掉的恰好是**容易修的那一半**，剩下的那一半看起来还像别的问题。

## 复现

```
python .tmp/gold/twin_check.py   # 机械筛查（写 inconsistencies.md + 结构化清单）
python .tmp/gold/twin_apply.py   # 逐词裁定表 → consistency_overrides.jsonl
python .tmp/gold/assemble.py; python .tmp/gold/check_gold.py
python .tmp/gold/find_mismatch.py
```
