# 新语种拓展 playbook（可执行流程，**本仓只做中粤**）

> 本文是**流程与验收口径**，不是语种方案。边界本身早就定死：本仓只发 `{mandarin, cantonese}`，
> 新语种一律留在 `pinyin-makedict`。本文只回答"**怎么把一个新语种从数据做到可报的准确率**"。

**行号口径**：本文所有 `文件:行` 均在本仓 `v2` 分支上**逐条读过**；引用前请自行复核——行号会漂，提交也会被
压缩改写，所以这里不钉哈希，以你读到的这棵树为准。
未复核的项在 §8 标「未验证」。

---

## 0. 一句话流程

```
pinyin-makedict 产出 out/dict/<lang>/  ──►  cpp 侧【只读】指向该目录做评估  ──►  报 EXACT/SYLLABLE + 绝对数
        （唯一允许增语种的地方）                 （树外接入，不改本仓数据面）            （两套设置并列，见 §4）
```

---

## 1. 边界（硬规范，已被门禁强制）

### 1.0 三条硬边界（用户指令）

1. **本仓只留中粤** ✓ —— 由门禁机械保证（`tests/data/dialect_set/` 两腿 + `tools/check_bindings.py` + `python/tools/build_native.py` 白名单）。
2. **新增语种只在词典仓（`pinyin-makedict`）落地** ✓，且**必须标明「实验版」** ✓（放行与否、数字多少，都只在那里记）。
3. **本仓不得出现这些语种的记录** ✓：语料、词表、清单、统计、达标数字一律不留 ✗；本仓只保留**方法与决策**
   （本 playbook、词典制作仓库里的拓展语种记录）✓，且必须写明**非规范、不代表本仓支持该语种** ✓。

| 事实 | 证据 |
|---|---|
| 新语种一律留在词典仓库，**与本仓无写入关系** | `docs/DECISIONS.md` §2.1:617` |
| 本仓目录集合**恰好**是 `{mandarin, cantonese}` | `tests/data/dialect_set/expected_dialects.txt`（两行） |
| 第一道门禁：目录名 + 运行时注册 id **双腿严格相等** | `tests/data/dialect_set/check_dialect_set.cmake`（139 行）：目录腿 `:100-105`，注册表腿 `:118-137` |
| 门禁自证：合成"多一个目录"必须红 | 同上 `:55-95`；`:72` 合成的正是 `synth/extra/teochew` |
| 门禁接线 | `tests/CMakeLists.txt:225-240`（`DialectSetGate` + `DialectSetGateSelfTest`） |
| 第二道门禁：绑定侧白名单 | `tools/check_bindings.py` 的 `check_dialect_surface`，白名单权威 `python/tools/build_native.py:32`（`DIALECTS = ("mandarin", "cantonese")`） |
| 第三方方言目录**不得**放进 `data/dialects/` | 门禁 `tests/data/dialect_set/expected_dialects.txt` 只列中粤两行，第三方目录出现在 `data/dialects/` 下会让 `DialectSetGate` 变红（原载此论断的 `docs/REPO_LAYOUT.md` 已于 2026-10-23 删除） |

门禁自己的报错文案把边界说得很清楚（`check_dialect_set.cmake:102-104`）：

> `A third language must stay in the dictionary repository; adding it here needs an explicit decision (plan 6.7.4 rule 2b).`

**⇒ 往 `data/dialects/` 加目录必红；把方言编进库或 `CApiSmoke` 也会让第二腿红。**

---

## 2. 新语种要产什么（数据契约）

目录形如 `data/dialects/<id>/`（树外则任意路径，`dictRoot` 由调用方给）：

| 文件 | 必需 | 说明 |
|---|---|---|
| `word.txt` | ✅ | 单字表，键是**单个 UTF-16 码元** |
| `phrases_dict.txt` | ✅ | 词组读音 |
| `phrases_map.txt` | ✅ | 滑动窗口的闸门位图 |
| `trans_word.txt` | ✅ | **繁→简**单字映射（`魚:鱼`；生成器产物实测如此） |
| `user_dict.txt` | 可选 | 实测粤语这份是 **0 字节**（允许为空）；**生成器不产出它**（C++ 侧 `src/EngineImpl.cpp:56-71` 可选加载） |
| `dialect.json` | 打包/门禁用 | **引擎不读它**（装载名单里没有它） |
| `License.txt` | 建议 | 许可随数据走 |

- **装载名单的唯一权威**：`src/EngineImpl.cpp:42-47`（四张表，按 `phrases_map` → `phrases_dict` → `word` → `trans_word` 顺序）。
- **行语法**：`src/dict/DictUtil.cpp:39-56`（`splitLine`）—— 去首尾空白后在 trim 区间找第一个分隔符。空行不是数据（可以存在）；**无分隔符或空键的行自 `507aa09` 起不再被丢弃**：它计入 `skippedNonBlank`，使**整张表**装载失败、文件名进 `lastError()`、引擎 `isReady()` 为假（判据在 `src/dict/DictUtil.cpp:43-74` 与 `:142-169`，两处字面量相同）。
  **例外**：`user_dict.txt` 走 `loadAdditionalDict`（`src/dict/DictUtil.cpp:199-229`）——文件打不开会返回 false（`test_user_dict.cpp` 钉住），但**内容级零校验**：其中的坏行既不计数也不报行号。
  分隔符默认 `sep1=':'`、`sep2=','`（`src/dict/DictUtil.h:39`,`:48`,`:53`）。样例：`word.txt` 的 `日:rì`、`phrases_dict.txt` 的 `一下儿:yī,xià,er`、`phrases_map.txt` 的 `下:234`、`trans_word.txt` 的 `魚:鱼`。
- **归一化函数必传**（`src/dict/DictUtil.h:61-64` 原文）：`normalizeReading` 是 **MANDATORY**；它曾经默认用普通话的函数，
  于是静默把粤语用户词典的 `jyut6` 改写成 `jyu6t6`。**新语种必须自带归一化，不能继承普通话的。**
- 单张表**一条 entry 都没有 = 装载失败**（`src/dict/DictUtil.cpp:58-61`）——空表不是"合法的空方言"。

---

## 3. 两种接入方式

### 3.1 树外接入（本仓路线的**唯一**形态，评估就用这个）

`include/cpp-pinyin2/ext/Dialect.h:15-17` 原文：

> `Adding a dialect does NOT require touching this library: provide dictionary files, a ToneConverter subclass, and one Dialect, then hand either to Pinyin::Engine (directly, or through DialectRegistry so it can be addressed by id).`

具体：`Dialect`（`include/cpp-pinyin2/ext/Dialect.h`）是聚合类型，字段 `id`、`dictSubDir`（空则等于 `id`）、
`tone`（按值）、`supportsVToU`、`supportsNeutralTone5`、`postProcess`（只对返回 `Result` 的 query 生效）。
拿到后可直接构造 `Engine(dialect, dictRoot)`；要按 id 解析，就用**调用方自持**的注册表：`cpp_pinyin::v2::Registry registry;`（`include/cpp-pinyin2/ext/Registry.h` 的 `Registry` 类与它的默认构造 —— 构造即含两个内置方言）→ `registry.add(id, creator)`（creator 的类型是 `DialectCreator`）→ `Engine::open(registry, dictRoot, id)`（`Engine.h` 的 `open` 注释里有原文示例）。**2026-10-05（S3）起没有自注册宏、也没有进程级注册表** —— 见本页末尾那节的作废说明。
`include/cpp-pinyin2/ext/Dialect.h` 的 `Dialect` 注释明确 **`Dialect` 不携带词典路径、不携带已加载状态**；`include/cpp-pinyin2/ext/Registry.h` 的 `resolve` 注释明确解析失败
**不得**被当成"回落到普通话"；`postProcess` 的注释明确消歧窗口**冻结**，不要在 `postProcess` 里重造引擎。

> 这一段按**名字**引用，不写 `文件:行号`：行号每次重构都会漂，指错的引用比没有更糟。

⚠️ **C ABI 没有注册入口**（`include/cpp-pinyin2/ext/c_api.h` 末尾那段的原文 `DELIBERATELY NOT HERE`）：C 无法构造 `ToneConverter`，
所以 C#/Python 壳今天**挂不了第三方方言**，只能拿两个内置方言（外加 C++ 宿主在交出句柄前注册过的）。
要发布新方言，走 C++ 插件路径：`docs/Custom Chinese Dialect.md`。

### 3.2 树内收编（**不在本仓路线内**）

真要做，必须同步改四处：`tests/data/dialect_set/expected_dialects.txt`、`python/tools/build_native.py:32`、
`src/Engine.cpp:45-50`、以及 `data/dialects/<id>/`。门禁文案要求这是**显式决定**，不是顺手加目录。**用户口径：不做。**

---

## 4. 验收口径（这一节是本文的核心）

**门槛：≥3000 个整句字位、字级对齐的金标。单位是整句，单字测试集一律不看。**

1. **指标锁定**（判据原文在词典制作仓库的拓展语种记录 §1.2）：`POLYPHONE` = 引擎 `readings()` 报出 >1 个读音的字；
   `EXACT` = 引擎读音等于金标任一项；`SYLLABLE` = 去掉末尾声调数字/变音符后相等。
   **中粤分开、永不合并；两个强度都要报；必须同时报绝对数**——上一轮出现过"分母缩水而百分比反而上升"的真事。
2. **循环验证禁令**（同归档 §1.5）：词典与测试集**不得建自同一批语料**。第一版这样做得到 100.00%，
   **什么也不证明**，只说明格式能往返。每个数字都要能回答"这是循环验证还是留出测试"。
3. **换算规则必须公开**：语料与词典若属两套正字法，只允许用**已公开发布**的对照规则转换
   （客家语的 PFS→MOE 是这一类的正例）。**禁止从被测语料里学出对应关系**——上一轮留下的教训原文在
   那条"从被测语料学出来的转换器"**已不在盘上**（不可复现，只作教训引用），
   它自己的 docstring 就写着"**对应关系越加越多本身就是危险信号**"。
4. **两种设置并列报出**（裁定 D1）：字形一致口径与"承认方言字对应"口径**都要报**，
   并写清差异来源。参照：客家语 `81.26%`（零映射）/ `93.20%`（承认 `我←𠊎`、`他/她←佢`）。
5. **达标线逐语种单独定**（裁定 D5）：不设全局固定阈值。每个语种首次报数时**逐例给定**，且必须写清
   **分母是多少、依据是什么**；若某语种存在**算术上限**，必须给出证明。参照：客家语已证明
   `(2545+213)/3132 = 88.06% < 90%`，即只要表把「我」读 `ngai` 之外的写法、「他/她」读 `ta`，任何修正都到不了 90%。
6. **盲测下限对照**：低于"随机选一读"的下限时（吴语实测 5.95% < 下限 43.27%），**不得报为准确率** ——
   那说明表与语料不适配，而不是"引擎差"。

---

## 5. 与 `pinyin-makedict` 的联动（已实测过的机制）

1. **先立基线**：原始生成器必须能**逐字节复现**冻结的短语表（`phrases_dict.txt` 完全一致），
   这是判断后续任何改动是否可信的前提。改动前先证明它，再动。
2. **短语表会爆炸**（陷阱）：改动默认首位会让冗余判定（`clip_long_phrase()` 里用 `default_pinyin[text][0]` 比较）
   翻转，实测把 `phrases_dict.txt` 从 **6,026 行撑到 93,066 行**（15 倍），且新增的 87,040 条里有 **85,380 条不含任何被改字**
   ⇒ 传播路径**尚未查明**，不要猜。
3. **规避法（已验证）**：`word.txt` 用生成器产出，**其余词典文件用冻结版**。这样评分与手工补丁**逐项一致**
   （`mandarin_tone` 的 `verdict_fnv` 与手工补丁记录值相同）。把两张表一起重新生成得到的分数**不可归因**，不要引用。
4. **规范化要自己定义**：`tone_to_tone3` 对中性调**不收敛**、`tone3_to_tone` **不幂等**（会把 `me` 变成 `me,me`），
   两个都不能当规范化函数用；生成器侧新增了 `canon_reading()` 并显式验证幂等。
5. **版本钉死**：`pypinyin==0.54.0`。本机 0.55.0 的差异**恰好**是仓库已记录的 5 个字 + `U+9FA6..9FFF` 新增 23 行；
   要产出与冻结表逐字节一致的词典，仍需装回 0.54.0。
6. **两仓的数据契约（2026-10-03 实测，补充说明——哪些表由生成器产出、哪些是本仓人工维护）**：
   这条以前没人写清，容易误以为"`data/dialects/<id>/` 里的一切都该由 makedict 生成"。实测：

   | `data/dialects/<id>/` 里的文件 | 来源 | 证据 |
   |---|---|---|
   | `word.txt` / `phrases_dict.txt` / `phrases_map.txt` / `trans_word.txt` | **makedict 生成** | 另一仓的 `mandarin.py:63,79,85,91`、`cantonese.py:97,108,113,119` 逐个 `open(f"{self.out_path}/…", "w")`；`out/dict/mandarin` 实测四张表齐备 |
   | `user_dict.txt` | **本仓人工维护**（生成器**不产出**） | 另一仓两个生成器全文**无** `user_dict.txt` 写入点（只在读侧用它）；mandarin 现役 101 行、cantonese 0 行 |
   | `License.txt` | **本仓人工维护**（生成器**不产出**） | 同上，生成器只写上述四张表；`dialect.json` 把它列为 `assets` 而非 `required_tables`（`data/dialects/mandarin/dialect.json:12-14`） |
   | `dialect.json` | **本仓维护**（清单） | 声明 `required_tables`/`optional_tables`/`assets`；由 `tools/check_bindings.py` 的 dialect-surface 检查按目录核对 |

   **契约的三条可执行判据**（都在本仓门禁里，不需要去另一仓跑）：
   ① `DialectSetGate`：`data/dialects/` 的目录集合 == 运行期注册表 == `tests/data/dialect_set/expected_dialects.txt`
   （**两腿**，且自带"多一个目录/少一个目录必须红"的自证）；② `check_bindings.py` 的 dialect 检查：
   每份 `dialect.json` 与目录内容一致、且表名与 loader 实际读的名字一致；
   ③ `BindingGates` 与 `LayerIncludes` 把 ①② 拉进本机门禁（`ctest -LE slow`，实测 25/25）。
   ⇒ **"新语种只留在 makedict"这条规则是有机器门禁的**：往 `data/dialects/` 塞第三门语言会**红**。

   **两仓对照只有一处权威**：词典仓的 `tools/check_drift.ps1`（逐字节，`-CppPinyinRoot` 指向本仓）。
   本仓不自带第二份"只比表名 + 行数"的近似检查——一个事实两个答案，迟早给出两个结论。
   早前那张差 1 行的 `cantonese/trans_word.txt`（`吒:咤`）由词典仓生成器侧补齐，本仓 `data/dialects` 一行未改；
   别在消费者可见的读数上"删行"凑齐（那会把 `readings("吒")` 从 4 个读数变成 2 个）。
   ⚠️ **三件跨仓工具的职责边界——谁是字节权威、为什么"行数 ≠ 字节"、为什么它们都不进 CTest——
   以 `tools/check_drift.ps1` 的文件头（字节权威）与词典仓 `TOOLING.md` 末尾的「三者职责」表为准**（2026-10-04 起；2026-10-23 起原载此表的 `docs/REPO_LAYOUT.md` 已删除），本节不复述；
   本节只留上面那次实测记录本身。
7. ⚠️ **生成仓现状（2026-10-03 只读复核，静态判读、未运行生成器）**：
   - 仓库是同级检出 **`pinyin-makedict`**，
     分支 **`v2`**、工作区干净、31 个跟踪文件（2026-10-01 的一次快照；该仓此后已推进，当前 HEAD 与跟踪文件数按该仓 `git log -1` / `git ls-files` 为准）。
     `rime-cantonese-upstream` 子模块**未初始化**（目录 0 项）⇒ 不联网拉它就跑不了粤语全量生成。
   - **入口是每语种一个脚本**：`python mandarin.py` / `cantonese.py`（须在仓库根目录跑，路径全相对；
     模块级执行，没有 `if __name__ == "__main__"`）。该仓 `README.md` 已是真实现状（不再是 `makedict.py`）；
     该仓 `TOOLING.md:20` 写的是 `cd <该仓检出目录>` —— **正确，不是过期陈述**（2026-10-02 记的
     "未核对"一条至此结案：**已核对，无漂移**）。
   - **已产出现状**（实测 `out/dict/` 内容，`out/` 已 gitignore）：`mandarin/{word,phrases_dict,phrases_map,trans_word}.txt`
     + `mandarin.cpdict`、`cantonese/` 同结构 + `cantonese.cpdict`、**`teochew/` 四张表仍在**（生成器已出仓，
     产物是历史遗留，不要当出厂词库）。⇒ **`.cpdict` 今天已经真的产出来了** —— 这一点直接关系到
     本仓 **AC-10（二进制词典）**的现状判读：**判据在本仓侧仍未落地**（本仓 loader 无 `cpd`/binary 支持），
     但**前提（生成器能产出）已经成立** ⇒ AC-10 从"两头都没有"变成"**只差本仓侧**"。这一条以前没有任何文档写过。
   - 已产出：`out/dict/{mandarin, cantonese, teochew}/`。
   - **语种成熟度**（该仓 `docs/DIALECT_EXTENSION_RECORD.md`）：普通话/粤语 L0 出厂；**潮州话 L1 实验**（生成为真）；
     客家语、越南语 L2（**手搓表，无生成器**）；闽南语仅调研；吴语 L3；高棉语不适用。
     ⚠️ **按红线②，各语种的准确率统计与依据只在该仓记录** —— 本仓不复述具体百分比；方法与判据见该仓
     `docs/DIALECT_EXTENSION_RECORD.md` 的语种表。
   - **短语表行数以产出为准 = 6,975 行**（`out/dict/mandarin/phrases_dict.txt` 与本仓现役文件**行数一致**）。
     ⚠️ 常被引用的 **6,931** 是**审计分母**（该仓 `pinyin-makedict/data/overrides/man_overrides.py:195` 的原话是"All 6931 Mandarin phrase entries were
     compared against the corpus …"），**不是产物行数**——本仓 `tests/cpp/test_accuracy_v2.cpp` 与过程记录曾按 6,931 记，
     已更正。
   - **加新语种的落点**（该仓 `docs/DIALECT_EXTENSION_RECORD.md`；潮州话先例的生成器与数据已出仓，只存记录）：
     `data/<lang>_sanitized.txt`（`词\t读音`）→ 无 G2P 库时再给 `data/<lang>_origin_vocab.txt`（`字#r1|r2`）→
     空 `data/<lang>_overrides.py` → `<lang>.py` 子类实现 `load_dict` / `fill_unicode_pinyin` / `make_dict` →
      非普通话音位用**构造参数** `MakeDict(..., preserve_e_hat=True)`（模块级全局 `common.PRESERVE_E_HAT` 已删除）
      → **`tools/check_drift.ps1` 不需要改**：它从两棵树的目录名**派生**方言集合，没有需要维护的清单 →
      **不改本仓**。

   - **未验证**：历史上那批"未提交改动"是否仍在（`man_overrides.py` 里的 61+35 条已随提交 `de7d3d4` 落地，但两个仓库的
     Δpt 记法不一致：cpp 侧记 **+24.85pt**、该仓记 **+24.32pt**，**未核实哪个是权威** ✗）。

---

## 6. 其他语种去哪里看

本仓只做中粤。其他语种的可用性、许可红线与结案结论由词典仓的 `docs/DIALECT_EXTENSION_RECORD.md` 一处说了算；
本文件只保留**方法**（怎么接、怎么量、按什么判），不复述那份台账里的数字。

---

## 7. 四套构建配置的注册用例数与指纹

同一份源码在不同配置下注册**不同**的 CTest 条目，所以"多少个用例"必须带配置说。下面是四套配置的当前读数，
由 `tools/check_doc_claims.py` 的 `measured_fingerprint()` 直接实测（条目名排序后取 `sha256` 前 16 位——
**计数相同不等于集合相同**，`默认` 与 `+两者` 都是 49 条但指纹不同）。

| 配置 | 注册用例 | `-LE slow` | 条目集合指纹 |
|---|---|---|---|
| 默认 | **49** | **45** | `93b0e403a39ffa01` |
| + `-DCPP_PINYIN2_BUILD_EXAMPLES=ON` | **50** | **46** | `fe3bb3d3b3465f4f` |
| + `-DBUILD_SHARED_LIBS=OFF` | **48** | **44** | `822eb3cb318453cf` |
| + 两者同时 | **49** | **45** | `a131c1e6c36e0485` |

**这四行由谁复核、以及复核它需要什么**（2026-10-27 补，因为这一点此前只写在代码里）：`tools/check_doc_claims.py`
的 `check_config_fingerprints` 会重算每一行并与表比对，但它读的是**四个固定目录**——`.tmp/mk-default`、
`.tmp/mk-examples`、`.tmp/mk-static`、`.tmp/mk-min`。后三个名字**没有任何文档或 workflow 会去创建**，
所以"某一行没被复核"完全可能悄悄发生；检查器会把这种行单独报成 *unverified*（不是通过）。

要让四行都可复核，按下面四行建树（都在仓库根，产物只落 `.tmp/`）：

| 表里的行 | 目录 | 配置 |
|---|---|---|
| 默认 | `.tmp/mk-default` | `Release` / Ninja / 共享 / tests ON / examples OFF |
| + examples | `.tmp/mk-examples` | 同上，`-DCPP_PINYIN2_BUILD_EXAMPLES=ON` |
| + 静态 | `.tmp/mk-static` | 同上，`-DBUILD_SHARED_LIBS=OFF` |
| + 两者同时 | `.tmp/mk-min` | 两者都开（**名字是历史遗留**："min" 指当年"没有导出面门禁的最小构建"，今天就是"静态 + examples"） |

2026-10-27 实测：四棵树齐备时，**四行的计数与指纹全部与表一致**（`93b0e403a39ffa01` / `fe3bb3d3b3465f4f`
/ `822eb3cb318453cf` / `a131c1e6c36e0485`），也就是说这张表不是抄的，是可复现的。

文档里单独出现的用例数一律指**默认配置**；`tools/check_doc_claims.py` 以 `.tmp/mk-default` 为规范配置，
被测配置另由 `--ctest-inventory` 传入，因此同一份文档在四套配置下给出同一结论。

本机不可验的配置是 `-DCPP_PINYIN2_SANITIZE=ON`：`CMakeLists.txt` 在编译器不支持时配置期就报错（这是对的），
而本机 MinGW 无 sanitizer 运行库、Windows clang 缺 `stl_asan.lib` ⇒ 它属 Linux CI 路径，本地只能记"未验证"。
**探针必须实例化一个 STL 类型**（如 `std::string`）：只编译 `int main() { return 0; }` 的探针在 Windows clang 上
会**通过**配置期，真到链接期才炸 `stl_asan.lib`。现在的探针实例化 `std::string`（见 `CMakeLists.txt` 该分支），
所以本机确实是**配置期**给出那条 FATAL_ERROR。

---

### 7.1 按标签选跑（实测条目数）

标签清单由注册表自动发现，不是硬编码。默认配置当前的实测分布是：`abi` 7 条、`docs` 11 条、`corpus` 7 条、
`generation` 5 条、`slow` 4 条、`install` 3 条、`dialect` 2 条、`registry` 2 条、`thread` 2 条、`ac3` 1 条、
`ac4` 1 条、`doc` 1 条。`slow` 只报告不重跑（清单第 1 步已经跑过全部）。

### 7.2 五条 generation 门禁

默认配置与三套变体**都**注册这五条，注册点在 `tests/CMakeLists.txt` 的 `foreach` 内；它们守的是"名字与记号
在换代时一起变"：`GenerationGate_check_version_tokens`、`GenerationGate_check_public_names`、
`GenerationGate_check_root_names`、`GenerationGate_check_generation_compat`、
`GenerationGate_check_doc_code_tokens`。对应工具分别是 `tools/check_version_tokens.py`、
`tools/check_public_names.py`、`tools/check_root_names.py`、`tools/check_generation_compat.py`、
`tools/check_doc_code_tokens.py`；跨语言夹具的取值范围另由 `tools/check_conformance_provenance.py` 守。

---

## 新增方言时"必须改的第二处"

新增一个方言，除了 `data/dialects/<id>/` 与它的 `dialect.json`，代码里通常还有一份**隐式清单**要跟着改；
漏改不会报错，而是**静默变窄**（工具照跑、门禁照绿）。本仓实测一处：

| 位置 | 症状 | 现在的把守 |
|---|---|---|
| `dialect.json` 的 `required_tables` 与 `MANIFEST.sha256` 各自列一遍表名 | 两份权威可静默漂移 | 词典仓 `check_drift.ps1` 逐字节比；本仓 `DialectSetGate` 比方言集合 |

**两条规矩**：能从树或 manifest 派生的**一律派生**；不能派生的（如写进 header 的槽位号）必须**响亮自检**
（点名 + 非零退出）。完整版——三条规矩、判断标准、以及词典仓那两处同类坑——见词典仓 `TOOLING.md` §7
（口径单点在那里，本页不再复述，避免又造出一份"同一事实两处权威"）。

