# 第二十五轮：四个抽检队列、22 处净改动、1 处自我撤销（2026-10-06）

## 1. 这一轮在做什么

第 24 轮收工后，把「逐句抽检」做成常规队列：一批 40 行、一位读者、一份纪律文件
（`.tmp/gold/_reader_prompt.md`：只在有把握时报、报前逐字核对位次、不许读别的文件、边读边落盘、
「一/不」与带 `/` 的两可位置不报）。读者的产出只是**候选声明**，每一条都由我逐条复核，判据顺序是
**词典词组条目 → 语料自洽 → 语感**，成立才写进 `scan_overrides.jsonl`。

四个队列：

| 队列 | 载荷 | 规模 | 关注点 |
|---|---|---|---|
| audit15 | `le_liao.txt` / `zhe.txt` / `de.txt` | 25 / 28 / 325 行 | 「了」liǎo、「着」zhuó/zháo、「得」dé 的成词与否 |
| audit16 | `guo_a.txt` / `guo_b.txt` | 208 / 207 行 | 「过」经历体清点（第 24 轮） |
| audit16 | `di_1..3.txt` + `small.txt` | 820 + 27 行 | 「地」de/dì；「的」de/dì/dí、「得」děi、「着」zhuó |
| audit17 / audit18 | `batch01..16.txt` | 16×40 行 ×2 | 从**没被逐句看过**的普通话行里随机抽检 |
| audit19 | `jiang_1.txt` / `jiang_2.txt` | 114 / 113 行 | 「将」标 jiang4 的**类别清点**：一处一个可判定问题——「这一处是不是介词『把』/副词『将要』？」|
| audit21 | `chang_1..4.txt` | 277 处「长」 | 义项清点：「生长/年长/长官」zhǎng 还是「长短/长久」cháng |
| audit22 | `chuan_1..3.txt`、`zhong_1..4.txt` | 约 190 / 约 300 处 | 同法清点「传」（chuán/zhuàn）与「重」（zhòng/chóng）|

清点战绩：**将** 227 处抓到 24 处错标（把字句误标 jiàng）；**长** 277 处抓到 3 处，全部落在同一个盲点
（地名/物名里的「长」应读 cháng：长汀县、西安长安区、长板凳）；**传** 三组交齐（54+90+90 处）**零判错**，
zhuan4 那 8 处也对（列传、乘传＝驿站车马、甄嬛传、使徒行传）。这说明金标的**高频虚词与专名**是两类最容易出错的
地方，而像「传」这种词典条目齐备、语境义项分明的字，出错率极低。

`audit19` 是第 24 轮那条方法结论（§10.24）的又一次应用：不问「你觉得读什么」，只问一个能回答是否的问题。
第一组 114 处里报了 13 处，逐句核对**全部成立**（`cm:58044` 同句的「武贲郎将」读者主动放过）；
判据同时被语料自己证实——`将其` 53 处写 jiang1、只有 2 处写 jiang4。

## 2. 改动清单（第 26–32 波）

第 26–44 波共写入 **127 条**覆盖记录：**115 处**是读者申报成立后的**净改动**，另有 6 条是撤销我自己第 28 波的
错改（§4），其余为同波内的成对/重复记录。净改动的 115 处：

> **工具坑（第 43 波踩到）**：含汉字的诊断输出必须由 Python 自己 `io.open(..., encoding="utf-8")` 写文件，
> **不要**靠 `python ... | Out-File` ——PowerShell 会按控制台代码页解码一次、再按 UTF-8 重编码，中文全成乱码
> （第 43 波想留档的「隽」分布就是这样丢的，只能重跑）。ASCII 输出无所谓。

| 类别 | 位置 | 改动 | 判据 |
|---|---|---|---|
| 了/得/的 | `cm:38431` 位29、`cm:65448` 位6 | liao3 → **le5** | 完成体助词；同行/同库同类句都写 le5 |
| 了/得/的 | `cm:78886` 位17 | de2 → **de5** | 词典 `使得:shǐ,de`；同库 27 行「使得」全 de5 |
| 了/得/的 | `cm:68742` 位33、`cm:68763` 位15 | de2 → **de5** | 「品尝得到」是 V+得+到 可能补语 |
| 了/得/的 | `cm:29049` 位4 | di4 → **de5** | 「醒目的…」是定语标记 |
| 形声 | `cm:60390` 位14 | pang4 → **pan2** | 成语「心广体胖」，安泰义 pán |
| 形声 | `cm:12045` 位2、`cm:53290` 位27 | yang5 → **yang2**；zai3 → **zi3** | 复合词「太阳能」不轻声；「黑仔枣」幼小义 zǐ（词典 `仔猪:zǐ,zhū`）|
| 处/晃/削/磨/蔚/脯 | `cm:23332` 位3 | chu3 → **chu4** | 名词「之处」；同库 10 行「之处」全 chu4 |
| 处/晃/削/磨/蔚/脯 | `cm:65046` 位7、`cm:65103` 位1 | huang3 → **huang4** | 副词「一晃」yīhuàng |
| 处/晃/削/磨/蔚/脯 | `cm:38349` 位8 | xiao1 → **xue1** | 书面语「如削」（同 削壁/削铁如泥）|
| 处/晃/削/磨/蔚/脯 | `cm:41497` 位40 | mo4 → **mo2** | 动词「磨玉米」研磨义 |
| 处/晃/削/磨/蔚/脯 | `cm:52544` 位1 | wei4 → **yu4** | 河北「蔚县」Yùxiàn |
| 处/晃/削/磨/蔚/脯 | `cm:77720` 位2、`cm:77741` 位30/位35 | pu2 → **fu3** | 「脯氨酸」fǔ'ānsuān（肉脯义）|
| 方剂/词 | `cm:68167` 位14 | san4 → **san3** | 「牵正散」方剂名，粉末义 sǎn |
| 方剂/词 | `cm:69156` 位30 | huo5 → **he2** | 「温和的嘲讽」是不严厉义 wēnhé |
| 专名 | `cm:25722` 位14 | ji4 → **ji3** | 「济阴」因济水得名（词典 `济水:jǐ,shuǐ`）|
| 专名 | `cm:66161` 位4 | shi4 → **kuo4** | 「赵汝适」Zhào Rǔkuò（`适:shì,kuò`）|
| 高频虚词 | `cm:3603` 位7 | jiang4 → **jiang1** | 介词「把」（将 X 映射到…）；语料 将 jiāng 510 处 vs jiàng 228 处 |
| 高频虚词 | `cm:3610`/`17472`/`18526`/`29355`/`35957`/`52160`×2/`52395`/`55383`×3/`55949`/`58044` | jiang4 → **jiang1** | 同上：13 处都是「把/将要」（`audit19` 类别清点第一组，逐句核过）|
| 高频虚词 | `cm:61644`/`65062`/`66423`/`67672`/`68277`/`73425`/`73781`/`73785`/`78664` | jiang4 → **jiang1** | 第二组：同样是「将+宾语+动词」的把字句（将河间一事、将她作为、将钉子钉入…）|
| 高频虚词 | `cm:59124` 位27、`cm:59143` 位7 | jiang4 → **jiang1** | 「裨将军」是官名（裨+将军），将军读 jiāngjūn；同句「裨将王离」是 裨将，不动 |
| 地名/姓氏 | `cm:31027` 位27 | ren4 → **ren2** | 「任城国妃」的任城是地名；词典 `任县:rén,xiàn`、`任丘市:rén,qiū,shì`、`任安:rén,ān` |
| 地名/姓氏 | `cm:78409` 位13 | yue4 → **le4** | 「长乐府」＝福建长乐，读 chánglè；语料 长乐 的「乐」le4 2 处（长乐县、长乐宫）、yue4 只此 1 处 |
| 结构助词 vs 名词 | `cm:9378` 位12、`cm:9849` 位23/46、`cm:31887` 位6、`cm:33852` 位39、`cm:65066` 位34 | de5 → **di4** | 「**等地**」＝等+地（名词「地方」）；语料 31 处「等地」25 处写 di4，de5 的 6 处全是同一句式（列举后收尾、后接逗号/句号），读者只报了 1 处，我查惯例后把同类 6 处一起改 |
| 地名/姓氏 | `cm:52228` 位13、`cm:55404` 位19、`cm:59722` 位38 | zhang3 → **chang2** | 地名/事物名里的「长」是 cháng：「长汀县」Chángtīng、「西安长安区」Cháng'ān、「长板凳」＝长条凳（audit21「长」义项清点的收获）|
| 多音虚词 | `cm:51979` 位3 | zai4 → **zai3** | 「**录载**」是记载义（同记载/刊载/转载），载读 zǎi；zài 是装载运载 |
| 人名同类批量 | `cm:43211` 等 **50 处** | xian3 → **xi3** | 语料里 109 处「铣」中有 50 处是「**萧铣**」且全标 xian3；该名读 Xiāo Xǐ（第 43 波改了一处，第 44 波按同名同类一并改）——信号来自 audit20 第 11 批读者把另一处当「正确读法」核过（它看到的载荷早于第 43 波）|
| 地名/多音字 | `cm:56083` 位15、`cm:27134` 位31/32 | lang2 → **lang4**；lei3 → **lei2** | 「阆州」与「阆中」同音 làng（同句位10 本就写 lang4，自己跟自己矛盾）；「负债累累」＝léiléi（累积得多），lěilěi 是憔悴颓丧义 |
| 译名 | `cm:29492` 位18、`cm:43210` 位3 | xiao4 → **xiao1** | 「梅肖韦」Maeshowe、「诺沃肖洛夫」Novosyolov 音译；语料 肖 xiao1 122 处 vs xiao4 9 处 |

**没有改的**：`cm:74312`「为什么」本来就是 shén（读者报「现标 shi2」，金标从来不是 shi2）。

## 3. 驳回的声明（判据一律是词典条目或语料自洽）

| 声明 | 判据 |
|---|---|
| `cm:62723` 闷 men1→men4 | 词典条目 **`胸闷:xiōng,mēn`**；语料「胸闷」4/4 都是 men1 |
| `cm:2496` 得 de5→de2 | 词典条目 **`值得:zhí,de`**（另有 `不值得:bù,zhí,de`）；语料「值得」6/6 de5 |
| `cm:3570` 识 shi5→shi2 | 词典条目 **`知识:zhī,shi`**、`知识分子:zhī,shi,…`；语料「知识」59/59、「认识」40/40 |
| `cm:65923` 为 wei4→wei2 | 词典条目 **`因为:yīn,wèi`**；语料「因为」wei4 82 / wei2 1 |
| `cm:25124` 累 lei4→lei2 | 词典条目 **`劳累:láo,lèi`** |
| `cm:62481` 挣 zheng4→zheng1 | `挣扎:zhēng,zhá` 只管「挣扎」这个词；语料「挣脱」20/20 zheng4，独立的「挣」28:3 偏 zheng4 |
| `cm:59093` 长 zhang3→chang2 | 「乱是用长」＝祸乱滋长，正是 zhǎng；语料「乱是用长」2/2、「滋长」2/2 都 zhang3 |
| `cm:66687` 豁 huo1→huo4 | 豁口＝huōkǒu；语料「豁口」huo1 20/20（豁达/豁然才是 huo4）|
| `cm:19170` 塞 sai4→sai1 | 译名「何塞/圣何塞」语料 7/7 sai4（同 塞纳、塞浦路斯）|
| `cm:69417` 缪 | 现标 `miao4` 就是 miào，读者自己的结论也是「很可能对」|
| `cm:77295`、`cm:77319` 閤 ge2→he2 | 閤 是 阁 的异体（gé）；语料「閤」ge2 43/43、「阁」ge2 44/44；两行是 DICT_GAP（词典缺 閤 的 gé）|
| `cm:53321` 肚子 du3→du4 | 语料对「肚」是成体系分工的：动物胃（牛肚/羊肚/爆肚/皮肚/涮肚子）du3，腹部（肚脐/肚皮）du4 |
| `cm:24269` 蒙 meng3→meng2 | 「蒙古」就是 **Měnggǔ**（上声），现标本就正确 |
| `cm:55383` 将 ×3 | 与 `audit19` 类别清点独立报出同三处，第 34 波已改（交叉印证，无新增改动）|
| `cm:55009` 恶 e3→e4 | 「恶心」就是 **ěxin**（e3），现标本就正确——读者是被候选表里没有 ě 误导的（`恶 ě` 是已知词典缺口）|
| `cm:36567` 朴 piao2→pu3 | 韩国姓氏「朴」读 **Piáo**（朴槿惠 Piáo Jǐnhuì），piao2 正确 |
| `cm:36567` 丧 sang1→sang4 | 「国丧」是名词，正读 **sāng**——读者理由里写的也是 sāng，与自己的结论矛盾 |
| `cm:67472` 分 fen5→fen4 | 「部分」普通话就是 **bùfen**（轻声），属轻声风格类，不是错 |
| `cm:58127` 与 yu3→yu4、`cm:55798` 为 wei2→wei4 | 两种句读都成立（「与（之）议」介词省略宾语 vs 「与议＝参与」；「为…所…」被动 vs 诗题「为＝替」）——读者自己也标成「不确定、不报」，不改 |
| `cm:19216` 塞 sai4→sai1 | 读者的前提「音译中一律读 sāi」被词典本身否掉：词典里音译的「塞」全部注 **sài**（塞内加尔、塞拉利昂、塞浦路斯、塞舌尔、塞韦里诺…，台港式译名用 sè），**没有一条注 sāi**；本行还是 `stratum=dispute` 的既有裁定 |
| `cm:67672` 将 jiang4→jiang1 | 与第 37 波「将」清点第二组独立报出同一处（交叉印证，无新增改动）|
| `cm:77325` 藏 cang2→zang4 | 句面是「此外，**西藏閤**还有漆木器出土，种类包括围棋盘、琴、盒、笥、耳杯、盘、奁、镶玉金樽等」——这是**藏閤**（cánggé，存放器物的闼室/库房，「西」是方位），随后的器物全是汉墓随葬品，不是西藏；该行本身是 `stratum=dispute` 的 DICT_GAP 裁定，注里写的正是「引擎产不出正确读音」——即读者主张的 zàng 恰是引擎默认值 |
| `cm:62736` 闷 men1→men4 | 词典条目就是 `胸闷:xiōng,mēn`；「闷」读 mēn 表示（胸中）憋闷不通，mèn 是心情烦闷 |
| `cm:20869` 论 lun4→lun2 | 「谈论」就是 **tánlùn**；lún 只用于《论语》一类——读者把两个义项的方向搞反了 |
| `cm:4312` 系 xi5→xi4 | 「关系」词典作 `关·系`（第二音节可轻声），xi5 与 xi4 都在标准之内；同一读者的同类主张（`cm:4448` 关系 xi5）本轮也已驳回，属轻声风格类 |
| `cm:65795` 示 shi5→shi4 | 「告示」词典作 **gàoshi**（第二音节可轻声），与「关系/部分/位置」同属轻声风格类，不是错 |
| `cm:41602`/`cm:12063`/`cm:43552`/`cm:65767`/`cm:18716` 将 jiang1→jiang4 ×5 | 读者把规则**反了**：「将上面的人杀掉」「将他带到上层大气」正是把字句，读 **jiāng**（一声），jiàng 才是将领/将官——同一类在第 34、37 波已按词典与语料（将其 jiāng 53 : jiàng 2）改过 24 处，不能反向改回 |
| `cm:75681` 隽 jun4→juan4（奕隽）| 语料自己否掉了这条：全语料 107 处「隽」= **jun4 94 / juan4 8 / juan4·jun4 5**，人名一律 jun4（裴延隽、任鸿隽、李隽青、曾毓隽、严隽琪、萧隽、姚隽…），juan4 只出现在隽永/清隽/下隽一类；更直接的是**同一个人**在 `cm:75757` 里写作「潘奕**隽**」标的就是 jun4。词典条目也只有 `隽誉:jùn,yù` |
| `cm:19145` 塞 sai4→sai1（塞维鲁 Severus）| 同一个错误前提**第二次**出现（第 20 波 `cm:19216` 维塞什已驳过一次）：读者说「音译的塞读 sāi」，而词典 17 条含「塞」的条目里音译**全部注 sài**，其中 `塞韦里诺:sài,wéi,lǐ,nuò`（Severino）与本题的 Severus 是同一种 "Se-" 对音，`塞尔达:sài`、`塞内加尔:sài` 同理 |

统计口径：`audit17` 十六批共申报 **46 处**；其中我在本轮逐条核过、并有完整记录的是
第 06/07/11/12/13/14/15/16 批（19 处申报，成立 4 处：散、温和、济、适），另外 9 处（处/晃/削/磨/蔚/脯）
来自第 01/05/10 批。早期批次的成立与否见 `scan_overrides.jsonl` 的波次记录。

## 4. 我自己的错：把「劈腿」改成 pī，第 30 波撤销

第 28 波我把 6 处「劈腿」由 pi3 改成 pi1，理由是「分开腿，同劈叉、劈开」。错。
词典条目就是 **`劈腿:pǐ,tuǐ`**，「腿或手指等过分叉开」这个义项属 **pǐ**
（同 `劈叉:pǐ,chà`、`劈柴:pǐ,chai`）。第 30 波全部撤销回 pi3，`cm:68773` 那一批读者的同类声明也一并驳回。

流程教训（已写进 `decisions.md` §10.25）：**整批改一个重复搭配之前，先 grep 这个词自己的词组条目**。
当时手里已经有 `劈柴:pǐ,chai`，本该拦住我。

## 5. 读者误报的六种形态（下一轮要防）

1. **报出载荷里根本不存在的位次**：`cm:59064` 报「位 0(裨) 现=bi4 应=pi2」，而该行载荷只列了 3/5/9/16/17/19，
   何况金标那一格本来就是 pi2。
2. **把候选表的第一个音当成现标**：同上；`cm:74312` 报「现=shi2」，金标从来是 shén。
3. **理由与结论自相矛盾**：`cm:25124` 写「劳累是去声 lèi」却报「应改 lei2」；`cm:59093` 写「祸乱滋长」却报「应改 cháng」。
4. **把语料既定的轻声写法当成错误**：知识 shi5、胸闷 men1、值得 de5，三条都是同类。
5. **要求回到已被否掉的旧值**：`cm:2496`、`cm:62723` 两行的 `was=` 注记记着旧值（de2、men4），读者正好要求改回去。
6. **覆盖率自述不实**：`audit18/batch06` 的读者说文件「只有 27 条、179 行」，实际是 40 条、133 行，该批已重跑。

## 6. 冻结数字

| 数据集 | pass | ALT_OK | DATA_FIX | ENGINE_BUG | DICT_GAP | EXCLUDED | strict | algo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `mandarin_ctx` | **3,038** | 640 | 969 | 4,957 | 1,396 | 0 | 32.35% | 37.99% |
| `mandarin_untone` | 1,062 | 9 | 19 | 74 | 2 | 0 | 93.32% | 93.50% |
| `cantonese_tone` | 4,944 | 524 | 1,613 | 1,172 | 236 | 2 | 77.83% | 80.84% |
| `cantonese_untone` | 5,767 | 221 | 854 | 454 | 131 | 2 | 90.79% | 92.70% |

相对第二十四轮，`mandarin_ctx` 的 pass 由 3,026 变为 **3,038**（ALT_OK 641→640、DATA_FIX 947→969、
ENGINE_BUG 4,970→4,957、DICT_GAP 1,395→1,396）——注意第 42 波后 pass 曾**回升** 1，第 44 波（50 处「萧铣」）
一挙 +29：pass 下降不是唯一方向，改对金标既可能让原本算对的变错（挪进 ENGINE_BUG/DICT_GAP），
也可能让「引擎早就念对、金标一直念错」的行**转正**（挪进 DATA_FIX 并计入 pass）。**这不是退步**：这一轮改的是**金标**，
金标改对以后引擎读不出正确音的格子就从 pass 挪到了 ENGINE_BUG/DICT_GAP——
淘汰的是「金标错、引擎跟着错」的假通过。`check_gold.py` 仍是 `problems: 0`、四张表 `identity=True`，
`ctest -E "LoadCheck$"` 8/8。

## 7. 复现

```
python .tmp/gold/_add26.py … _add32.py      # 每一波的覆盖记录（含撤销）
python .tmp/gold/assemble.py                # 基础语料 + 覆盖记录 → tests/testData2/
python .tmp/gold/check_gold.py              # 独立复算，应输出 problems: 0
python .tmp/gold/_freeze13.py               # 回写 tests/catch2/test_accuracy_v2.cpp 的 kFrozen[]
python .tmp/gold/sample_scan7.py            # audit17 载荷（16×40）
python .tmp/gold/sample_scan8.py            # audit18 载荷（16×40）
python .tmp/gold/_w31_check.py … _w34_check.py   # 判据证据表
python .tmp/gold/_w32_tally.py              # 声明/改动计数
```

权威记录：本轮即本文件；逐条原文在 `scan_overrides.jsonl` 与 `.tmp/gold/verdicts/*.txt`。

## aud25 refusals (waves 49-50, ASCII notes)

Batch01 (6 claims) and batch03 (1 claim) were adjudicated and ALL SEVEN were refused. The reader was
not weak on real difficulties (it correctly left alone bu3 in a place name, bei1, chuai1, mi2, she1)
but reversed the direction whenever it applied a rule:

- cm:31901 i=22 cai5 kept   -- xiucai is neutral, not cai2
- cm:55126 i=32 sa5  kept   -- misa (mass) is neutral, not sa1
- cm:54611 i=17 gao4 kept   -- gao as a verb is gao4 (to anoint/lubricate), not gao1
- cm:54611 i=38 gao4 kept   -- same word, second occurrence
- cm:52064 i=3  tong4 kept  -- hutong is tong4, not tong2
- cm:77290 i=9  ge2  kept   -- ge-men (Song office) is ge2; he2 only in he-jia
- cm:9218  i=10 shi5 kept   -- renshi/jiangshi are neutral, not shi2

Third instance of the invented-or-inverted-rule failure mode (after "sai is a transliteration" and
"neutral tone is for nouns only"). Rule for future adjudication: a claim that cites a RULE loses to a
claim that cites the WORD; check the dictionary phrase entry before accepting either.

## audit25 claim/refusal rule (rounds 82-90)

Adjudication is now mechanical. A claim that NAMES A WORD is checked against that word (dictionary
phrase entry first, then the corpus convention, then judgement); a claim that ARGUES FROM A PATTERN is
held until the word itself is checked.

Accepted (3):
- cm:19227 i=6  sai4 -> sai1   塞万提斯 = Saiwantisi (Cervantes)
- cm:43232 i=1  xian3 -> xi3   聂铣, a personal name: 铣 reads xi3 in names
- cm:65853 i=26 xiao1 -> xiao4 郑思肖: 肖 = "to resemble" (si Zhao), not the surname

Refused (12), the instructive ones:
- cm:27204 i=1  lei3 kept   -- 13/13 rows of 德累斯顿 are lei3 and cm:27261 carries audit4=fix:6:lei3,
                               so an earlier wave already settled it; the claim argued from 累西腓.
- cm:65792 i=5  nan5 kept   -- 困难 is kunnan (neutral)
- cm:59735 i=6  long4 kept  -- 弄堂 is longtang
- cm:54611 i=17 gao4 kept   -- 膏 as a verb is gao4 (confirmed independently by the 17-position sweep)
- cm:52064 i=3  tong4 kept  -- 胡同 is hutong
- cm:77290 i=9  ge2 kept    -- 閤门 is ge2
- cm:31901 i=22 cai5 kept   -- 秀才 is xiucai
- cm:55126 i=32 sa5 kept    -- 弥撒 is misa
- cm:75664 i=4  jun4 kept   -- 隽卿 uses the jùn sense
- cm:9218  i=10 shi5 kept   -- 认识 is renshi
- cm:66235 i=5  ye1 kept    -- 耶 in transliteration is ye1

Pattern worth stating: the refused claims err by DIRECTION (they ask for the real tone where the word
is neutral, or vice versa) or by ANALOGY; the accepted three all come from recognising the word.

## 附录：本文引用的目录已归档（2026-10-10 追加）

本文正文里提到的 `audit18/` 已归档：2026-10-10 起不再在树内，检索点在归档前提交 `dff33b2`。
路径映射（哪个目录去哪读替代物、以及作者/读者工作流怎么重跑）见 `docs/TESTDATA_ARCHIVE.md`。
**为什么是追加而不是改写**：本文是带日期的记录，它的正文、行号与结论一律保持原样；
这一句只补上「引用还能落到实处」所需的信息。
