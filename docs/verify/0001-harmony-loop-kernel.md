# 验证报告：0001-harmony-loop-kernel

## 1. 基本信息

| 项 | 值 |
|---|---|
| change id | `0001-harmony-loop-kernel` |
| 执行人 | AI 智能体（TRAE） |
| 执行时间 | 2026-11-05 11:53（最近一次） |
| 机器 / 系统 | Windows 11（开发机） |
| 工具链版本 | OpenSpec CLI 1.14.3；CMake 4.2.3；MSVC 19.44.35228.0；Python 3.14 + numpy 2.5.3；ffmpeg N-122110；DevEco Studio 26.0.0（build DS-261.23567.138.36.2600865）；hvigor 6.26.9 |
| 运行环境 | HarmonyOS SDK 26.0.0.105（apiVersion 26，Release）；hdc 3.2.0f；**真机在线（MRT-AL10，序列号 5KTUT25424016774，API 26，系统 7.0.0.111(SP12C00E111R5P2)）** |
| 命令 | `powershell -File scripts/verify.ps1 --change 0001-harmony-loop-kernel` |
| 日志 | `artifacts/verify-0001-harmony-loop-kernel-20261105-115309.log` |
| 结果包 | `fixtures/synthetic/` 3 个音频 + `fixtures/manifest.json`；`entry/build/default/outputs/default/entry-default-signed.hap`（3 364 578 B，含 `libs/arm64-v8a/libentry.so`、`libs/x86_64/libentry.so`） |

**本次验证范围**：任务 1.1、2.2~2.5、3.1~3.5、4.1 已完成。本机单元测试 5 套件 / 870 断言全通过；真机 G0 冒烟（2.5）已实测通过。任务 6.2 / 6.3 真机听测仍阻塞（循环播放链路未实现）。功能场景 T01、T05~T11、T13、T15~T16 仍未实现。

## 2. 需求覆盖

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| Sample-Accurate Loop Jump | T01 | `LoopStreamTests.testLoopJumpsBackToStart` | NOT RUN | 代码未实现 |
| Sample-Accurate Loop Jump | T02 | `LoopConfigTests.testInvalidEndNormalized` | **部分通过** | 终点 0 / -1 / -4096 / Total+1 / Total×2 均归一化为 Total，起点保持不变，错误回调未触发；「播放不中断」属集成行为，待任务 4.2 验证 |
| Sample-Accurate Loop Jump | T03 | `LoopConfigTests.testInvalidStartRejected` | **PASS** | 起点 -1 / -Total / Total / Total+1 均返回 `kInvalidStart`，原区间保持不变，回调收到 4 次 `kInvalidStart` |
| Sample-Accurate Loop Jump | T04 | `LoopConfigTests.testDegenerateRangeRejected` | **PASS** | `End == Start`、`End < Start`、`End=1 < Start` 三种退化形态均被拒绝，原区间不变，不进入空区间 |
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
| Continuous Audio Output | T17 | `RingBufferTests` + `AudioOutputLogicTests` | **部分通过** | 第 1 条（静音填充）、第 2 条（恢复后继续播放）已通过；第 3 条（事件上报）代码未接线 |

## 3. 回归

| 范围 | 命令 | 结果 |
|---|---|---|
| 规范严格校验 | `openspec validate --all --strict` | **PASS**（1 passed, 0 failed） |
| 合成 fixture 生成 | `python scripts/make-fixtures.py` | **PASS**（3 文件 + manifest，见下） |
| 合成 fixture 相位连续性 | 离线 numpy 检查 | **PASS**（见 D5 说明） |
| 全量单元测试 | `scripts/verify.ps1 --scope unit` | **PASS**（5 套件 / 870 断言 / 0 失败） |
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
| 正弦波实际发声 | **PASS**（2026-11-05 12:19 真机实测，见下节「任务 2.5」） |

### 任务 2.5 真机 G0 冒烟（2026-11-05 12:18~12:20）

| 检查项 | 实测结果 |
|---|---|
| 设备 | HUAWEI MRT-AL10，序列号 `5KTUT25424016774`，`const.ohos.apiversion = 26`，`MRT-AL10 7.0.0.111(SP12C00E111R5P2)` |
| 安装 | `hdc install -r entry-default-signed.hap` → `install bundle successfully` |
| 启动 | `aa start -a EntryAbility -b com.cpu.seamlessloopharmony` → `start ability successfully`，进程存活（pid 31292） |
| 渲染器启动 | `SLH_AudioOutput: renderer started: 48000 Hz, 2 ch, 440 Hz tone fallback`；`SLH_Index: startTone ret=0` |
| 渲染器停止 | `SLH_AudioOutput: renderer stopped, total underrun bytes=0`（连续 5 次启停均为 0 欠载） |
| 听感 | 人类听测判定：**清晰 1 秒 440Hz 正弦音，无爆音** |
| 结论 | 任务 2.5 **通过**；OHAudio 构建 → 链接 → 真机发声链路闭环 |

### 任务 3.1 / 3.2 / 3.3 实测证据（2026-11-05 10:20）

| 检查项 | 实测结果 |
|---|---|
| dr_libs 引入 | `third_party/dr_wav.h`（0.14.6，375509 B）、`dr_mp3.h`（0.7.4，213748 B），版本与原项目 `loopfinder/third_party` 一致 |
| 实现单元 | `third_party/dr_libs_impl.c` 单独定义 `DR_WAV_IMPLEMENTATION` / `DR_MP3_IMPLEMENTATION`，避免重复定义 |
| 本机单测构建 | `cmake -DBUILD_TESTS=ON` + MSVC 19.44 编译通过，产出 `ring_buffer_tests.exe` / `audio_decoder_tests.exe` |
| `RingBufferTests` | **PASS**：9 用例 / 184 断言 / 0 失败（含 T17 第 1、2 条） |
| `DecoderTests` | **PASS**：10 用例 / 50 断言 / 0 失败 |
| WAV 格式上报 | 44100Hz / 1ch / 121275 帧 / BlockAlign 2；48000Hz / 2ch / 132000 帧 / BlockAlign 4 |
| MP3 格式上报 | 44100Hz / 1ch，总帧数落在 (118000, 124000) 区间（已扣除编码器延迟与填充） |
| seek clamp | `-1 → 0`，`total+999999 → total`，均实测通过 |
| seek 后数据正确性 | seek 到 LoopStart 后读出的 1024 帧与文件头 1024 帧不同，证明真的跳转了 |
| HAP 构建（回归） | BUILD SUCCESSFUL；`libentry.so` 已包含 decoder 与 ring buffer |

### 任务 3.4 / 3.5 实测证据（2026-11-05 11:24）

| 检查项 | 实测结果 |
|---|---|
| `DecodeThreadTests` | **PASS**：7 用例 / 34 断言 / 0 失败 |
| `AudioOutputLogicTests` | **PASS**：9 用例 / 528 断言 / 0 失败 |
| `RingBufferTests` | **PASS**：9 用例 / 184 断言 / 0 失败 |
| T17 第 1 条（静音填充） | 欠载时写 0 而非残留 0xFF；空缓冲全静音 |
| T17 第 2 条（恢复后继续播放） | `testRecoversAfterUnderrun`：读空 16 字节 → 欠载 → 写入 20 字节 → 正确读出 20 字节；`testRecoversAfterUnderrunAcrossWrap`：欠载点跨环绕边界后同样正确恢复 |
| 解码完整性 | 44.1kHz 单声道 fixture 解出 **121275 帧**，与 manifest 的 `total_samples` 完全一致 |
| 立体声字节数 | 48kHz 立体声 fixture 解出 132000 帧 → 消费者读到 **528000 字节**（= 132000 × 4），BlockAlign 换算正确 |
| FIFO 满时行为 | 容量设为 64 帧时，FIFO 被精确写满到 128 字节且不再增长；解码线程保持运行（等待空间），未丢数据、未结束 |
| 停止语义 | `Stop()` 后 `DecodedFrames()` 在 100ms 内不再增长；`Start()` 可重复调用 |
| 空指针 / 未打开防护 | `Start()` 在 decoder 未打开、decoder 为 null、ring 为 null 三种情况下均返回 false，不启动线程 |
| 写回调规划守恒 | 9 组 size × 10 组 avail 全组合下，`bytesFromFifo + silenceBytes == audioDataSize` 恒成立；`bytesFromFifo` 始终 BlockAlign 对齐 |
| 非对齐请求 | `audioDataSize=1026`（非 4 的倍数）时取 1024 + 补静音 2，无未初始化内存 |
| 非法 BlockAlign | `blockAlign=0/-4` 时全部按静音处理，不越界读 |
| HAP 构建（回归） | BUILD SUCCESSFUL；新代码（decode_thread / audio_output_logic）已编入 `libentry.so` |

### 任务 4.1 实测证据（2026-11-05 11:53）

| 检查项 | 实测结果 |
|---|---|
| `LoopConfigTests` | **PASS**：9 用例 / 74 断言 / 0 失败（ctest 第 5 个套件） |
| 默认区间 | `LoopConfig(121275)` → `[0, 121275)`；`LoopConfig(-5)` → total 归一为 0，不产生负区间 |
| T02 归一化 | 终点 `0` / `-1` / `-4096` / `121276` / `242550` 五种取值全部归一化为 `121275`，起点保持 `33075`；错误回调计数为 0（归一化不是失败） |
| T02 边界 | 终点恰为 `Total` 或 `Total-1` 时保持原值，不被归一化改写 |
| T03 拒绝 | 起点 `-1` / `-121275` / `121275` / `121276` 均返回 `kInvalidStart`，原区间 `[33075, 90000)` 不变，回调收到 4 次 `kInvalidStart` |
| T04 拒绝 | `(50000,50000)`、`(50000,40000)`、`(40000,1)` 均返回 `kDegenerateRange`，原区间不变；失败后区间仍可正常使用 |
| 失败后恢复 | 连续两次拒绝后区间仍为满区间，随后 `SetRange(500, 121275)` 成功生效 |
| 重新加载 | `Reset(132000)` 后区间回到 `[0, 132000)` |
| 回调缺失 | 未登记回调时拒绝路径不崩溃，返回值仍为对应错误码 |
| T02「不中断播放」 | 本任务只保证归一化不产生拒绝；实际播放连续性由任务 4.2 的循环跳转验证 |
| HAP 构建（回归） | BUILD SUCCESSFUL；`loop_config.cpp` 已编入 `libentry.so` |

### 测试过程中发现并修正的缺陷

| 编号 | 缺陷 | 处理 |
|---|---|---|
| B1 | 首版 `PlanWrite` 的测试断言把 `droppedTailBytes` 重复计入守恒式（`1024+2+2=1028 ≠ 1026`） | 确认为**测试断言错误**，实现本身正确。已在 `audio_output_logic.h` 明确 `droppedTailBytes` 是 `silenceBytes` 的子集、不参与求和；断言改为 `bytesFromFifo + silenceBytes == size`，并新增「子集关系」断言防止回归 |

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
| D1 | 真机曾断开（`hdc list targets` 为空），真机安装与听测无法执行 | 已解除 | 2026-11-05 真机 MRT-AL10 重新接入；安装、启动与 G0 正弦波听测均通过。循环听测仍待功能实现 |
| D2 | `compatibleSdkVersion` 已设为 26.0.0，可安装设备范围受限 | 一般 | 真机 `const.ohos.apiversion = 26` 已确认一致，维持不下调；若后续需覆盖更低版本设备，另开 change |
| D3 | `fixtures/real/` 尚无真实素材，真实场景听测无法执行 | 严重 | 任务 1.3 待用户提供；已给出清单与获取方式 |
| D4 | 音乐文件获取方式（Picker + 持久化 URI vs 沙箱导入）未定 | 一般 | 本变更用固定沙箱路径绕过；已写入 `design.md` Open Questions |
| D5 | 合成 fixture 的循环边界突变虽小于循环体波动，但**不为零**（比值 0.50） | 一般 | 属相位近似连续（包络为 2 周期正弦，采样点落在离散步长上）；T16 判据已改为相对比值而非绝对值，避免误判 |
| D6 | MP3 解码后的实际采样位置未测，`manifest.json` 中 MP3 条目缺少 loop_start/loop_end | 一般 | 待解码器实现后实测回填；这本身就是任务 3.2 的验证内容 |
| D7 | 任务 2.5（G0 冒烟听测）未执行：无真机，无法确认 440Hz 正弦波实际发声与无爆音 | 已解除 | 2026-11-05 12:19 真机实测：听到清晰 1 秒正弦音、无爆音；`renderer started: 48000 Hz, 2 ch`，`total underrun bytes=0` |
| D8 | `scripts/verify.ps1` 的构建步骤原用 hvigor daemon，本机会挂起（无输出、不退出） | 一般 | 已改为 `--no-daemon`（12~13 秒完成），属验证入口可复现性修正，不涉及产品代码。**注：该次挂起未复现、根因未定位**，`--no-daemon` 是官方降级方式，同时让验证入口不依赖常驻进程状态 |
| D9 | 真机安装失败：`code:9568320 / no signature file` | 已解除 | 人类已在根 `build-profile.json5` 配置 `signingConfigs`；2026-11-05 12:18 signed HAP 安装成功 |
| D10 | `wmic.exe` 在 Win11 已移除，hvigor 的 daemon 进程探测回退到 `powershell Get-CimInstance`，每次约 1.36 秒 | 一般 | 属 hvigor 工具链行为，不影响本工程代码；registry 条目超过 8 条时会触发批量探测，建议定期 `hvigorw --stop-daemon` |

## 5. 结论

- [ ] Ready for archive
- [x] 需修复后复验（Build 阶段进行中，非归档态）
- [ ] 需人类决策

**说明**：Plan 阶段产物（proposal / design / tasks / delta spec）已完成并通过严格校验；合成 fixture 已生成并通过相位连续性数值检查；HAP 构建、真机安装、启动与 G0 正弦波听测均已实测通过。当前已完成任务 1.1、2.2~2.5、3.1~3.5 与 4.1，下一步为任务 4.2。任务 6.2 / 6.3 的循环听测需等循环播放链路与最小界面（4.2~6.1）完成后再执行。

已确认的决策：HarmonyOS SDK **26.0.0**、真实素材由用户提供、包名 `com.cpu.seamlessloopharmony`。

真机实测环境：HUAWEI **MRT-AL10**（序列号 `5KTUT25424016774`），`OpenHarmony-7.0.0.111(SP12C00E111R5P2)`，`const.ohos.apiversion = 26`，与 `compatibleSdkVersion 26.0.0` 一致；签名已配置，signed HAP 安装成功。

仍未完成：`fixtures/real/` 真实素材（任务 1.3）；循环边界听测（T15 / T16 / T05，随 6.2 / 6.3）。
