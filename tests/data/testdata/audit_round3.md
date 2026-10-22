# 第三轮：对**金标本身**的独立审计（2026-09-28）

前两轮复核的是**引擎**：审音员看“这一行引擎读得对不对”。这一轮反过来，审计的是**已经写进 `tests/testData2/` 的金标**：载荷里**不给引擎输出**，审音员独立逐位置读一遍整句，再与金标比对，只报“我有把握金标写错了”的位置。
它的意义是把“两轮复核残留多少错”从**估计**变成**实测**。

- 抽样行数：**2980**（金标 16883 行中的 17.7%），按“证据强弱”分层
- 覆盖：**每一条抽样行都有回写**（无漏行、无未答）
- 报告结果：**79 条**（2.65% 的行被判有一处可疑），归并成 64 组
- 裁定后：**改金标 61 行**（fix 52、widen 12、narrow 1、exclude 1），驳回 13 条

**残留错误率（实测）**：抽样里 **2.05% 的行至少有一处金标缺陷**——52 处是“金标写的读音不对”，12 处是“金标只收了一个读音、实际两读并存”，1 处是“金标多收了一个不合语境的读音”，1 处是语料本身写错字（转 `EXCLUDED`）。
这比校准注入推出的“约 4–5%”要低（校准测的是**注入型**错误，比自然错误更难发现），但它仍是**抽样估计**：只覆盖 17.7% 的行，未审计部分的真实残留率无法直接测量。方向仍然是“把错的当成对的”，即准确率被轻微高估。

## 抽样设计

| 数据集 | 证据最弱（两轮都没发现问题） | DATA_FIX | ALT_OK | ENGINE_BUG | DICT_GAP | 合计 |
|---|---:|---:|---:|---:|---:|---:|
| mandarin_ctx | 700 | 300 | 150 | 250 | 120 | 1520 |
| mandarin_untone | 250 | 20 | 8 | 50 | 2 | 330 |
| cantonese_tone | 500 | 300 | 150 | 100 | 80 | 1130 |

`nf` = 两轮都只扫了一遍、什么都没报的行（金标 = 引擎，证据最弱）；其余按状态标记分层。

## 裁定政策（故意不对称）

- **fix**：主张是“事实”——该字没有这个读音，或固定词/人名的读音已定。改错比放宽更糟，所以只在可核验时使用；多数还能被**行内自证**：同一句里同一个词已按正确读音标注（例如“两幢…一幢…”、两处“高句丽”、两处“缊纥提”）。
- **widen**：两个读音对该字都成立，差别只在语体/记法/粤语變調 → 写成 `a/b`，不替语境拍板，行仍留在分母里。
- **narrow**：金标多收的读音在本语境里任何语体都不成立（前一轮放宽过头）→ 收窄。
- **exclude**：语料本身写错字 → 整行转 `EXCLUDED`，不参与任何统计。
- **reject**：审计员判断有误，或提议的“修改”与金标现值相同/下标越界。

## 全部改动（逐条，含理由）

| 数据集 | 行 | 位置 | 原金标 | 改为 | 动作 | 理由 |
|---|---|---:|---|---|---|---|
| cm | `cm:19479` | 18 | ye5 | ye2 | fix | 灶王爷 zàowángyé keeps the full tone |
| cm | `cm:60645` | 9 | er5 | er2 | fix | 孩儿 hái'ér keeps a full syllable; the corpus itself writes 女儿/婴儿/孤儿 as er2 |
| cm | `cm:71851` | 25 | zuo2 | zhuo2 | fix | 琢磨 zhuómó = to study deeply, not zuómo = to ponder |
| cm | `cm:23635` | 25 | chang2 | zhang3 | fix | 台长 tái zhǎng (head of a station) |
| cm | `cm:78377` | 13 | ou4 | ou1 | fix | 沤寓 takes 浮沤 (bubble) = ōu; same sentence again |
| cm | `cm:20834` | 26 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the same sentence already uses gōu |
| cm | `cm:79020` | 17 | kui2 | wei3 | widen | 隗 is a surname with both kuí and wěi attested |
| cm | `cm:59383` | 10 | yi2 | yi1 | fix | 始终如一 keeps the base tone: this engine applies no tone sandhi by requirement |
| cm | `cm:22907` | 17 | jiang4 | jiang1 | fix | 将 as the preposition 'taking' is jiāng |
| cm | `cm:78597` | 27 | yun1 | yun4 | fix | 缊纥提: the row itself uses yùn for the same name |
| cm | `cm:73806` | 7 | ju1 | ju2 | fix | 锔 the element is jú; the row's 氯化锔 already says ju2 |
| cm | `cm:78947` | 1 | kui2 | wei3 | widen | 隗 is a surname with both kuí and wěi attested |
| cm | `cm:76233` | 27 | duo3 | duo4 | fix | 柴垛 = a stack of firewood is duò; the row's 草垛 says duo4 |
| cm | `cm:73342` | 17 | huan2 | yuan2 | fix | 圜 in 韩圜 (the currency) is yuán; the row says so itself |
| cm | `cm:47305` | 15 | xiang1 | xiang4 | fix | 三相电动机 = three-phase, xiàng |
| cm | `cm:61625` | 11 | mi2 | ni3 | fix | 祢 as the divine 'Thou' is nǐ; mí is the surname 祢衡 |
| cm | `cm:20739` | 25 | zi5 | zi3 | fix | 老子 the person is Lǎozǐ |
| cm | `cm:55321` | 13 | xue1 | xiao1 | fix | 刮削器 the tool is xiāo (cutting); the corpus says so |
| cm | `cm:68617` | 11 | er5 | er2 | fix | 孩儿 hái'ér keeps a full syllable; the corpus itself writes 女儿/婴儿/孤儿 as er2 |
| cm | `cm:68617` | 34 | er5 | er2 | fix | 孩儿 hái'ér keeps a full syllable; the corpus itself writes 女儿/婴儿/孤儿 as er2 |
| cm | `cm:56072` | 2 | lang2 | lang4 | fix | 阆 is làng (阆苑, 阆中) |
| cm | `cm:67387` | 15 | hang2 | heng2 | fix | 珩 as a jade pendant in a name is héng |
| cm | `cm:75856` | 5 | qin2 | tan2 | widen | 覃 has tán and qín; the title 覃王 is read either way in practice, so both stay acceptable |
| cm | `cm:38604` | 2 | gao3 | hao4 | fix | 镐 in a personal name (镐京) is hào; gǎo is the pickaxe |
| cm | `cm:67316` | 4 | gu3 | lu4 | fix | 谷蠡王 the Xiongnu title is lù |
| cm | `cm:27237` | 5 | lei3 | lei2 | fix | 累官 = repeatedly promoted, léi |
| cm | `cm:51925` | 3 | sai1 | sai4 | fix | 塞 in transliterated foreign place names is sài |
| cm | `cm:51925` | 25 | sai1 | sai4 | fix | 塞 in transliterated foreign place names is sài |
| cm | `cm:59706` | 22 | chuang2 | zhuang4 | fix | 幢 as a measure word for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:62934` | 24 | huo5 | he2 | fix | 气候温和 is wēnhé; wēn huo is dialectal |
| cm | `cm:74267` | 7 | zhu4 | zhuo2 | fix | 著 here means 着 (to wear), zhuó |
| cm | `cm:70764` | 32 | man4 | man2 | fix | 谩骂 is mán mà |
| cm | `cm:73300` | 25 | ge2 | li4 | fix | 鬲 the bronze vessel is lì; the row's other 鬲 says li4 |
| cm | `cm:59784` | 27 | chuang2 | zhuang4 | fix | 幢 as a measure word for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:59784` | 33 | chuang2 | zhuang4 | fix | 幢 as a measure word for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:74463` | 4 | tong2 | zhuang4 | fix | 僮族 the old name of 壮族 is zhuàng; the row says so |
| cm | `cm:51373` | 5 | zhong4 | zhong1 | fix | 高中 = senior high school is gāo zhōng |
| cm | `cm:73881` | 23 | shao3 | shao4 | fix | 少卿 the deputy post is shào; the corpus agrees elsewhere |
| cm | `cm:59237` | 3 | pa5 | pa2 | fix | 枇杷 is pípá; the row's own 枇杷属 says pa2/pa5 |
| cm | `cm:59723` | 8 | chuang2 | zhuang4 | fix | 幢 as a measure word for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:59723` | 17 | chuang2 | zhuang4 | fix | 幢 as a measure word for buildings is zhuàng; chuáng is 经幢 |
| cm | `cm:73934` | 1 | sheng4 | cheng2 | widen | 晟 is read chéng in historical names and shèng in modern ones; 55 other rows already accept both |
| cm | `cm:20749` | 10 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the same sentence already uses gōu |
| cm | `cm:20749` | 45 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the same sentence already uses gōu |
| cm | `cm:72865` | 8 | li5 | li2 | fix | 琉璃 is liúlí; the neutral tone belongs to 玻璃 |
| cm | `cm:463` | 34 | jiang4 | jiang1 | fix | 将 as the preposition 'taking' is jiāng |
| cm | `cm:51508` | 11 | fan1 | pan1 | fix | 番禺 is Pānyú; the row's first 番禺 already says pan1 |
| cm | `cm:48503` | 13 | dian4 | tian2 | fix | 青钿 (荷叶 like small coins) is tián; diàn is 螺钿 |
| cm | `cm:43248` | 7 | zi5 | zi3 | fix | 老子 the person is Lǎozǐ |
| cm | `cm:78367` | 0 | nang3 | nang2 | fix | 馕 the flatbread is náng; the row's 烤制馕 says nang2 |
| cm | `cm:71692` | 13 | sha1 | cha4/sha1 | widen | 刹 in transliteration is read chà or shā; the row's other 刹 keeps both |
| cm | `cm:74577` | 7 | qian4 | qian4/xi1 | widen | 茜 in a personal name is qiàn or xī; the row's other 茜 keeps both |
| cm | `cm:52635` | 35 | wei2 | xu1 | fix | 圩 in South-China place and market names is xū (吴圩, 圩日) |
| cm | `cm:78981` | 20 | kui2 | kui2/wei3 | widen | same: keep both surname readings |
| cm | `cm:20830` | 16 | ju4 | gou1 | fix | 高句丽 is Gāogōulí; the same sentence already uses gōu |
| ct | `ct:894` | 1 | pou2 | pou4 | exclude | the corpus wrote 莆 for 脯 (菜脯 coi3 pou2): the row demands a reading for a character it does not have |
| ct | `ct:4117` | 7 | taai3 | taai2 | widen | X太 the honorific takes the 變調 taai2; the base tone taai3 stays acceptable |
| ct | `ct:2154` | 6 | cak1 | caak1 | widen | 測 caak1 is the customary reading, cak1 the dictionary's variant; both are readings of the character |
| ct | `ct:1111` | 11 | jyun2 | jyun4 | widen | 園 as a plain noun takes jyun4 and the 變調 jyun2 occurs in compounds; keep both |
| ct | `ct:4706` | 12 | mut3 | mut6 | fix | 沫 is 明母末韻: 泡沫 is pou5 mut6, mut3 is not its reading |
| ct | `ct:5461` | 3 | zoek6 | zoek3 | fix | 穿著 (to wear) is zoek3; the corpus agrees |
| ct | `ct:2042` | 4 | dong1 | dong3 | fix | 當 in 當作 (to treat as) is dong3 |
| mu | `mu:11946` | 20 | bo | bao | fix | 剥开 is the colloquial bāo; bō is for 剥削/剥夺. The source corpus says bāo as well |
| mu | `mu:12480` | 4 | hai | hai/huan | widen | 还 in the classical line 还洁去 is read huán; the source corpus annotates huan, and sung readings are accepted alongside the modern hái |
| mu | `mu:12458` | 3 | ji | xi | widen | 系 (to tie) is xì in literary reading and jì in colloquial use; both stay acceptable |
| mu | `mu:5396` | 2 | ban/pan | ban | narrow | 般 in 如云般 (like a cloud) is bān; 般乐/般桓 does not apply here, so the extra reading goes |

## 被驳回的主张（逐条，含理由）

| 数据集 | 原金标 | 审计提议 | 驳回理由 |
|---|---|---|---|
| cm | kui2/wei3 | wei3 | already accepts wěi; narrowing adds nothing |
| cm | bian4 | bian4 | no-op: the gold already reads biàn |
| cm | xing2 | xing2 | no-op |
| cm | zhe5 | zhe5 | no-op |
| cm | None | shi4 | out of range: the reported position does not exist on that row (and the sentence has no such clause) |
| cm | pu2 | pu2 | no-op |
| cm | tie3 | tie3 | no-op |
| ct | zek1 | zit1 | 唧 as the final particle is zek1 in Cantonese; the dictionary not listing it is a gap, not an error |
| ct | aa3 | aa3 | no-op |
| ct | tong2/tong4 | tong4 | already accepts the base tone tong4 |
| mu | ta | tuo | 拓 in 拓碑/拓本 means to rub a copy and is tà; the gold's tà is right and the audit's own reasoning says so |

## 这一轮同时暴露的三个流水线缺陷（已修）

1. **`was=` 备注写错位置**：`DATA_FIX` 行备注里的“原标注值”取的是该行**第一个**分歧位置的值，可能属于另一个位置（例如 `埗`（深水埗）那行写着 `was=jin2`）。现在按**被覆盖的位置**逐条记录（`was=7:baak3`），影响 2,621 行。
2. **覆盖层之后没有重算引擎判定**：审计的 `widen` 把引擎自己的读音加回单元格后，行的状态仍停在 `ENGINE_BUG`，使 `decided == pass + ENGINE_BUG + DICT_GAP` 不成立（实测 2 行：`cm:73934` 晟、`mu:12458` 系）。改为在覆盖之后**从最终金标重算**。
3. **判据把 `EXCLUDED` 的跳过放在逐位置比较之后**：这一轮新增了一行“字数一致、但语料写错字”的 `EXCLUDED`（`ct:894`），它在旧写法下会被顺手判分。已把跳过移到比较之前，`check_gold.py` 同步（`EXCLUDED` 不进任何统计，连“恰好通过”也不算）。

## 缺陷计数的变化（必须解释清楚）

审计把 19 行从“通过”改成“引擎错”（`DICT_GAP` +17、`ENGINE_BUG` +2）：这些行原本是**金标错**（引擎照旧读它自己那个读音，于是被算成对），把金标改对之后，引擎才显出错。D16 的“`ENGINE_BUG`/`EXCLUDED` 只许减少”防的是“为了让数字好看而放水”，不是“不许发现新缺陷”；这里每一条增加都能在上表逐条对到位置、读音与理由。

## 复现

```
python tools/gold/build_audit.py      # 抽样 + 生成批次
# 子代理写 audit/out/*.jsonl（每行回写）
python tools/gold/merge_audit.py      # 归并 + 形式校验
python tools/gold/audit_apply.py     # 裁定表 → audit_overrides.jsonl
python tools/gold/assemble.py && python tools/gold/check_gold.py
```
