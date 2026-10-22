# 引擎错误簇清单（由金标集实证）

数据来源：`tests/verified/`（双轮独立复核 + 分歧三审）与 `.tmp/corpus/defects.jsonl`。
每个条目是一个**簇**：同一个字、同一组「引擎读音 → 正确读音」的全部错误位置合并计数，
因为一次词组表/词典修正通常能消掉整簇。

- 错误位置总数：**10879**（ENGINE_BUG 8553：词典里有正确读音、算法选错；DICT_GAP 2326：词典里根本没有该读音）
- 簇数：**978**
- 状态含义见 `docs/testdata/decisions.md`；ENGINE_BUG 修算法，DICT_GAP 补数据。

| # | 字 | 引擎 | 正确 | 类别 | 次数 | 语料 |
|---|---|---|---|---|---|---|
| 1 | 将 | jiang4 | jiang1 | ENGINE_BUG | 357 | mandarin_tone×357 |
| 2 | 子 | zi5 | zi3 | ENGINE_BUG | 213 | mandarin_tone×213 |
| 3 | 阆 | lang2 | lang4 | ENGINE_BUG | 197 | mandarin_tone×197 |
| 4 | 粘 | zhan1 | nian2 | ENGINE_BUG | 189 | mandarin_tone×189 |
| 5 | 更 | geng1 | geng4 | ENGINE_BUG | 175 | mandarin_tone×175 |
| 6 | 什 | shen2 | shi2 | ENGINE_BUG | 173 | mandarin_tone×173 |
| 7 | 艮 | gen3 | gen4 | ENGINE_BUG | 168 | mandarin_tone×168 |
| 8 | 镐 | gao3 | hao4 | DICT_GAP | 159 | mandarin_tone×159 |
| 9 | 贲 | bi4 | ben1 | ENGINE_BUG | 154 | mandarin_tone×154 |
| 10 | 卷 | juan3 | juan4 | ENGINE_BUG | 146 | mandarin_tone×146 |
| 11 | 吗 | ma3 | ma5 | ENGINE_BUG | 143 | mandarin_tone×143 |
| 12 | 幢 | chuang2 | zhuang4 | DICT_GAP | 141 | mandarin_tone×141 |
| 13 | 塞 | sai1 | sai4 | ENGINE_BUG | 139 | mandarin_tone×139 |
| 14 | 为 | wei2 | wei4 | ENGINE_BUG | 127 | mandarin_tone×127 |
| 15 | 攒 | zan3 | cuan2 | ENGINE_BUG | 127 | mandarin_tone×127 |
| 16 | 陂 | pi2 | bei1 | ENGINE_BUG | 125 | mandarin_tone×125 |
| 17 | 当 | dang4 | dang1 | ENGINE_BUG | 124 | mandarin_tone×124 |
| 18 | 肖 | xiao4 | xiao1 | ENGINE_BUG | 121 | mandarin_tone×121 |
| 19 | 靡 | mi3 | mi2 | ENGINE_BUG | 118 | mandarin_tone×118 |
| 20 | 嗎 | maa1 | maa3 | ENGINE_BUG | 97 | cantonese_tone×97 |
| 21 | 跌 | tit3 | dit3 | ENGINE_BUG | 97 | cantonese_tone×97 |
| 22 | 地 | de5 | di4 | ENGINE_BUG | 96 | mandarin_tone×96 |
| 23 | 铣 | xi3 | xian3 | ENGINE_BUG | 94 | mandarin_tone×94 |
| 24 | 隽 | juan4 | jun4 | ENGINE_BUG | 91 | mandarin_tone×91 |
| 25 | 都 | dou1 | du1 | ENGINE_BUG | 83 | mandarin_tone×83 |
| 26 | 地 | dei6 | dei2 | ENGINE_BUG | 83 | cantonese_tone×83 |
| 27 | 到 | dou3 | dou2 | ENGINE_BUG | 83 | cantonese_tone×83 |
| 28 | 只 | zhi1 | zhi3 | ENGINE_BUG | 81 | mandarin_tone×81 |
| 29 | 了 | le1 | le5 | ENGINE_BUG | 80 | mandarin_tone×80 |
| 30 | 畲 | yu2 | she1 | ENGINE_BUG | 80 | mandarin_tone×80 |
| 31 | 率 | lv4 | shuai4 | ENGINE_BUG | 76 | mandarin_tone×76 |
| 32 | 啊 | a1 | a5 | DICT_GAP | 75 | mandarin_tone×75 |
| 33 | 晟 | cheng2 | sheng4 | DICT_GAP | 70 | mandarin_tone×70 |
| 34 | 裨 | bi4 | pi2 | DICT_GAP | 68 | mandarin_tone×68 |
| 35 | 吧 | ba1 | ba5 | ENGINE_BUG | 67 | mandarin_tone×67 |
| 36 | 子 | zi5 | zi3 | DICT_GAP | 64 | mandarin_tone×64 |
| 37 | 姥 | lao3 | mu3 | ENGINE_BUG | 63 | mandarin_tone×63 |
| 38 | 園 | jyun4 | jyun2 | ENGINE_BUG | 62 | cantonese_tone×62 |
| 39 | 铺 | pu1 | pu4 | ENGINE_BUG | 60 | mandarin_tone×60 |
| 40 | 茜 | qian4 | xi1 | ENGINE_BUG | 60 | mandarin_tone×60 |
| 41 | 藏 | cang2 | zang4 | ENGINE_BUG | 59 | mandarin_tone×59 |
| 42 | 句 | ju4 | gou1 | DICT_GAP | 58 | mandarin_tone×58 |
| 43 | 埗 | bou6 | bou2 | DICT_GAP | 58 | cantonese_tone×58 |
| 44 | 朝 | zhao1 | chao2 | ENGINE_BUG | 57 | mandarin_tone×57 |
| 45 | 丽 | li4 | li2 | DICT_GAP | 56 | mandarin_tone×56 |
| 46 | 朴 | pu3 | piao2 | ENGINE_BUG | 54 | mandarin_tone×54 |
| 47 | 隗 | kui2 | wei3 | DICT_GAP | 54 | mandarin_tone×54 |
| 48 | 莘 | shen1 | xin1 | ENGINE_BUG | 53 | mandarin_tone×53 |
| 49 | 将 | jiang4 | jiang1 | DICT_GAP | 53 | mandarin_tone×53 |
| 50 | 行 | hang4 | haang4 | ENGINE_BUG | 53 | cantonese_tone×53 |
| 51 | 儿 | er5 | er2 | ENGINE_BUG | 51 | mandarin_tone×51 |
| 52 | 哩 | li1 | li3 | ENGINE_BUG | 50 | mandarin_tone×50 |
| 53 | 會 | wui5 | wui2 | ENGINE_BUG | 49 | cantonese_tone×49 |
| 54 | 了 | liao3 | le5 | ENGINE_BUG | 48 | mandarin_tone×48 |
| 55 | 燕 | yan4 | yan1 | ENGINE_BUG | 48 | mandarin_tone×48 |
| 56 | 僮 | tong2 | zhuang4 | DICT_GAP | 48 | mandarin_tone×48 |
| 57 | 呀 | jaa1 | aa4 | DICT_GAP | 48 | cantonese_tone×48 |
| 58 | 湮 | yin1 | yan1 | ENGINE_BUG | 47 | mandarin_tone×47 |
| 59 | 钌 | liao4 | liao3 | DICT_GAP | 47 | mandarin_tone×47 |
| 60 | 斗 | dou4 | dou3 | ENGINE_BUG | 46 | mandarin_tone×46 |

## 最大簇说明

- **将 jiang4 → jiang1**（357 次，ENGINE_BUG）：词典候选 ['jiang4', 'jiang1', 'qiang1']；例：cm:100(位置29)、cm:463(位置34)、cm:1761(位置4)、cm:2958(位置20)
- **子 zi5 → zi3**（213 次，ENGINE_BUG）：词典候选 ['zi5', 'zi3', 'zi4']；例：cm:518(位置12)、cm:2953(位置9)、cm:2981(位置5)、cm:2981(位置16)
- **阆 lang2 → lang4**（197 次，ENGINE_BUG）：词典候选 ['lang2', 'lang4']；例：cm:56059(位置7)、cm:56059(位置10)、cm:56060(位置0)、cm:56061(位置11)
- **粘 zhan1 → nian2**（189 次，ENGINE_BUG）：词典候选 ['zhan1', 'nian2']；例：cm:10896(位置3)、cm:15547(位置27)、cm:52753(位置12)、cm:61070(位置23)
- **更 geng1 → geng4**（175 次，ENGINE_BUG）：词典候选 ['geng1', 'geng4']；例：cm:752(位置25)、cm:2973(位置8)、cm:3514(位置38)、cm:3515(位置19)
- **什 shen2 → shi2**（173 次，ENGINE_BUG）：词典候选 ['shen2', 'shi2', 'shi5']；例：cm:5813(位置11)、cm:10145(位置2)、cm:10146(位置2)、cm:10147(位置1)
- **艮 gen3 → gen4**（168 次，ENGINE_BUG）：词典候选 ['gen4', 'gen3']；例：cm:5813(位置7)、cm:10234(位置9)、cm:19148(位置8)、cm:51779(位置7)
- **镐 gao3 → hao4**（159 次，DICT_GAP）：词典候选 ['gao3']；例：cm:38604(位置2)、cm:55255(位置1)、cm:55256(位置4)、cm:55257(位置1)
- **贲 bi4 → ben1**（154 次，ENGINE_BUG）：词典候选 ['bi4', 'ben1']；例：cm:56126(位置9)、cm:58013(位置14)、cm:58014(位置13)、cm:58015(位置2)
- **卷 juan3 → juan4**（146 次，ENGINE_BUG）：词典候选 ['juan3', 'juan4']；例：cm:2660(位置9)、cm:27678(位置13)、cm:32715(位置28)、cm:32715(位置32)
- **吗 ma3 → ma5**（143 次，ENGINE_BUG）：词典候选 ['ma3', 'ma5', 'ma2']；例：cm:30410(位置3)、cm:55235(位置27)、cm:59403(位置31)、cm:67351(位置9)
- **幢 chuang2 → zhuang4**（141 次，DICT_GAP）：词典候选 ['chuang2']；例：cm:5851(位置14)、cm:59686(位置5)、cm:59687(位置24)、cm:59688(位置6)
- **塞 sai1 → sai4**（139 次，ENGINE_BUG）：词典候选 ['sai1', 'se4', 'sai4']；例：cm:12136(位置3)、cm:15190(位置21)、cm:19132(位置27)、cm:19133(位置2)
- **为 wei2 → wei4**（127 次，ENGINE_BUG）：词典候选 ['wei2', 'wei4', 'wei5']；例：cm:752(位置18)、cm:1805(位置24)、cm:3421(位置5)、cm:3424(位置25)
- **攒 zan3 → cuan2**（127 次，ENGINE_BUG）：词典候选 ['zan3', 'cuan2']；例：cm:68147(位置12)、cm:68147(位置19)、cm:68148(位置18)、cm:68149(位置14)
- **陂 pi2 → bei1**（125 次，ENGINE_BUG）：词典候选 ['pi2', 'bei1', 'po1']；例：cm:64637(位置0)、cm:64639(位置14)、cm:64640(位置3)、cm:64642(位置1)
- **当 dang4 → dang1**（124 次，ENGINE_BUG）：词典候选 ['dang1', 'dang4', 'dang5']；例：cm:3524(位置23)、cm:6411(位置19)、cm:8293(位置8)、cm:9229(位置16)
- **肖 xiao4 → xiao1**（121 次，ENGINE_BUG）：词典候选 ['xiao4', 'xiao1']；例：cm:5081(位置0)、cm:27180(位置35)、cm:32873(位置3)、cm:43204(位置2)
- **靡 mi3 → mi2**（118 次，ENGINE_BUG）：词典候选 ['mi3', 'mi2']；例：cm:19604(位置1)、cm:19607(位置1)、cm:19607(位置17)、cm:19608(位置1)
- **嗎 maa1 → maa3**（97 次，ENGINE_BUG）：词典候选 ['maa1', 'maa3', 'ma3', 'maa5', 'maa4']；例：ct:17(位置18)、ct:18(位置19)、ct:76(位置17)、ct:186(位置18)
- **跌 tit3 → dit3**（97 次，ENGINE_BUG）：词典候选 ['dit3', 'tit3']；例：ct:58(位置16)、ct:119(位置15)、ct:131(位置15)、ct:167(位置16)
- **地 de5 → di4**（96 次，ENGINE_BUG）：词典候选 ['de5', 'di4', 'di5']；例：cm:1744(位置26)、cm:3668(位置11)、cm:5307(位置11)、cm:5321(位置14)
- **铣 xi3 → xian3**（94 次，ENGINE_BUG）：词典候选 ['xi3', 'xian3']；例：cm:43166(位置11)、cm:43167(位置11)、cm:43167(位置14)、cm:43168(位置14)
- **隽 juan4 → jun4**（91 次，ENGINE_BUG）：词典候选 ['juan4', 'jun4']；例：cm:75661(位置6)、cm:75662(位置15)、cm:75663(位置1)、cm:75664(位置4)
- **都 dou1 → du1**（83 次，ENGINE_BUG）：词典候选 ['dou1', 'du1']；例：cm:6526(位置8)、cm:6530(位置14)、cm:6537(位置8)、cm:6539(位置14)
- **地 dei6 → dei2**（83 次，ENGINE_BUG）：词典候选 ['dei6', 'dei2']；例：ct:5(位置7)、ct:13(位置2)、ct:95(位置7)、ct:281(位置6)
- **到 dou3 → dou2**（83 次，ENGINE_BUG）：词典候选 ['dou3', 'dou2']；例：ct:406(位置13)、ct:470(位置12)、ct:517(位置11)、ct:523(位置11)
- **只 zhi1 → zhi3**（81 次，ENGINE_BUG）：词典候选 ['zhi1', 'zhi3']；例：cm:639(位置28)、cm:3545(位置13)、cm:3645(位置26)、cm:5191(位置30)
- **了 le1 → le5**（80 次，ENGINE_BUG）：词典候选 ['le5', 'liao3']；例：cm:1(位置17)、cm:14(位置14)、cm:26(位置18)、cm:44(位置4)
- **畲 yu2 → she1**（80 次，ENGINE_BUG）：词典候选 ['yu2', 'she1']；例：cm:52088(位置0)、cm:52093(位置41)、cm:52095(位置5)、cm:52096(位置4)
- **率 lv4 → shuai4**（76 次，ENGINE_BUG）：词典候选 ['lv4', 'shuai4']；例：cm:6581(位置15)、cm:24213(位置2)、cm:25700(位置24)、cm:25706(位置21)
- **啊 a1 → a5**（75 次，DICT_GAP）：词典候选 ['a1']；例：cm:61561(位置6)、cm:61562(位置11)、cm:61563(位置9)、cm:61564(位置5)
- **晟 cheng2 → sheng4**（70 次，DICT_GAP）：词典候选 ['cheng2']；例：cm:73808(位置13)、cm:73809(位置33)、cm:73810(位置1)、cm:73811(位置20)
- **裨 bi4 → pi2**（68 次，DICT_GAP）：词典候选 ['bi4']；例：cm:59035(位置5)、cm:59036(位置5)、cm:59037(位置7)、cm:59038(位置1)
- **吧 ba1 → ba5**（67 次，ENGINE_BUG）：词典候选 ['ba1', 'ba5']；例：cm:30410(位置17)、cm:59286(位置5)、cm:59290(位置36)、cm:59291(位置20)
- **子 zi5 → zi3**（64 次，DICT_GAP）：词典候选 ['zi5', 'zi3', 'zi4']；例：cm:20739(位置25)、cm:20784(位置24)、cm:21403(位置3)、cm:21403(位置7)
- **姥 lao3 → mu3**（63 次，ENGINE_BUG）：词典候选 ['lao3', 'lao5', 'mu3']；例：cm:51944(位置3)、cm:51945(位置16)、cm:51946(位置1)、cm:51946(位置5)
- **園 jyun4 → jyun2**（62 次，ENGINE_BUG）：词典候选 ['jyun4', 'jyun2']；例：ct:8(位置13)、ct:62(位置14)、ct:178(位置13)、ct:185(位置12)
- **铺 pu1 → pu4**（60 次，ENGINE_BUG）：词典候选 ['pu1', 'pu4']；例：cm:6626(位置11)、cm:25796(位置30)、cm:31090(位置2)、cm:31091(位置9)
- **茜 qian4 → xi1**（60 次，ENGINE_BUG）：词典候选 ['qian4', 'xi1']；例：cm:15301(位置3)、cm:74563(位置8)、cm:74563(位置9)、cm:74564(位置19)
- **藏 cang2 → zang4**（59 次，ENGINE_BUG）：词典候选 ['cang2', 'zang4']；例：cm:10293(位置15)、cm:14150(位置3)、cm:14173(位置37)、cm:14186(位置22)
- **句 ju4 → gou1**（58 次，DICT_GAP）：词典候选 ['ju4']；例：cm:20725(位置25)、cm:20727(位置16)、cm:20731(位置7)、cm:20731(位置25)
- **埗 bou6 → bou2**（58 次，DICT_GAP）：词典候选 ['bou6']；例：ct:113(位置8)、ct:212(位置6)、ct:228(位置10)、ct:307(位置10)
- **朝 zhao1 → chao2**（57 次，ENGINE_BUG）：词典候选 ['chao2', 'zhao1']；例：cm:135(位置6)、cm:4327(位置1)、cm:6365(位置1)、cm:6376(位置14)
- **丽 li4 → li2**（56 次，DICT_GAP）：词典候选 ['li4', 'li2']；例：cm:20725(位置26)、cm:20727(位置17)、cm:20731(位置8)、cm:20731(位置26)
- **朴 pu3 → piao2**（54 次，ENGINE_BUG）：词典候选 ['pu3', 'po4', 'piao2', 'pu2']；例：cm:41823(位置11)、cm:41826(位置2)、cm:41836(位置24)、cm:41841(位置0)
- **隗 kui2 → wei3**（54 次，DICT_GAP）：词典候选 ['kui2']；例：cm:78938(位置9)、cm:78940(位置1)、cm:78941(位置9)、cm:78945(位置1)
- **莘 shen1 → xin1**（53 次，ENGINE_BUG）：词典候选 ['shen1', 'xin1']；例：cm:12137(位置31)、cm:55855(位置20)、cm:55860(位置32)、cm:55861(位置14)
- **将 jiang4 → jiang1**（53 次，DICT_GAP）：词典候选 ['jiang4', 'jiang1', 'qiang1']；例：cm:20739(位置0)、cm:20809(位置18)、cm:20831(位置30)、cm:23049(位置5)
- **行 hang4 → haang4**（53 次，ENGINE_BUG）：词典候选 ['hang4', 'haang4', 'hong4', 'hang6', 'hong2']；例：ct:88(位置20)、ct:142(位置19)、ct:233(位置20)、ct:287(位置5)
- **儿 er5 → er2**（51 次，ENGINE_BUG）：词典候选 ['er5', 'er2']；例：cm:100(位置6)、cm:16142(位置8)、cm:16142(位置36)、cm:16163(位置17)
- **哩 li1 → li3**（50 次，ENGINE_BUG）：词典候选 ['li1', 'li5', 'li3', 'li2']；例：cm:73711(位置6)、cm:73712(位置6)、cm:73715(位置6)、cm:73716(位置23)
- **會 wui5 → wui2**（49 次，ENGINE_BUG）：词典候选 ['wui5', 'wui6', 'wui2', 'wui3', 'kui2', 'kui3', 'kwui2']；例：ct:388(位置4)、ct:715(位置18)、ct:773(位置15)、ct:872(位置9)
- **了 liao3 → le5**（48 次，ENGINE_BUG）：词典候选 ['le5', 'liao3']；例：cm:71(位置12)、cm:95(位置22)、cm:147(位置4)、cm:148(位置8)
- **燕 yan4 → yan1**（48 次，ENGINE_BUG）：词典候选 ['yan4', 'yan1']；例：cm:6563(位置10)、cm:38407(位置15)、cm:42295(位置36)、cm:42296(位置6)
- **僮 tong2 → zhuang4**（48 次，DICT_GAP）：词典候选 ['tong2']；例：cm:74462(位置11)、cm:74463(位置4)、cm:74463(位置13)、cm:74465(位置6)
- **呀 jaa1 → aa4**（48 次，DICT_GAP）：词典候选 ['jaa1']；例：ct:361(位置7)、ct:384(位置7)、ct:507(位置6)、ct:585(位置8)
- **湮 yin1 → yan1**（47 次，ENGINE_BUG）：词典候选 ['yan1', 'yin1']；例：cm:69645(位置7)、cm:69648(位置17)、cm:69650(位置27)、cm:69653(位置13)
- **钌 liao4 → liao3**（47 次，DICT_GAP）：词典候选 ['liao4']；例：cm:78606(位置9)、cm:78607(位置10)、cm:78608(位置3)、cm:78609(位置6)
- **斗 dou4 → dou3**（46 次，ENGINE_BUG）：词典候选 ['dou4', 'dou3', 'dou5']；例：cm:15565(位置23)、cm:25105(位置10)、cm:25105(位置28)、cm:25111(位置39)
- **拽 ye4 → zhuai4**（44 次，ENGINE_BUG）：词典候选 ['zhuai4', 'zhuai3', 'ye4']；例：cm:69766(位置15)、cm:69768(位置28)、cm:69769(位置40)、cm:69774(位置13)
- **煞 sha1 → sha4**（44 次，ENGINE_BUG）：词典候选 ['sha1', 'sha4']；例：cm:70616(位置27)、cm:70616(位置32)、cm:70617(位置2)、cm:70617(位置25)
- **閤 he2 → ge2**（43 次，DICT_GAP）：词典候选 ['he2', 'he5']；例：cm:31473(位置2)、cm:77289(位置7)、cm:77290(位置9)、cm:77291(位置30)
- **巂 gui1 → xi1**（43 次，DICT_GAP）：词典候选 ['gui1']；例：cm:77655(位置21)、cm:77656(位置5)、cm:77657(位置9)、cm:77658(位置6)
- **弄 nong4 → long4**（42 次，ENGINE_BUG）：词典候选 ['nong4', 'nong5', 'long4', 'leng5']；例：cm:16302(位置21)、cm:22133(位置21)、cm:22151(位置3)、cm:22151(位置29)
- **钉 ding1 → ding4**（40 次，ENGINE_BUG）：词典候选 ['ding1', 'ding4', 'ding5']；例：cm:66420(位置27)、cm:66423(位置31)、cm:66437(位置5)、cm:66439(位置20)
- **侗 dong4 → tong2**（39 次，ENGINE_BUG）：词典候选 ['dong4', 'tong2']；例：cm:32737(位置17)、cm:53505(位置13)、cm:53506(位置1)、cm:53509(位置14)
- **汗 han4 → han2**（38 次，ENGINE_BUG）：词典候选 ['han4', 'han2']；例：cm:24198(位置3)、cm:25710(位置25)、cm:29783(位置13)、cm:43250(位置4)
- **嚟 lai4 → lei4**（38 次，ENGINE_BUG）：词典候选 ['lai4', 'lei4']；例：ct:193(位置13)、ct:222(位置9)、ct:295(位置4)、ct:389(位置15)
- **为 wei4 → wei2**（36 次，ENGINE_BUG）：词典候选 ['wei2', 'wei4', 'wei5']；例：cm:996(位置7)、cm:1205(位置0)、cm:3631(位置32)、cm:4158(位置8)
- **轧 ya4 → zha2**（36 次，ENGINE_BUG）：词典候选 ['ya4', 'zha2']；例：cm:73971(位置14)、cm:73978(位置10)、cm:73983(位置1)、cm:73984(位置12)
- **长 chang2 → zhang3**（35 次，ENGINE_BUG）：词典候选 ['chang2', 'zhang3']；例：cm:1217(位置25)、cm:3604(位置43)、cm:12228(位置12)、cm:12234(位置10)
- **缪 miu4 → miao4**（35 次，DICT_GAP）：词典候选 ['miu4', 'mou2']；例：cm:68211(位置2)、cm:69379(位置9)、cm:69381(位置10)、cm:69391(位置28)
- **大 dai4 → da4**（34 次，ENGINE_BUG）：词典候选 ['da4', 'dai4', 'da2']；例：cm:937(位置12)、cm:1583(位置5)、cm:1583(位置17)、cm:9253(位置5)
- **覃 tan2 → qin2**（34 次，DICT_GAP）：词典候选 ['tan2']；例：cm:75799(位置8)、cm:75851(位置23)、cm:75853(位置0)、cm:75854(位置0)
- **过 guo4 → guo5**（33 次，ENGINE_BUG）：词典候选 ['guo4', 'guo5']；例：cm:3517(位置13)、cm:4957(位置6)、cm:4973(位置23)、cm:4979(位置25)
- **刹 sha1 → cha4**（33 次，ENGINE_BUG）：词典候选 ['sha1', 'cha4']；例：cm:68153(位置7)、cm:71666(位置10)、cm:71669(位置3)、cm:71670(位置9)
- **鱲 lip6 → laap6**（33 次，ENGINE_BUG）：词典候选 ['lip6', 'laap6', 'lap6']；例：ct:130(位置7)、ct:255(位置5)、ct:720(位置7)、ct:723(位置5)
- **得 de2 → de5**（32 次，ENGINE_BUG）：词典候选 ['de2', 'de5', 'dei3', 'de4']；例：cm:2408(位置25)、cm:2408(位置31)、cm:2506(位置14)、cm:3580(位置17)
- **率 lv4 → shuai4**（32 次，DICT_GAP）：词典候选 ['lv4', 'shuai4']；例：cm:20731(位置13)、cm:55270(位置13)、cm:55352(位置16)、cm:58203(位置7)
- **夫 fu5 → fu1**（31 次，ENGINE_BUG）：词典候选 ['fu1', 'fu5']；例：cm:1583(位置6)、cm:1583(位置18)、cm:9253(位置6)、cm:11919(位置37)
- **薄 bo2 → bao2**（31 次，ENGINE_BUG）：词典候选 ['bo2', 'bao2', 'bo4']；例：cm:8824(位置26)、cm:33750(位置22)、cm:41552(位置11)、cm:41565(位置32)
- **曾 ceng2 → zeng1**（31 次，ENGINE_BUG）：词典候选 ['ceng2', 'zeng1']；例：cm:28585(位置21)、cm:28606(位置25)、cm:28610(位置15)、cm:28655(位置8)
- **宿 su4 → xiu4**（31 次，ENGINE_BUG）：词典候选 ['su4', 'xiu4', 'su1']；例：cm:31572(位置12)、cm:31576(位置7)、cm:31576(位置18)、cm:31576(位置35)
- **呀 jaa1 → aa3**（31 次，DICT_GAP）：词典候选 ['jaa1']；例：ct:33(位置6)、ct:153(位置5)、ct:850(位置19)、ct:1376(位置9)
- **吓 haa2 → haa5**（30 次，ENGINE_BUG）：词典候选 ['haa2', 'haa1', 'haak1', 'haak3', 'haa5']；例：ct:39(位置6)、ct:88(位置21)、ct:142(位置20)、ct:233(位置21)
- **少 shao3 → shao4**（29 次，ENGINE_BUG）：词典候选 ['shao3', 'shao4', 'shao5']；例：cm:27249(位置36)、cm:27281(位置24)、cm:27290(位置6)、cm:27297(位置20)
- **钻 zuan1 → zuan4**（29 次，ENGINE_BUG）：词典候选 ['zuan1', 'zuan4']；例：cm:28710(位置15)、cm:28717(位置8)、cm:28720(位置1)、cm:28733(位置8)
- **扒 pa2 → ba1**（29 次，ENGINE_BUG）：词典候选 ['pa2', 'ba1']；例：cm:55954(位置10)、cm:55955(位置20)、cm:55956(位置25)、cm:55960(位置15)
- **揣 chuai3 → chuai1**（29 次，ENGINE_BUG）：词典候选 ['chuai3', 'chuai1', 'chuai4']；例：cm:71350(位置8)、cm:71355(位置7)、cm:71359(位置4)、cm:71365(位置17)
- **圜 huan2 → yuan2**（29 次，DICT_GAP）：词典候选 ['huan2']；例：cm:73307(位置10)、cm:73308(位置39)、cm:73310(位置15)、cm:73316(位置13)
- **倒 dao3 → dao4**（28 次，ENGINE_BUG）：词典候选 ['dao3', 'dao4']；例：cm:1761(位置7)、cm:15547(位置2)、cm:40285(位置6)、cm:40290(位置7)
- **都 dou1 → du1**（28 次，DICT_GAP）：词典候选 ['dou1', 'du1']；例：cm:20846(位置14)、cm:55303(位置15)、cm:55315(位置18)、cm:55378(位置26)
- **劲 jin4 → jing4**（28 次，ENGINE_BUG）：词典候选 ['jin4', 'jing4']；例：cm:55693(位置10)、cm:55695(位置33)、cm:55696(位置12)、cm:55703(位置4)
- **啦 la1 → la5**（28 次，ENGINE_BUG）：词典候选 ['la1', 'la5']；例：cm:67423(位置14)、cm:70464(位置25)、cm:70466(位置14)、cm:70471(位置7)
- **长 zhang3 → chang2**（27 次，ENGINE_BUG）：词典候选 ['chang2', 'zhang3']；例：cm:12044(位置23)、cm:12219(位置9)、cm:12225(位置17)、cm:12303(位置20)
- **佛 fu2 → fo2**（27 次，ENGINE_BUG）：词典候选 ['fo2', 'fu2']；例：cm:14404(位置10)、cm:14405(位置0)、cm:14420(位置14)、cm:14425(位置5)
- **曲 qu1 → qu3**（27 次，ENGINE_BUG）：词典候选 ['qu1', 'qu3']；例：cm:17325(位置9)、cm:19183(位置14)、cm:19449(位置9)、cm:19459(位置9)
- **銀 ngan4 → ngan2**（27 次，ENGINE_BUG）：词典候选 ['ngan4', 'an2', 'ngan2', 'ngang2', 'an4', 'ngang4']；例：ct:95(位置15)、ct:199(位置14)、ct:216(位置14)、ct:415(位置14)
- **下 haa6 → haa5**（27 次，ENGINE_BUG）：词典候选 ['haa6', 'haa5', 'haa2']；例：ct:655(位置7)、ct:956(位置1)、ct:1244(位置11)、ct:1671(位置16)
- **豁 huo4 → huo1**（26 次，ENGINE_BUG）：词典候选 ['huo4', 'huo1', 'hua2']；例：cm:66579(位置27)、cm:66581(位置4)、cm:66583(位置24)、cm:66584(位置40)
- **载 zai4 → zai3**（25 次，ENGINE_BUG）：词典候选 ['zai4', 'zai3']；例：cm:7791(位置3)、cm:7795(位置2)、cm:7805(位置12)、cm:7821(位置3)
- **为 wei2 → wei4**（25 次，DICT_GAP）：词典候选 ['wei2', 'wei4', 'wei5']；例：cm:14178(位置0)、cm:20739(位置15)、cm:39203(位置6)、cm:59144(位置19)
- **杆 gan3 → gan1**（25 次，ENGINE_BUG）：词典候选 ['gan3', 'gan1']；例：cm:29026(位置10)、cm:29034(位置13)、cm:29034(位置25)、cm:29037(位置9)
- **弹 tan2 → dan4**（25 次，ENGINE_BUG）：词典候选 ['tan2', 'dan4', 'tan5']；例：cm:29166(位置20)、cm:29184(位置18)、cm:29191(位置13)、cm:29194(位置1)
- **涌 yong3 → chong1**（25 次，ENGINE_BUG）：词典候选 ['yong3', 'chong1']；例：cm:33850(位置1)、cm:33852(位置11)、cm:33870(位置2)、cm:33870(位置18)
- **番 fan1 → pan1**（25 次，DICT_GAP）：词典候选 ['fan1']；例：cm:51471(位置13)、cm:51476(位置13)、cm:51481(位置10)、cm:51485(位置4)
- **鬲 ge2 → li4**（25 次，DICT_GAP）：词典候选 ['ge2']；例：cm:73268(位置11)、cm:73269(位置7)、cm:73271(位置8)、cm:73272(位置21)
- **垛 duo3 → duo4**（25 次，DICT_GAP）：词典候选 ['duo3']；例：cm:76129(位置23)、cm:76147(位置19)、cm:76149(位置4)、cm:76157(位置7)
- **瑱 zhen4 → tian4**（25 次，DICT_GAP）：词典候选 ['zhen4']；例：cm:79068(位置1)、cm:79070(位置15)、cm:79071(位置1)、cm:79072(位置1)
- **玩 wun6 → waan2**（25 次，ENGINE_BUG）：词典候选 ['wun6', 'waan2', 'waan4']；例：ct:222(位置10)、ct:927(位置18)、ct:927(位置20)、ct:1277(位置4)
- **丽 li4 → li2**（24 次，ENGINE_BUG）：词典候选 ['li4', 'li2']；例：cm:937(位置10)、cm:937(位置32)、cm:24738(位置21)、cm:32765(位置2)
- **上 soeng6 → soeng5**（24 次，ENGINE_BUG）：词典候选 ['soeng6', 'soeng5']；例：ct:7(位置3)、ct:7(位置11)、ct:195(位置14)、ct:207(位置7)
- **重 chong2 → zhong4**（23 次，ENGINE_BUG）：词典候选 ['chong2', 'zhong4']；例：cm:1128(位置1)、cm:1129(位置14)、cm:1158(位置7)、cm:1170(位置21)
- **转 zhuan4 → zhuan3**（23 次，ENGINE_BUG）：词典候选 ['zhuan3', 'zhuan4', 'zhuan5']；例：cm:1129(位置0)、cm:1434(位置32)、cm:1440(位置21)、cm:1459(位置35)
- **中 zhong4 → zhong1**（23 次，ENGINE_BUG）：词典候选 ['zhong1', 'zhong4']；例：cm:1986(位置15)、cm:3189(位置8)、cm:3341(位置7)、cm:5137(位置2)
- **分 fen4 → fen1**（23 次，ENGINE_BUG）：词典候选 ['fen1', 'fen4', 'fen5']；例：cm:2981(位置4)、cm:2981(位置15)、cm:2981(位置23)、cm:6082(位置0)
- **仇 chou2 → qiu2**（23 次，DICT_GAP）：词典候选 ['chou2']；例：cm:58180(位置0)、cm:58181(位置0)、cm:58203(位置5)、cm:58205(位置6)
- **氓 mang2 → meng2**（23 次，ENGINE_BUG）：词典候选 ['mang2', 'meng2']；例：cm:68476(位置12)、cm:68478(位置38)、cm:68483(位置19)、cm:68487(位置2)
- **俟 si4 → qi2**（23 次，DICT_GAP）：词典候选 ['si4']；例：cm:75234(位置17)、cm:75239(位置4)、cm:75244(位置7)、cm:75249(位置4)
- **更 gaang1 → gang3**（23 次，ENGINE_BUG）：词典候选 ['gaang1', 'gang1', 'gang3']；例：ct:123(位置11)、ct:170(位置10)、ct:335(位置4)、ct:776(位置12)
- **呢 nei1 → ne1**（23 次，DICT_GAP）：词典候选 ['nei1', 'li1', 'le4', 'ni1', 'le1']；例：ct:644(位置9)、ct:800(位置20)、ct:895(位置21)、ct:1155(位置9)
- **出 chu5 → chu1**（22 次，ENGINE_BUG）：词典候选 ['chu1', 'chu5']；例：cm:3282(位置28)、cm:9613(位置27)、cm:12096(位置32)、cm:19241(位置33)
- **長 coeng4 → zoeng2**（22 次，ENGINE_BUG）：词典候选 ['coeng4', 'zoeng2', 'zoeng6', 'coeng2', 'coeng1']；例：ct:416(位置13)、ct:416(位置21)、ct:438(位置10)、ct:490(位置4)
- **呢 ne5 → ni2**（21 次，ENGINE_BUG）：词典候选 ['ne5', 'ni2']；例：cm:63600(位置17)、cm:63605(位置10)、cm:63618(位置6)、cm:63624(位置17)
- **地 di5 → di4**（20 次，ENGINE_BUG）：词典候选 ['de5', 'di4', 'di5']；例：cm:88(位置9)、cm:766(位置35)、cm:5586(位置33)、cm:5706(位置5)
- **任 ren4 → ren2**（20 次，ENGINE_BUG）：词典候选 ['ren4', 'ren2']；例：cm:11767(位置3)、cm:21628(位置8)、cm:44090(位置3)、cm:54255(位置0)
- **琢 zuo2 → zhuo2**（20 次，ENGINE_BUG）：词典候选 ['zhuo2', 'zuo2']；例：cm:36250(位置15)、cm:41540(位置21)、cm:71767(位置2)、cm:71769(位置2)

## 复现

```
python .tmp/corpus/clusters.py
```

## 词典本身的缺陷（不是算法，是词典数据：词条写错，或少了必须的读音）

这类缺陷由三个方向发现：抽检时校对员指出的、`tools/corpus/dict_typos.py` 按「两个读音只差一个易混声母」扫出来的、以及金标里 verified 值**不在候选表**中的那些位置（缺读音）。
它们会让引擎**理直气壮地读错**：词典里那条错读音就是引擎的合法候选，甚至就是首选；缺读音时引擎只能给出别的音（金标记 DICT_GAP）。

| 文件:行 | 现状 | 应为 | 影响 |
|---|---|---|---|
| `res/dict/cantonese/word.txt:1727` | 跌:dit3,tit3 | 跌:dit3 | tit3 不是 跌 的音；金标 48 处 dit3/tit3 并收因此收窄，ct/cu 各 26/28 行改判引擎有错 |
| `res/dict/cantonese/phrases_dict.txt:1398` | 跌打:tit3,daa2 | 跌打:dit3,daa2 | 同一个错读音的词组表版本 |
| `res/dict/cantonese/phrases_dict.txt:680` | 雞脾:gai1,pei2 | 雞脾:gai1,bei2 | 雞髀/雞脾 的 髀 读 bei2（同一份表里 大髀:daai6,bei2 就是这么写的，word.txt 髀:bei2），但 雞脾 这条写了 pei2，引擎就照它读 pei4/pei2；金标本来就是 bei2，是正确的 |
| `res/dict/cantonese/word.txt:4588` | 吓:haa2,haa1,haak1,haak3,haa5 | 吓:haa5 | 動詞后缀「吓」（行吓、睇吓）读 haa5；这条把 haa2 排在第一位，引擎就照它读 haa2。金标 11 处跟着错、18 处写 haa5，自相矛盾，已统一收窄 |
| `res/dict/cantonese/word.txt:339` | 加:gaa1,gaa3 | 加:gaa1 | 加 只有 gaa1（新加坡、參加）；gaa3 不是这个字的音，金标里 2 处跟着标 gaa3 已改回 gaa1 |
| `res/dict/cantonese/trans_word.txt:628` | 薈:wai3,kui2,kwui2,wui6,kui3,wui3 | 薈:wui6 | 薈 读 wui6（蘆薈、薈萃、東薈城）；这条塞了六个互不相干的音且把 wai3 排第一，金标 7 处跟着标 wai3、2 处标 wui6，已统一收窄 |
| `res/dict/cantonese/phrases_dict.txt:977` | 一切:jat1,cai3 | 一切:jat1,cit3 | 一切 读 jat1 cit3；cai3 是切菜的口语音。语料跟着词典一起错，已按规范读法改判 |
| `res/dict/cantonese/word.txt:1927` | 呢:nei1,li1,le4,ni1,le1 | 呢:nei1,li1,le4,ni1,le1,ne1 | **缺口**（第十二轮发现）：句末语气助词「呢」读 ne1 —— 本语料把限定词 呢（nei1）44 处与句末 呢（ne1）23 处分得干干净净，但这条候选表里根本没有 ne1，引擎在句末只能给 nei1/le1 一类的音，金标这 23 个位置因此记 DICT_GAP |
| `res/dict/cantonese/word.txt:2047` | 嚟:lai4,lei4 | 嚟:lei4 | 口语「来」义的 嚟 读 **lei4**——仓库自己的词组条目就是这么写的（`phrases_dict 過嚟:gwo3,lei4`、`返嚟:faan1,lei4`），而这条把 lai4 排在第一位（lai4 本来只是 黎/犁 的音，因为民间常用 黎 代写 嚟 才混进来）。语料 45 行跟着错、6 行并收，已全部收窄为 lei4，ct 有 25 行因此从「引擎对」改判引擎错 |

复现：`python tools/corpus/dict_typos.py`（扫同一字两个读音只差易混声母的词条）
