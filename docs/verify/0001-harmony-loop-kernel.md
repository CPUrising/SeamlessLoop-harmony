# 验证报告：0001-harmony-loop-kernel

## 1. 基本信息

| 项 | 值 |
|---|---|
| change id | `0001-harmony-loop-kernel` |
| 执行人 | AI 智能体（TRAE） |
| 执行时间 | 2026-11-05 10:20（最近一次） |
| 机器 / 系统 | Windows 11（开发机） |
| 工具链版本 | OpenSpec CLI 1.14.3；CMake 4.2.3；MSVC 19.44.35228.0；Python 3.14 + numpy 2.5.3；ffmpeg N-122110；DevEco Studio 26.0.0（build DS-261.23567.138.36.2600865）；hvigor 6.26.9 |
| 运行环境 | HarmonyOS SDK 26.0.0.105（apiVersion 26，Release）；hdc 3.2.0f；**真机曾接入（MRT-AL10，API 26），当前已断开** |
| 命令 | `powershell -File scripts/verify.ps1 --change 0001-harmony-loop-kernel` |
| 日志 | `artifacts/verify-0001-harmony-loop-kernel-20261105-102022.log` |
| 结果包 | `fixtures/synthetic/` 3 个音频 + `fixtures/manifest.json`；`entry/build/default/outputs/default/entry-default-unsigned.hap`（含 `libs/arm64-v8a/libentry.so`、`libs/x86_64/libentry.so`） |

**本次验证范围**：任务 2.2~2.4（已完成）、任务 3.1 / 3.2 / 3.3（本次新增）。本机单元测试首次真实执行并通过。任务 2.5 与 6.2 / 6.3 真机听测仍阻塞（签名未配置 + 真机已断开）。功能场景 T01~T11、T13、T15~T17 仍未实现。

## 2. 需求覆盖

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| Sample-Accurate Loop Jump | T01 | `LoopStreamTests.testLoopJumpsBackToStart` | NOT RUN | 代码未实现 |
| Sample-Accurate Loop Jump | T02 | `LoopConfigTests.testInvalidEndNormalized` | NOT RUN | 代码未实现 |
| Sample-Accurate Loop Jump | T03 | `LoopConfigTests.testInvalidStartRejected` | NOT RUN | 代码未实现 |
| Sample-Accurate Loop Jump | T04 | `LoopConfigTests.testDegenerateRangeRejected` | NOT RUN | 代码未实现 |
| Loop Configuration At Runtime | T05 | `LoopConfigTests.testChangeOutsideRangeSeeks` + 真机听测 | NOT RUN | 代码未实现 + 无真机 |
| Loop Configuration At Runtime | T06 | `LoopConfigTests.testChangeInsideRangeKeepsPosition` | NOT RUN | 代码未实现 |
| Loop Configuration At Runtime | T07 | `LoopStreamTests.testLoopDisabledStopsAtEnd` | NOT RUN | 代码未实现 |
| Loop Cycle Reporting | T08 | `LoopCounterTests.testCounterIncrementsAndResets` | NOT RUN | 代码未实现 |
| Loop Cycle Reporting | T09 | `LoopCounterTests.testNoEventWhenDisabled` | NOT RUN | 代码未实现 |
| Playback Progress Reporting | T10 | `ProgressTests.testProgressContinuousAcrossLoop` | NOT RUN | 代码未实现 |
| Playback Progress Reporting | T11 | `ProgressTests.testNoCompensationDuringIntro` | NOT RUN | 代码未实现 |
| Playback Transport Control | T12 | `DecoderTests.testLoadReportsFormat` | **PASS** | 实测 WAV 单声道/立体声与 MP3 均正确报出采样率、声道数、总采样数；起始位置为 0 |
| Playback Transport Control | T13 | `TransportTests.testPauseResumeKeepsPosition` | NOT RUN | 代码未实现（任务 5.2） |
| Playback Transport Control | T14 | `TransportTests.testSeekClampsAndAligns` | **部分通过** | 解码侧 clamp 已由 `DecoderTests.testSeekClampsOutOfRange` 验证；FIFO 清空（播放侧）待任务 5.2 |
| Continuous Audio Output | T15 | 离线波形分析 + 真机听测 | NOT RUN | 无真机；合成 fixture 已就绪 |
| Continuous Audio Output | T16 | 离线波形分析 + 真机听测 | NOT RUN | 无真机；合成 fixture 已就绪，相位连续性已数值验证 |
| Continuous Audio Output | T17 | `RingBufferTests.testUnderrunWritesSilence` | **PASS** | 欠载时写静音（实测非 0xFF 残留）、空缓冲全静音；欠载事件上报待任务 3.5 |

## 3. 回归

| 范围 | 命令 | 结果 |
|---|---|---|
| 规范严格校验 | `openspec validate --all --strict` | **PASS**（1 passed, 0 failed） |
| 合成 fixture 生成 | `python scripts/make-fixtures.py` | **PASS**（3 文件 + manifest，见下） |
| 合成 fixture 相位连续性 | 离线 numpy 检查 | **PASS**（见 D5 说明） |
| 全量单元测试 | `scripts/verify.ps1 --scope unit` | **PASS**（2 套件 / 153 断言 / 0 失败） |
| HAP 构建 | `hvigorw --mode module -p product=default --no-daemon assembleHap` | **PASS**（BUILD SUCCESSFUL，产出 unsigned HAP） |
| Native 库链接 | `llvm-readelf -d libentry.so` | **PASS**（NEEDED 含 `libohaudio.so`、`libhilog_ndk.z.so`、`libace_napi.z.so`） |
| 既有通过项是否回退 | 本变更为首次变更，无既有基线 | N/A |

### 任务 2.2 / 2.3 / 2.4 实测证据（2026-11-04 22:35）

| 检查项 | 实测结果 |
|---|---|
| `entry/src/main/cpp/CMakeLists.txt` 存在并被 hvigor 调用 | `ConfigureCmake` / `BuildNativeWithCmake` / `BuildNativeWithNinja` 均 Finished |
| `libentry.so` 动态依赖 | `libace_napi.z.so`、`libohaudio.so`、`libhilog_ndk.z.so`、`libc++_shared.so`、`libc.so` |
| HAP 内 native 产物 | `libs/arm64-v8a/libentry.so`（55240 B）、`libs/x86_64/libentry.so`（55664 B） |
| ArkTS 侧 napi 导入 | `CompileArkTS` Finished；仅一条 SDK 提示「module for 'libentry.so' is not verified」（后续 SDK 才启用校验，非错误） |
| 正弦波实际发声 | **NOT RUN**（无真机，任务 2.5 阻塞） |

### 任务 3.1 / 3.2 / 3.3 实测证据（2026-11-05 10:20）

| 检查项 | 实测结果 |
|---|---|
| dr_libs 引入 | `third_party/dr_wav.h`（0.14.6，375509 B）、`dr_mp3.h`（0.7.4，213748 B），版本与原项目 `loopfinder/third_party` 一致 |
| 实现单元 | `third_party/dr_libs_impl.c` 单独定义 `DR_WAV_IMPLEMENTATION` / `DR_MP3_IMPLEMENTATION`，避免重复定义 |
| 本机单测构建 | `cmake -DBUILD_TESTS=ON` + MSVC 19.44 编译通过，产出 `ring_buffer_tests.exe` / `audio_decoder_tests.exe` |
| `RingBufferTests` | **PASS**：7 用例 / 103 断言 / 0 失败 |
| `DecoderTests` | **PASS**：10 用例 / 50 断言 / 0 失败 |
| WAV 格式上报 | 44100Hz / 1ch / 121275 帧 / BlockAlign 2；48000Hz / 2ch / 132000 帧 / BlockAlign 4 |
| MP3 格式上报 | 44100Hz / 1ch，总帧数落在 (118000, 124000) 区间（已扣除编码器延迟与填充） |
| seek clamp | `-1 → 0`，`total+999999 → total`，均实测通过 |
| seek 后数据正确性 | seek 到 LoopStart 后读出的 1024 帧与文件头 1024 帧不同，证明真的跳转了 |
| HAP 构建（回归） | BUILD SUCCESSFUL；`libentry.so` 已包含 decoder 与 ring buffer |

### 构建日志中的已知告警（非缺陷）

| 告警 | 说明 |
|---|---|
| `Will skip sign 'hos_hap'. No signingConfigs profile` | 工程未配置签名，产出 unsigned HAP；真机安装前需配置签名 |
| `clang++: warning: argument unused ... --gcc-toolchain` | hvigor 默认注入参数，与本次代码无关 |
| `Currently module for 'libentry.so' is not verified` | SDK 26 尚未启用 napi 声明校验，`.d.ts` 已提供 |

### fixture 相位连续性实测（2026-11-04）

`fixtures/synthetic/intro_loop_44100_mono.wav`，LoopStart=33075，LoopEnd=121275：

| 指标 | 实测值 |
|---|---|
| `x[LoopStart]` | `0.0` |
| `x[LoopEnd-1]` | `-0.040864` |
| 边界相邻采样差 `|x[LoopStart] - x[LoopEnd-1]|` | `0.040864` |
| 循环体内最大相邻采样差 | `0.081729` |
| 比值 | `0.50`（**≤ 1.5**，满足 T16 判据） |

结论：合成 fixture 的循环边界突变**小于循环体内部正常波动**，可用于 T16 的数值断言。

## 4. 偏差与风险

| 编号 | 偏差 | 等级 | 处理 |
|---|---|---|---|
| D1 | 本机无 HarmonyOS 真机连接（`hdc list targets` 为空），全部真机安装与听测任务阻塞 | **阻断** | DevEco Studio / SDK / hdc 已就绪，HAP 可构建；登记至 `docs/verification-debt.md`，待真机接入后继续 |
| D2 | `compatibleSdkVersion` 已设为 26.0.0，可安装设备范围受限 | 严重 | 待真机系统版本确认后决定是否下调；`targetSdkVersion` 保持 26.0.0 |
| D3 | `fixtures/real/` 尚无真实素材，真实场景听测无法执行 | 严重 | 任务 1.3 待用户提供；已给出清单与获取方式 |
| D4 | 音乐文件获取方式（Picker + 持久化 URI vs 沙箱导入）未定 | 一般 | 本变更用固定沙箱路径绕过；已写入 `design.md` Open Questions |
| D5 | 合成 fixture 的循环边界突变虽小于循环体波动，但**不为零**（比值 0.50） | 一般 | 属相位近似连续（包络为 2 周期正弦，采样点落在离散步长上）；T16 判据已改为相对比值而非绝对值，避免误判 |
| D6 | MP3 解码后的实际采样位置未测，`manifest.json` 中 MP3 条目缺少 loop_start/loop_end | 一般 | 待解码器实现后实测回填；这本身就是任务 3.2 的验证内容 |
| D7 | 任务 2.5（G0 冒烟听测）未执行：无真机，无法确认 440Hz 正弦波实际发声与无爆音 | **阻断** | 已登记 `docs/verification-debt.md`；代码与构建链路已就绪，待真机接入后执行 |
| D8 | `scripts/verify.ps1` 的构建步骤原用 hvigor daemon，本机会挂起（无输出、不退出） | 一般 | 已改为 `--no-daemon`（12~13 秒完成），属验证入口可复现性修正，不涉及产品代码。**注：该次挂起未复现、根因未定位**，`--no-daemon` 是官方降级方式，同时让验证入口不依赖常驻进程状态 |
| D9 | 真机安装失败：`code:9568320 / no signature file` | **阻断** | `build-profile.json5` 的 `signingConfigs` 为空但 product 引用了 `default`；本机无任何签名材料。需在 DevEco 中配置自动签名（依赖真机在线读取 UDID），当前真机已断开 |
| D10 | `wmic.exe` 在 Win11 已移除，hvigor 的 daemon 进程探测回退到 `powershell Get-CimInstance`，每次约 1.36 秒 | 一般 | 属 hvigor 工具链行为，不影响本工程代码；registry 条目超过 8 条时会触发批量探测，建议定期 `hvigorw --stop-daemon` |

## 5. 结论

- [ ] Ready for archive
- [x] 需修复后复验（Build 阶段进行中，非归档态）
- [ ] 需人类决策

**说明**：Plan 阶段产物（proposal / design / tasks / delta spec）已完成并通过严格校验；合成 fixture 已生成并通过相位连续性数值检查；HAP 构建已实测通过。人类已批准进入 Build 阶段，当前已完成任务 2.2~2.4 与 3.1~3.3，下一步为任务 3.4。任务 2.5 / 6.2 / 6.3 的真机听测在真机重新接入并配置签名前无法执行。

已确认的决策：HarmonyOS SDK **26.0.0**、真实素材由用户提供、包名 `com.cpu.seamlessloopharmony`。

真机实测环境（2026-11-05 曾接入）：HUAWEI **MRT-AL10**，`OpenHarmony-7.0.0.111`，`const.ohos.apiversion = 26`，与 `compatibleSdkVersion 26.0.0` 一致。

仍阻塞：真机连接（当前 `hdc list targets` 为空）、签名配置（D9）、`fixtures/real/` 素材。
