# 第四轮：定向修复波（2026-09-29）

第三轮抽样是**为了测量**（分层代表性抽样，2.05% 这个残留率就是它给的）。这一轮反过来，**只挑第三轮发现的最烂的类**，目的是修，不是测——“这一轮的发现率”**绝不能**当语料级估计用。

选择依据是第三轮**实测的各类产出率**（`audit_yield.py`）：

| 类 | 第三轮发现/审计 | 产出率 | 第四轮剩余量 | 第四轮取 |
|---|---:|---:|---:|---:|
| cm dg | 22.1% | | 1281 | 300 |
| cm eb | 5.5% | | 4717 | 200 |
| cm ao | 4.7% | | 277 | 200 |
| ct dg | 3.7% | | 149 | 149 |
| ct eb | 2.0% | | 1099 | 150 |
| cm nf | 2.3% | | 844 | 0 |
| ct df | 0.3% | | 1348 | 0 |
| cm df | 0.0% | | 653 | 0 |

`dg` = 金标收了一个**词典里没有**的读音（第三轮发现这里最烂：有相当一部分读音是审音员自己发明的）；`eb` = 金标把引擎判错的行；`nf` = 两轮都只扫过一遍的行。

- 本轮抽样 **999** 行，**0 漏行**（每条抽样行都有回写）
- 报告 **76** 条（44 组）：fix 53、widen 23、驳回 0
- 改金标 **67 行**（另含 1 处对第三轮裁定的**修订**）

## 这一轮反复出现的形态：同一个词在同一句里两个读音

第三轮的修复只落在审计员报的那一个位置上，同一个词/人名/地名的**另一处没跟着改**，于是行内自相矛盾（都是两轮复核没发现的）。审计员逐句读的时候一眼就能看到，所以这一类在第四轮集中爆发：

- **幢**：量词 zhuàng，同句另一处已 zhuang4
- **高句丽**：gōu，同句的第二次出现仍是 ju4
- **隗**：同一姓氏在句中出现两次，一次 kui2 一次 wei3
- **覃**：姓氏两读，同一行只给了一个
- **枇杷**：60 行已按 pa2/pa5 并存，个别行只写 pa5
- **僮族**：1965 年改称壮族前的写法，读 zhuàng
- **番禺**：Pānyú，同句第一次出现已 pan1
- **赵衰**：人名读 Cuī
- **冒顿**：匈奴单于 Mòdú
- **谷蠡**：匈奴王号 lùlí
- **歌圩**：圩＝墟，读 xū
- **冯翊**：汉代三辅 píng yì
- **万俟**：复姓 Mòqí（万俟卨）
- **骠骑**：官名 piào qí
- **德累斯顿**：Dé lěi sī dùn，金标把两个**错读**都收进了 a/b

还抓到两个**两轮复核都扫过去了**的单音字错误（正是校准注入指出的薄弱面）：`今` 写成 jin2（今只有 jīn）、`諲`（吕諲）写成 ji1（该字只有 yīn，**金标要求的读音这个字根本没有**）。

## 裁定政策

与第三轮同一套（fix 只用事实、两读并存用 widen、语料错字 exclude、判错则驳回），并且**同一批内的同类必须一致**——这也迫使我**修订一条第三轮的裁定**：

- `cm:59237`：第三轮把 杷 定为单读 `pa2`；本轮改为 `pa2/pa5`（widen）。理由：revised: round 3 fixed 枇杷 to the standard pá, but 60 rows accept pa2/pa5 and colloquial speech uses the neutral form -- keeping one row strict while others are lenient made the gold contradict itself

## 全部改动（逐条，含理由）

| 数据集 | 行 | 位置 | 原金标 | 改为 | 动作 | 理由 |
|---|---|---:|---|---|---|---|
| cm | `cm:59237` | 3 | pa2 | pa2/pa5 | widen (修订) | revised: round 3 fixed 枇杷 to the standard pá, but 60 rows accept pa2/pa5 and colloquial speech uses the neutral form -- keeping one row strict while others are lenient made the gold contradict itself |
| cm | `cm:59247` | 15 | pa5 | pa2 | widen | 枇杷 is pípá with the neutral form used in speech; 60 rows already accept both |
| cm | `cm:59247` | 20 | pa5 | pa2 | widen | 枇杷 is pípá with the neutral form used in speech; 60 rows already accept both |
| cm | `cm:42303` | 7 | jin2 | jin1 | fix | 今 has exactly one reading, jīn; the gold's second tone is simply a tone error that two passes skimmed past |
| cm | `cm:59228` | 11 | pa5 | pa2 | widen | 枇杷 is pípá with the neutral form used in speech; 60 rows already accept both |
| cm | `cm:72862` | 41 | dan4 | tan2 | fix | 澹台 is the compound surname Tántái |
| cm | `cm:75793` | 5 | tan2 | qin2/tan2 | widen | the surname 覃 has both qín and tán; the same name earlier in the row already accepts both |
| cm | `cm:70501` | 0 | jiang4 | jiang1 | fix | 将 as the 'be about to' adverb / object marker is jiāng; jiàng is 将领 |
| cm | `cm:75302` | 7 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:72927` | 7 | dan4 | tan2 | fix | 澹台 is the compound surname Tántái |
| cm | `cm:78999` | 7 | shuai1 | cui1 | fix | 赵衰 the Jin minister is Zhào Cuī |
| cm | `cm:52654` | 23 | wei2 | xu1 | fix | 歌圩 the song market: 圩 = 墟 (market) is xū |
| cm | `cm:76123` | 3 | shi2 | shi4 | fix | 莳 in this name is shì (shí is only 莳萝) |
| cm | `cm:73856` | 30 | qi1 | qi4 | fix | 妻 as the verb 'to give a daughter in marriage' is qì |
| cm | `cm:52167` | 13 | yu2 | she1 | fix | 畲话 the She language: 畲 is shē |
| cm | `cm:52142` | 16 | jiang4 | jiang1 | fix | 将 as the 'be about to' adverb / object marker is jiāng; jiàng is 将领 |
| cm | `cm:52142` | 30 | jiang4 | jiang1 | fix | 将 as the 'be about to' adverb / object marker is jiāng; jiàng is 将领 |
| cm | `cm:78976` | 4 | kui2 | kui2/wei3 | widen | 隗 is a surname with kuí and wěi both attested |
| cm | `cm:78472` | 3 | mao4 | mo4 | fix | 冒顿 the Xiongnu chanyu is Mòdú |
| cm | `cm:78472` | 4 | dun4 | du2 | fix | same name: 顿 is dú in 冒顿 |
| cm | `cm:79023` | 7 | kui2 | kui2/wei3 | widen | 隗 is a surname with kuí and wěi both attested |
| cm | `cm:75331` | 15 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:67247` | 17 | li3 | li2 | fix | 谷蠡 the Xiongnu title is lùlí; the row's other 谷蠡 says so |
| cm | `cm:59265` | 3 | pa5 | pa2 | widen | 枇杷 is pípá with the neutral form used in speech; 60 rows already accept both |
| cm | `cm:79042` | 25 | wei3 | kui2 | widen | same surname: keep both readings rather than pick one |
| cm | `cm:75234` | 16 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:75824` | 18 | tan2 | qin2/tan2 | widen | the surname 覃 has both qín and tán; the same name earlier in the row already accepts both |
| cm | `cm:75824` | 33 | tan2 | qin2/tan2 | widen | the surname 覃 has both qín and tán; the same name earlier in the row already accepts both |
| cm | `cm:79092` | 19 | biao1 | piao4 | fix | 骠骑 骠骑大将军: 骠 is piào here; biāo is 黄骠马 |
| cm | `cm:70483` | 6 | la1 | la1/la5 | widen | 啦 is both the interjection lā and the neutral particle la; the row uses both |
| cm | `cm:79108` | 5 | zhen4 | tian4/zhen4 | widen | 瑱 in a personal name is read tiàn or zhèn; the row already accepts both elsewhere |
| cm | `cm:73839` | 23 | cheng2 | cheng2/sheng4 | widen | 晟 is chéng in historical names and shèng in modern ones; 55 rows already accept both |
| cm | `cm:59255` | 4 | pa5 | pa2/pa5 | widen | same as the 杷 decision above: accept the standard and the neutral form |
| cm | `cm:78986` | 7 | wei3 | kui2 | widen | same surname: keep both readings rather than pick one |
| cm | `cm:59767` | 8 | chuang2 | zhuang4 | fix | 幢 as the classifier for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:17106` | 12 | jiang4 | jiang1 | fix | 将 as the 'be about to' adverb / object marker is jiāng; jiàng is 将领 |
| cm | `cm:75289` | 23 | zhao1 | chao2 | fix | 明朝 the Ming dynasty: 朝 is cháo; zhāo is 'morning' |
| cm | `cm:75289` | 27 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:78610` | 2 | liao4 | liao3 | fix | 钌 the element ruthenium is liǎo; liào is 钌铞儿 |
| cm | `cm:69695` | 1 | ge5 | ge4 | fix | 个 the classifier is gè; unlike 啦 it has no separate neutral entry, and the row's 整个/逐个 are ge4 |
| cm | `cm:46589` | 15 | hang2 | xing2 | fix | 一行 (a party of travellers) is yī xíng, not yī háng |
| cm | `cm:46589` | 14 | yi4 | yi1 | fix | 一 keeps its base tone: this engine applies no sandhi by requirement, and the same row's other 一 is yi1 |
| cm | `cm:59704` | 1 | chuang2 | zhuang4 | fix | 幢 as the classifier for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:59827` | 27 | chuang2 | zhuang4 | fix | 幢 as the classifier for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:78616` | 27 | liao4 | liao3 | fix | 钌 the element ruthenium is liǎo; liào is 钌铞儿 |
| cm | `cm:55392` | 35 | gao3 | hao4 | fix | 镐房/丰镐 take the Western-Zhou capital reading hào; gǎo is the pickaxe |
| cm | `cm:31580` | 19 | yi4 | yi1 | fix | 一 keeps its base tone: this engine applies no sandhi by requirement, and the same row's other 一 is yi1 |
| cm | `cm:51561` | 17 | fan1 | pan1 | fix | 番禺 is Pānyú; the same row's first 番禺 already says pan1 |
| ct | `ct:6067` | 7 | mi1 | mi4 | fix | 媽咪 the loanword is maa1 mi4; mi1 is not a reading of 咪 |
| cm | `cm:65448` | 17 | a1 | he1 | widen | 呵 is hē and, as a variant of 啊, the particle a; the row's vocative 呵 already says he1, so accept both |
| cm | `cm:79105` | 1 | ji1 | yin1 | fix | 諲 has only yīn (於巾切); ji1 is not a reading of the character at all -- the worst kind of gold defect |
| cm | `cm:78972` | 8 | kui2 | wei3 | widen | 郭隗/叔隗: the surname reads wěi or kuí; accept both |
| cm | `cm:76406` | 1 | wai3 | wai3/wei1 | widen | 崴 is wǎi and wēi; the same name later in the row already accepts both |
| cm | `cm:75244` | 6 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:73815` | 10 | jiang1 | jiang4 | fix | 将作 the office is jiàngzuò (将作大匠), not jiāng |
| cm | `cm:73898` | 36 | cheng2 | sheng4 | widen | 李晟 the Tang general is Lǐ Shèng; accept both, as 55 other rows do |
| cm | `cm:79083` | 16 | feng2 | ping2 | fix | 冯翊 the commandery is píng yì (冯 = 凭), not the surname reading |
| cm | `cm:59084` | 5 | biao1 | piao4 | fix | 骠骑 骠骑大将军: 骠 is piào here; biāo is 黄骠马 |
| cm | `cm:20731` | 25 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the row's other 高句丽 already says gou1 |
| cm | `cm:75798` | 14 | tan2 | qin2/tan2 | widen | the surname 覃 has both qín and tán; the same name earlier in the row already accepts both |
| cm | `cm:75798` | 33 | tan2 | qin2/tan2 | widen | the surname 覃 has both qín and tán; the same name earlier in the row already accepts both |
| cm | `cm:52742` | 2 | ao1 | wa1 | fix | 凹 in South-Western place names (黑土凹) is wā; āo is the 'hollow' reading, and the same name later in the row is wa1 |
| cm | `cm:74479` | 11 | tong2 | zhuang4 | fix | 僮族 the former name of 壮族 is zhuàng; tóng is 僮仆 |
| cm | `cm:52657` | 16 | wei2 | xu1 | fix | 歌圩 the song market: 圩 = 墟 (market) is xū |
| cm | `cm:77136` | 2 | o1 | wo1 | fix | 喹喔啉 is kuí wō lín; the row's other 喔 says wo1 |
| cm | `cm:79039` | 15 | kui2 | wei3 | widen | 郭隗/叔隗: the surname reads wěi or kuí; accept both |
| cm | `cm:79039` | 16 | qi1 | qi4 | fix | 妻 as the verb 'to give a daughter in marriage' is qì |
| cm | `cm:79039` | 18 | shuai1 | cui1 | fix | 赵衰 the Jin minister is Zhào Cuī |
| cm | `cm:75310` | 8 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |
| cm | `cm:27261` | 6 | lei2/lei4 | lei3 | fix | 德累斯顿 is Dé lěi sī dùn; both readings the gold accepted for 累 are wrong here |
| cm | `cm:20786` | 8 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the row's other 高句丽 already says gou1 |
| cm | `cm:74488` | 13 | tong2 | zhuang4 | fix | 僮族 the former name of 壮族 is zhuàng; tóng is 僮仆 |
| cm | `cm:51922` | 0 | sai1 | sai4 | fix | 塞 in transliterated foreign place names is sài |
| cm | `cm:51922` | 25 | sai1 | sai4 | fix | 塞 in transliterated foreign place names is sài |
| cm | `cm:59253` | 11 | pa5 | pa2 | widen | 枇杷 is pípá with the neutral form used in speech; 60 rows already accept both |
| cm | `cm:58205` | 26 | chou2 | qiu2 | fix | 仇 as a surname is qiú; the row's other 仇殷 says qiu2 |
| cm | `cm:75249` | 3 | wan4 | mo4 | fix | 万俟 the surname is Mòqí (万俟卨, 万俟丑奴); wàn is the ordinary reading of 万, not this one |

## 这一轮暴露的三个流水线缺陷（已修）

1. **批次命名与派发名不一致**：`build_audit2.py` 用“该批第一行属于哪个语料”命名批次，而我派发时按 `au2_cm_001…` 顺序编号，于是 23 个批次里有 6 个名字对不上、1 个（粤语 004）被审计员正确地拒绝了（不猜、不乱写），另外 5 个被审计员写成两份。已把所有输出对回 payload 名，其中的重复副本按字节相等删除；**同一批被两位审计员独立审过**的那一批按并集合并（正是第一轮/第二轮的协议）。
2. **混合批次让 `ds` 标错**：`merge_audit.py` 原来用“批次名里的语料”给行打 `ds`，混合批次会把普通话行打成粤语行，连带**用错声调范围做形式校验**（普通话 1–5 vs 粤语 1–6）。现在按**行 id 自己的前缀**取语料。
3. **覆盖层把审计员的 `a/b` 当作一个字符串**：`金色[3] = {"pa2"} ∪ {"pa2/pa5"}` 会写出病态单元格 `pa2/pa2/pa5`；而原来的合法性判据逐 token 检查，每个 token 都合法，所以**查不出来**（只有 `find_mismatch.py` 的状态恒等式先炸了才发现）。现在按 `/` 拆开再合并，并且判据新增“单元格内读音不得重复、不得嵌套”这条，它立刻又抓出 543 处**派生集**的重复：`cantonese_untone` 由带调集去调而来，`mou4/mou5` 去调后成了 `mou/mou`，既冗余又让该行被算成“接受多个读音”（虚高 `ALT_OK`）。现在去调时同时去重，`cantonese_untone` 的 `ALT_OK` 从 673 降到 220。

## 局限（必须与结论一起读）

- **不是测量**：本轮的采样刻意偏向最烂的类，发现率不能外推。
- **盲性更弱**：第三轮的提示词禁止查第三方工具，但**没有禁止**审计员翻仓库自己的台账（`audit_findings.jsonl`/`out2`/`out3`/`adjudicate`）；本轮多个审计员**确实**这么做了，并用它来核对“行内自相矛盾”。因此本轮的发现**不能**与旧台账算作互相独立的证据——它们读句子本身是独立的，但“与旧结论一致”这件事本身不构成独立确认。
- **仍未覆盖**：金标 16,883 行里还有 12,904 行没经过任何一轮审计（两轮复核仍然是它们唯一的证据）。
## 复现

```
python tools/gold/build_audit2.py         # 定向抽样（排除已审计行）
# 23 个子代理写 audit2/out/*.jsonl（每批 45 行）
$env:AUDIT_DIR="audit2"; python tools/gold/merge_audit.py
python tools/gold/fix_audit2_names.py   # 只在本轮用过一次（名字对不上）
python tools/gold/audit_apply2.py       # 裁定表 → audit2_overrides.jsonl
python tools/gold/assemble.py; python tools/gold/check_gold.py
```

## 附录：本文引用的目录已归档（2026-10-10 追加）

本文正文里提到的 `audit2/` 已归档：2026-10-10 起不再在树内，检索点在归档前提交 `dff33b2`。
路径映射（哪个目录去哪读替代物、以及作者/读者工作流怎么重跑）见 `docs/TESTDATA_ARCHIVE.md`。
**为什么是追加而不是改写**：本文是带日期的记录，它的正文、行号与结论一律保持原样；
这一句只补上「引用还能落到实处」所需的信息。
