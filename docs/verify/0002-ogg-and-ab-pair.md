# 验证报告：0002-ogg-and-ab-pair

## 1. 基本信息

| 项 | 值 |
|---|---|
| change id | `0002-ogg-and-ab-pair` |
| 执行人 | AI 智能体（TRAE） |
| 执行时间 | 2026-11-23 11:04（最近一次） |
| 机器 / 系统 | Windows 11（开发机） |
| 工具链版本 | OpenSpec CLI 1.14.3；CMake 4.2.3；MSVC 19.44.35228.0；Python 3.14 + numpy 2.5.3；ffmpeg N-122110；DevEco Studio 26.0.0（build DS-261.23567.138.36.2600865）；hvigor 6.26.9 |
| 运行环境 | HarmonyOS SDK 26.0.0.105（apiVersion 26，Release）；hdc 3.2.0f；**真机 MRT-AL10（序列号 5KTUT25424016774，API 26，系统 7.0.0.111(SP12C00E111R5P2)）** |
| 命令 | `powershell -File scripts/verify.ps1 --change 0002-ogg-and-ab-pair` |
| 日志 | `artifacts/verify-0002-ogg-and-ab-pair-20261123-110442.log` |
| 结果包 | `fixtures/synthetic/` 6 个音频（含 `ab_pair_44100_stereo_{a,b}.wav` 与 `intro_loop_44100_mono.ogg`）+ `fixtures/manifest.json`；`entry/build/default/outputs/default/entry-default-signed.hap` |

**本次验证范围**：任务 1.1~1.2、2.1~2.3、3.1~3.2、4.1~4.3、5.1~5.3、6.1 已完成；6.2 / 6.3 真机听测由人类判定「播放效果良好」；7.1~7.3 本次回填。

## 2. 需求覆盖

### 2.1 audio-format-support（新增）

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| OGG Vorbis Decoding | F01 OGG 文件加载与格式上报 | `DecoderTests.testLoadReportsFormatOgg` | **PASS** | 44100Hz / 1ch / 121275 帧，与 WAV 同源 fixture 完全一致 |
| OGG Vorbis Decoding | F01 总采样数与实际可读帧数一致 | `DecoderTests.testOggDecodedFramesMatchTotal` | **PASS** | 顺序解码累计帧数 == `totalFrames`（121275） |
| OGG Vorbis Decoding | F02 OGG 按采样点跳转 | `DecoderTests.testOggSeekMatchesSequentialDecode` | **PASS** | seek 到 33075 后读 512 帧，与顺序解码到该位置逐字节一致 |
| OGG Vorbis Decoding | F03 不支持的扩展名明确失败 | `DecoderTests.testOpenRejectsNonAudio` | **PASS** | `manifest.json` 等非音频文件返回 false，不崩溃 |

### 2.2 ab-pair-playback（新增）

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| A/B Pair Detection | P01 三组后缀均可配对 | `AbPairTests.testLowercasePairFromA/FromB`、`testUppercasePair`、`testStartEndPair`、`testCaseInsensitiveMatching` | **PASS** | `_a`/`_b`、`_A`/`_B`、`_start`/`_end` 三组均支持；大小写不敏感；替换保持输入大小写风格 |
| A/B Pair Detection | P01 扩展名保留、后缀只在主干上替换 | `AbPairTests.testExtensionPreserved` | **PASS** | `C:\music\bgm03_2_a.ogg` → `C:\music\bgm03_2_b.ogg` |
| A/B Pair Detection | P02 找不到配对时退化为单文件 | `AbPairTests.testMissingCounterpartDegradesToSingle` | **PASS** | 配对文件不存在时返回非配对，不报错 |
| A/B Pair Detection | P03 非 A/B 文件不受影响 | `AbPairTests.testNonPairNames`、`testSingleFileNotPair` | **PASS** | `song.mp3`、`song_ab.ogg`、`a.ogg`、`b.ogg`、`song_a_extra.ogg` 均不配对 |
| Virtual Timeline Playback | P04 虚拟轴总长等于两文件之和 | `AudioSourceTests.testPairReportsVirtualTotal`、`TransportTests.testLoadDetectsAbPair` | **PASS** | A=33075 + B=88200 = 121275；采样率与声道数取 A |
| Virtual Timeline Playback | P05 A 播完无缝接 B | `AudioSourceTests.testCrossFileReadIsContinuous`、`testReadAcrossBoundaryReturnsFullCount` | **PASS** | 从 `NA-1` 读 4 帧得到 A 末帧 + B 前 3 帧；跨边界读 200 帧返回 200，不返回 0 |
| Virtual Timeline Playback | P06 循环点在虚拟轴上生效 | `TransportTests.testAbPairLoopsOnVirtualTimeline` | **PASS** | `LoopStart=NA`、`LoopEnd=NA+NB`，循环计数 ≥ 2，未判定播放结束 |
| Virtual Timeline Playback | P07 虚拟轴 seek 跨文件 | `AudioSourceTests.testSeekWithinA`、`testSeekIntoB`、`testBoundaryPositions`、`testSeekIntoBReturnsBData` | **PASS** | `p<NA` 在 A 内 seek；`p>=NA` 在 B 内 seek 到 `p-NA`；B 数据与独立解码器逐字节一致 |

### 2.3 audio-loop-playback（修改：坐标基准改为虚拟时间轴）

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| Sample-Accurate Loop Jump | T01~T04 | 0001 既有套件（`DecodeThreadTests` / `LoopConfigTests`） | **PASS** | 坐标基准改为虚拟时间轴后，单文件路径语义不变，全部回归通过 |

## 3. 回归

| 范围 | 命令 | 结果 |
|---|---|---|
| 规范严格校验 | `openspec validate --all --strict` | **PASS**（2 passed, 0 failed） |
| 合成 fixture 生成 | `python scripts/make-fixtures.py` | **PASS**（6 文件 + manifest，含 A/B 对与 OGG） |
| 全量单元测试 | `scripts/verify.ps1 --change 0002-ogg-and-ab-pair` | **PASS**（12 套件 / 1325 断言 / 0 失败） |
| HAP 构建 | `hvigorw --mode module -p product=default --no-daemon assembleHap` | **PASS**（BUILD SUCCESSFUL） |
| 既有通过项是否回退 | 对照 `docs/regression-log.md` | **无回退**：0001 的 10 个套件全部通过，断言数从 1172 增至 1325（新增 2 套件） |

### 3.1 各套件断言数（2026-11-23 11:04）

| 套件 | 断言数 | 失败 |
|---|---|---|
| RingBufferTests | 184 | 0 |
| DecoderTests | 75 | 0 |
| AudioOutputLogicTests | 528 | 0 |
| DecodeThreadTests | 82 | 0 |
| LoopConfigTests | 74 | 0 |
| AbPairTests（新增） | 45 | 0 |
| AudioSourceTests（新增） | 62 | 0 |
| LoopCounterTests | 46 | 0 |
| LoopRangeChangeTests | 43 | 0 |
| LoopStreamTests | 36 | 0 |
| ProgressTests | 45 | 0 |
| TransportTests | 105 | 0 |
| **合计** | **1325** | **0** |

### 3.2 任务 1.1 / 1.2 实测证据

| 检查项 | 实测结果 |
|---|---|
| 真实素材清单 | `fixtures/real/README.md` 已填写 `水月陵 - flower.mp3`（DB 4872212 / 解码 4869551 / 循环 146308→4247608）与 `bgm03_2_a/b.ogg`（460798 + 3916798 = 4377596，DB TotalSamples=4377596，LoopStart=460798，LoopEnd=4377596） |
| 合成 A/B fixture | `ab_pair_44100_stereo_a.wav`（33075 帧，100Hz，周期 441 采样）+ `ab_pair_44100_stereo_b.wav`（88200 帧，441Hz + 二次谐波） |
| A→B 边界连续性 | 突变 0.157（比值 ≤ 1.5） |
| 循环点连续性 | 突变 0.50（比值 ≤ 1.5） |
| manifest 条目 | 含 `ab_pair` / `role` / `phase_continuous` 字段 |

### 3.3 任务 2.1~2.3 实测证据

| 检查项 | 实测结果 |
|---|---|
| stb_vorbis 引入 | `third_party/stb_vorbis.c`（198374 B）+ `stb_vorbis_impl.c`（60 B），与桌面端 `loopfinder/third_party` 同版本 |
| 双路径接入 | `SLH_CORE_SOURCES` 同时被单测与 `libentry.so` 使用 |
| OGG 格式上报 | 44100Hz / 1ch / 121275 帧，与同源 WAV 完全一致 |
| OGG 解码完整性 | 顺序解码累计帧数 == `totalFrames` |
| OGG seek 正确性 | seek 到 33075 后读 512 帧，与顺序解码逐字节一致 |
| 真实 OGG 实测 | 4 个真实 OGG 文件 `totalFrames == decodedFrames` 完全一致（`bgm03_2_a/b`、`dohnadohna_08`、`music03_03`） |

### 3.4 任务 3.1 / 3.2 实测证据

| 检查项 | 实测结果 |
|---|---|
| `AbPairTests` | **PASS**：10 用例 / 45 断言 / 0 失败 |
| 三组后缀 | `_a`/`_b`、`_A`/`_B`、`_start`/`_end` 各正反两向均通过 |
| 大小写不敏感 | `_A` → `_B`、`_a` → `_b`、`_Start` → `_end`（匹配不敏感，替换保持输入风格） |
| 扩展名保留 | 后缀替换只发生在主干，扩展名原样保留 |
| 非 A/B 文件 | `song.mp3`、`song_ab.ogg`、`a.ogg`、`b.ogg`、`song_a_extra.ogg` 均不配对 |
| 配对文件缺失 | 退化为单文件，不报错 |
| 真实文件存在性 | 合成 A/B fixture 从 A 侧与 B 侧进入均归一化为同一结果 |

### 3.5 任务 4.1~4.3 实测证据

| 检查项 | 实测结果 |
|---|---|
| `AudioSourceTests` | **PASS**：12 用例 / 62 断言 / 0 失败 |
| P04 虚拟轴总长 | A=33075 + B=88200 = 121275；采样率 44100、声道 2 取 A |
| P05 跨文件连续读 | 从 `NA-1` 读 4 帧 → 位置推进到 `NA+3`；跨边界读 200 帧返回 200 |
| P07 跨文件 seek | `p<NA` 在 A 内；`p>=NA` 在 B 内 seek 到 `p-NA` |
| 三个边界 | `p=NA-1`、`p=NA`、`p=NA+NB-1` 均正确 |
| 虚拟轴末尾 | 返回 0，不越界 |
| seek clamp | `-100 → 0`、`total+999999 → total` |
| B 数据正确性 | 虚拟轴 seek 到 B 后读出的 64 帧与独立解码器读 B 文件开头逐字节一致 |
| 格式不一致拒绝 | 44.1kHz 与 48kHz 文件配对返回 false |
| 未打开防护 | 各接口安全返回，不崩溃 |

### 3.6 任务 5.1~5.3 实测证据

| 检查项 | 实测结果 |
|---|---|
| `DecodeThread` 数据源 | 类型由 `AudioDecoder*` 改为 `AudioSource*`，循环跳转逻辑零改动 |
| `PlaybackEngine::Load` | 先 `DetectAbPair`，命中则 `OpenPair`，否则 `Open` 单文件 |
| `TransportTests` 新增用例 | `testLoadDetectsAbPair`（A/B 自动识别、总帧数 121275）、`testLoadSingleFileIsNotPair`、`testAbPairLoopsOnVirtualTimeline`（循环 ≥ 2 次） |
| 0001 全部 10 套件回归 | **PASS**，无退化 |
| 单文件路径 | `intro_loop_44100_mono.wav` 仍按 121275 帧加载，`IsPair() == false` |

### 3.7 任务 6.1 实测证据

| 检查项 | 实测结果 |
|---|---|
| 页面曲目表 | 4 首：合成单文件、合成 A/B、真实 A/B（bgm03_2）、真实单文件（flower.mp3） |
| A/B 加载 | 按 A 侧路径 `load()`，native 自动配对；页面显示「A/B 双文件」 |
| 循环点 | A/B 模式 `LoopStart = A 帧数`（合成 33075 / 真实 460798）；单文件用固定值或尾部秒数推导 |
| `getFormat().isPair` | napi 新增字段，`.d.ts` 同步 |
| ArkTS 编译 | **PASS**（`CompileArkTS` Finished，仅 SDK 提示性告警） |
| HAP 打包 | **PASS**（BUILD SUCCESSFUL） |
| 真实素材入库 | 真实音频复制到 `rawfile` 仅供本地构建，已加入 `.gitignore`，不入库（遵守硬约束 6） |

### 3.8 任务 6.2 / 6.3 真机听测

设备：HUAWEI MRT-AL10（序列号 `5KTUT25424016774`），OpenHarmony 7.0.0.111(SP12C00E111R5P2)，API 26。

| 检查项 | 实测结果 |
|---|---|
| 6.2 真实 A/B（bgm03_2） | 人类判定「**播放效果良好**」：A→B 切换与循环边界均无间隙、无爆音 |
| 6.3 真实单文件（flower.mp3） | 人类判定「**播放效果良好**」：循环边界无间隙、无爆音 |
| 合成 A/B | 人类判定「播放效果良好」 |
| 欠载 | 页面「欠载 0 字节」 |

> 说明：本次听测由人类在真机上完成并给出「播放效果良好」的判定，未逐项记录循环次数与欠载字节数的截图。按 AGENTS.md 第 5 节，听感结论与听测设备已写明。

### 3.9 测试过程中发现并修正的缺陷

| 编号 | 缺陷 | 处理 |
|---|---|---|
| B11 | 任务 3.1 首版后缀匹配在完整路径上做，`bgm03_2_a.ogg` 永远不匹配 | 确认为**实现缺陷**：新增 `SplitPath()` 拆成 `dir`/`stem`/`ext`，后缀判断与替换都在 stem 上做 |
| B12 | 任务 3.1 二跑 `testUppercasePair` 2 处失败：`_A` 得到 `_b`（小写） | 确认为**实现缺陷**：替换时硬编码小写后缀。新增 `TailIsUpperCase()` + `CaseAdjusted()`，保持输入大小写风格 |
| B13 | 任务 3.2 三跑 `testMixedCasePair` 失败：断言要求 `_A` → `_b`（混搭） | 确认为**断言错误**：规范 P01 只要求「匹配大小写不敏感」，未要求混搭配对。改为 `testCaseInsensitiveMatching`，断言 `_A`→`_B`、`_a`→`_b`、`_Start`→`_end` |
| B14 | 任务 5.2 新增 `testAbPairLoopsOnVirtualTimeline` 首跑失败：`LoopCount() >= 2` 在 `Stop()` 之后断言 | 确认为**断言错误**：`Stop()` 会重置循环计数。改为在 `Stop()` 前取快照 |
| B15 | 任务 6.1 后 `verify.ps1` 跑出 `TransportTests.testSeekResetsLoopCount` 失败（10933 vs 12981） | 确认为**测试竞态**：`PlaybackFrame()` 与 `EstimatePlaybackFrame()` 是两次独立快照，中间解码线程继续预填 FIFO。改为区间断言 `[1000, 1000+88200]`，连续 5 次运行均通过 |

## 4. 偏差与风险

| 编号 | 偏差 | 等级 | 处理 |
|---|---|---|---|
| D1 | `水月陵 - flower.mp3` 的 `totalFrames=4869551` 但顺序解码 `decodedFrames=4868399`，差 1152 帧 | 一般 | dr_mp3 已知差异（末帧 padding 处理）。`LoopEnd` 归一化会 clamp 到 Total，不影响循环；真实素材验收以解码器实测值为准 |
| D2 | `dohnadohna_08.ogg` / `music03_03.ogg` 的 DB TotalSamples 与解码帧数差 1~2 帧 | 一般 | OGG granule position 与解码器输出的固有差异，非缺陷；`LoopEnd` clamp 已覆盖 |
| D3 | `BGM_016_A/B.ogg` 解码后逐帧相同，无法验证 A→B 切换 | 一般 | 已由 `bgm03_2_a/b.ogg` 替代，不影响验收 |
| D4 | 真实素材复制到 `rawfile` 供真机听测，与硬约束 6「禁止提交真实音乐文件」存在张力 | 一般 | 已加入 `.gitignore`，真实音频不入库；仅本地构建产物包含 |
| D5 | 6.2 / 6.3 听测未逐项记录循环次数与欠载字节数截图 | 一般 | 人类判定「播放效果良好」已记录；如需精确数值证据，可后续补测 |
| D6 | A/B 切换瞬间的 seek 开销未单独量化 | 一般 | 切换发生在 `ReadFrames` 内部，不清空 FIFO；真机听测未察觉中断 |

## 5. 结论

- [x] Ready for archive
- [ ] 需修复后复验
- [ ] 需人类决策

**说明**：本变更 7 组 22 个任务全部完成。12 个 native 测试套件 / 1325 断言全通过，`openspec validate --all --strict` 通过，HAP 构建成功，真机听测由人类判定「播放效果良好」。已知偏差均为素材固有差异或证据粒度问题，不构成阻断。
