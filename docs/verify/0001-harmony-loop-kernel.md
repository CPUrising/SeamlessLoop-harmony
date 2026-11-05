# 验证报告：0001-harmony-loop-kernel

## 1. 基本信息

| 项 | 值 |
|---|---|
| change id | `0001-harmony-loop-kernel` |
| 执行人 | AI 智能体（TRAE） |
| 执行时间 | 2026-11-05 18:55（最近一次） |
| 机器 / 系统 | Windows 11（开发机） |
| 工具链版本 | OpenSpec CLI 1.14.3；CMake 4.2.3；MSVC 19.44.35228.0；Python 3.14 + numpy 2.5.3；ffmpeg N-122110；DevEco Studio 26.0.0（build DS-261.23567.138.36.2600865）；hvigor 6.26.9 |
| 运行环境 | HarmonyOS SDK 26.0.0.105（apiVersion 26，Release）；hdc 3.2.0f；**真机在线（MRT-AL10，序列号 5KTUT25424016774，API 26，系统 7.0.0.111(SP12C00E111R5P2)）** |
| 命令 | `powershell -File scripts/verify.ps1 --change 0001-harmony-loop-kernel` |
| 日志 | `artifacts/verify-0001-harmony-loop-kernel-20261105-185518.log` |
| 结果包 | `fixtures/synthetic/` 3 个音频 + `fixtures/manifest.json`；`entry/build/default/outputs/default/entry-default-signed.hap`（3 364 578 B，含 `libs/arm64-v8a/libentry.so`、`libs/x86_64/libentry.so`） |

**本次验证范围**：任务 1.1、2.2~2.5、3.1~3.5、4.1~4.5、5.1~5.2 已完成。本机单元测试 10 套件 / 1172 断言全通过；真机 G0 冒烟（2.5）已实测通过。任务 6.2 / 6.3 真机听测仍阻塞（循环播放链路未完全接通 UI）。功能场景 T15~T16 仍未实现；T05 的 200ms 听感条款待 6.3。

## 2. 需求覆盖

| Requirement | Scenario | 测试方法 | 结果 | 备注 |
|---|---|---|---|---|
| Sample-Accurate Loop Jump | T01 | `DecodeThreadTests.testLoopJumpsBackToStart` | **PASS** | 输出等于「文件头到 LoopEnd」+ 两段 `[LoopStart, LoopEnd)` 的逐字节副本；解码器全程保持打开，未重开文件 |
| Sample-Accurate Loop Jump | T02 | `LoopConfigTests.testInvalidEndNormalized` | **部分通过** | 终点 0 / -1 / -4096 / Total+1 / Total×2 均归一化为 Total，起点保持不变，错误回调未触发；「播放不中断」需运行时设置链路（任务 4.4 / 5.3） |
| Sample-Accurate Loop Jump | T03 | `LoopConfigTests.testInvalidStartRejected` | **PASS** | 起点 -1 / -Total / Total / Total+1 均返回 `kInvalidStart`，原区间保持不变，回调收到 4 次 `kInvalidStart` |
| Sample-Accurate Loop Jump | T04 | `LoopConfigTests.testDegenerateRangeRejected` | **PASS** | `End == Start`、`End < Start`、`End=1 < Start` 三种退化形态均被拒绝，原区间不变，不进入空区间 |
| Loop Configuration At Runtime | T05 | `LoopRangeChangeTests.testChangeOutsideRangeSeeks` + `testChangeOutsideRangeFlushesBufferedAudio` + 真机听测 | **部分通过** | 改点后跳转落点与 FIFO 清空已实测；200ms 无感中断属听感条款，待任务 6.3 |
| Loop Configuration At Runtime | T06 | `LoopRangeChangeTests.testChangeInsideRangeKeepsPosition` | **PASS** | 位置仍在新区间内时不触发清空与跳转，解码持续推进 |
| Loop Configuration At Runtime | T07 | `LoopStreamTests.testLoopDisabledStopsAtEnd` | **PASS** | 循环播放中关闭循环后不再跳转，播到末尾停止推进，结束回调恰好触发一次；排空后写回调持续输出静音 |
| Loop Cycle Reporting | T08 | `LoopCounterTests.testCounterIncrementsAndResets` | **PASS** | 真实循环跳转下计数与回调累计值一致（1,2,3…单调递增）；循环点变更后归零；`ResetLoopCount()` 后可重新从 1 计数 |
| Loop Cycle Reporting | T09 | `LoopCounterTests.testNoEventWhenDisabled` | **PASS** | 设置区间但不开启循环，整曲播到末尾，`LoopCount == 0` 且回调一次未触发 |
| Playback Progress Reporting | T10 | `ProgressTests.testProgressMonotonicAcrossRealLoops` | **PASS** | 真实循环 3 次以上，5ms 采样 45+ 次，进度单调不减、单步前跳未超过一个循环长度 |
| Playback Progress Reporting | T11 | `ProgressTests.testNoCompensationDuringIntro` | **PASS** | Intro 阶段（跳转次数 0）进度严格等于解码位置；集成用例中 `PlaybackFrame() == EstimatePlaybackFrame()` |
| Playback Transport Control | T12 | `DecoderTests.testLoadReportsFormat` | **PASS** | 实测 WAV 单声道/立体声与 MP3 均正确报出采样率、声道数、总采样数；起始位置为 0 |
| Playback Transport Control | T13 | `TransportTests.testPauseResumeKeepsPosition` | **PASS** | 暂停后解码位置 100ms 内不推进；恢复后从暂停点继续，偏差 < 4410 帧（100ms @44.1kHz） |
| Playback Transport Control | T14 | `TransportTests.testSeekClampsAndAligns` + `testSeekWhilePlayingFlushesBuffer` | **PASS** | 未播放时 seek 精确落点；`-1 → 0`、`total+999999 → total`；播放中 seek 后 FIFO 丢弃请求被消费者应用，无残留 |
| Continuous Audio Output | T15 | 离线波形分析 + 真机听测 | NOT RUN | 无真机；合成 fixture 已就绪 |
| Continuous Audio Output | T16 | 离线波形分析 + 真机听测 | NOT RUN | 无真机；合成 fixture 已就绪，相位连续性已数值验证 |
| Continuous Audio Output | T17 | `RingBufferTests` + `AudioOutputLogicTests` | **部分通过** | 第 1 条（静音填充）、第 2 条（恢复后继续播放）已通过；第 3 条（事件上报）代码未接线 |

## 3. 回归

| 范围 | 命令 | 结果 |
|---|---|---|
| 规范严格校验 | `openspec validate --all --strict` | **PASS**（1 passed, 0 failed） |
| 合成 fixture 生成 | `python scripts/make-fixtures.py` | **PASS**（3 文件 + manifest，见下） |
| 合成 fixture 相位连续性 | 离线 numpy 检查 | **PASS**（见 D5 说明） |
| 全量单元测试 | `scripts/verify.ps1 --scope unit` | **PASS**（10 套件 / 1172 断言 / 0 失败） |
| HAP 构建 | `hvigorw --mode module -p product=default --no-daemon assembleHap` | **PASS**（BUILD SUCCESSFUL，产出 signed HAP） |
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

### 任务 4.2 实测证据（2026-11-05 13:42）

| 检查项 | 实测结果 |
|---|---|
| `DecodeThreadTests` | **PASS**：12 用例 / 82 断言 / 0 失败（原 7 用例 / 34 断言，新增 5 个循环用例） |
| T01 循环拼接（单声道） | `testLoopJumpsBackToStart`：44.1kHz 单声道，区间 `[33075, 121275)`；输出前 121275 帧与原始 PCM `memcmp == 0`，其后两段各 88200 帧同样逐字节一致 |
| 短循环体 | `testShortLoopRangeRepeatsBody`：区间 `[33075, 34075)` 仅 1000 帧（小于 chunk 4096），连续 3 次跳转均精确重复循环体，未多读越过 LoopEnd |
| 立体声对齐 | `testStereoLoopKeepsFrameAlignment`：48kHz 立体声 BlockAlign=4，区间 `[36000, 132000)`；拼接头 528000 字节与循环体 384000 字节均与原始 PCM 一致，偏移均为 4 的整数倍 |
| 不重开文件 | 循环若干个周期后 `dec.IsOpen() == true`，`totalFrames` 不变；循环跳转只调用 `SeekToFrame`，不重建渲染器、不重启音频流 |
| 区间校验 | `testLoopRangeValidation`：负起点、退化区间、超出总帧数一律拒绝；未设置区间时 `SetLooping(true)` 不生效 |
| 循环关闭回归 | `testLoopRangeWithoutLoopingPlaysToEnd`：设置区间但不开启循环，仍播到文件末尾（`DecodedFrames == total`，`ReachedEnd == true`） |
| HAP 构建（回归） | BUILD SUCCESSFUL；`decode_thread.cpp` 的循环跳转已编入 `libentry.so` |

### 任务 4.3 实测证据（2026-11-05 13:53）

| 检查项 | 实测结果 |
|---|---|
| `LoopCounterTests` | **PASS**：7 用例 / 46 断言 / 0 失败（新增第 6 个测试套件） |
| T08 计数单调递增 | 真实解码线程循环 `[33075, 121275)`，连续跳转后 `LoopCount` 与回调累计值一致，回调序列严格为 1,2,3…（`sequenceBroken == 0`） |
| T08 循环点变更重置 | `SetLoopRange(40000, total)` 生效后 `LoopCount == 0`；重新启动后从 1 重新计数 |
| T08 显式重置 | `ResetLoopCount()` 后 `LoopCount == 0`，供任务 5.2 的 seek 调用 |
| 中途开启循环 | 未开启时 `LoopCount == 0`；开启后仅统计开启之后的跳转 |
| T09 关闭循环不上报 | 设置区间但不开启循环，整曲播完 `LoopCount == 0`、回调触发 0 次、`DecodedFrames == total` |
| 回调并发约定 | 回调在解码线程同步执行；计数读写全部使用原子变量，回调未登记时递增不崩溃 |
| HAP 构建（回归） | BUILD SUCCESSFUL；`loop_counter.h` 与计数逻辑已编入 `libentry.so` |

### 任务 4.4 实测证据（2026-11-05 18:22）

| 检查项 | 实测结果 |
|---|---|
| `LoopRangeChangeTests` | **PASS**：5 用例 / 43 断言 / 0 失败（新增第 7 个测试套件） |
| T05 跳转落点 | 原区间 `[33075, 40000)` 播放中改为 `[80000, 121275)`；改点后消费者读到的第一个采样等于 fixture 中 `all[80000]`，证明跳转精确落在新区间起点 |
| T05 FIFO 清空 | `RequestFlush()` 由消费者在 `Read()` 时应用；`FlushCount` 递增且 `HasPendingFlush()` 回到 false，旧区间残留数据不会播出 |
| T05 计数归零 | 改点后回调重新从 1 报数（`sawRestartFromOne`），不是接着旧计数递增 |
| T06 不跳转 | 原区间 `[0, 30000)` 改为 `[0, 121275)`（仍含当前位置）：`FlushCount` 不变、无挂起丢弃、解码帧数持续增长 |
| 循环关闭时改点 | 仅更新区间与计数，不触发清空或跳转，普通播放不被打断 |
| 改点后仍可循环 | 新区间 `[60000, 121275)` 能再次触发跳转，`LoopCount` 从 1 重新累计 |
| 数据新鲜度防护 | 解码线程记录读前 `FlushRequestCount()` 代次；解码耗时期间若发生改点/seek，该块数据整块丢弃，避免残留旧音频（本任务修复的实现缺陷） |
| HAP 构建（回归） | BUILD SUCCESSFUL；运行时改点逻辑已编入 `libentry.so` |

### 任务 4.5 实测证据（2026-11-05 18:37）

| 检查项 | 实测结果 |
|---|---|
| `LoopStreamTests` | **PASS**：5 用例 / 36 断言 / 0 失败（新增第 8 个测试套件） |
| T07 关闭循环后停止推进 | 循环 `[33075, 53075)` 播到 `LoopCount >= 2` 后调用 `SetLooping(false)`，随后不再跳转，解码推进到文件末尾并置 `ReachedEnd` |
| T07 结束回调 | 关闭循环后结束回调恰好触发 1 次；`Stop()` 之后再等 200ms 仍为 1 次，不重复上报 |
| T07 循环计数冻结 | 关闭循环后 `LoopCount` 不再变化（100ms 内保持同一值） |
| T07 静音输出 | 排空 FIFO 后 `ReadOrSilence` 返回有效字节 0，整个 1024 字节缓冲全为 0——写回调持续输出静音而非残留数据 |
| 循环期间无结束事件 | 循环中等到 `LoopCount >= 3` 仍未触发任何结束回调，`ReachedEnd == false` |
| 未登记回调 | 无结束回调时到达末尾不崩溃，`ReachedEnd` 正常置位 |
| HAP 构建（回归） | BUILD SUCCESSFUL；结束上报逻辑已编入 `libentry.so` |

### 任务 5.1 实测证据（2026-11-05 18:48）

| 检查项 | 实测结果 |
|---|---|
| `ProgressTests` | **PASS**：8 用例 / 45 断言 / 0 失败（新增第 9 个测试套件） |
| T11 纯函数 | 跳转次数为 0 时 `ComputeCompensatedDecodeFrame(frame, ...) == frame`，从 0 到 LoopStart 全程成立；跳转次数为负也不补偿 |
| T11 集成 | 真实解码线程在 Intro 阶段 `PlaybackFrame() == EstimatePlaybackFrame()`，即未叠加任何补偿 |
| T10 补偿量 | 跳转 1 次后解码位置回到 LoopStart 时进度为 LoopEnd；跳转 2 次为 `LoopEnd + 1×L`；跳转 3 次在任意位置为 `pos + 3×L` |
| T10 边界连续性 | 跳转前 `LoopEnd-1` → 跳转后 `LoopEnd`，单调不减且前跳为 1 帧，远小于一个循环长度 |
| T10 集成 | 真实循环跨越 3 次以上，5ms 采样 45+ 次：无回退、单步前跳均未超过一个循环长度 |
| 退化区间 | `loopEnd <= loopStart` 时不做补偿，返回原解码位置 |
| 缓冲扣减夹零 | `bufferedFrames >= decodedFrame` 时返回 0，不出现负进度 |
| 改点后清零 | 改点使计数归零，`PlaybackFrame() == EstimatePlaybackFrame()`，补偿随之清零（允许此处回退） |
| HAP 构建（回归） | BUILD SUCCESSFUL；`progress.h` 已编入 `libentry.so` |

### 任务 5.2 实测证据（2026-11-05 18:55）

| 检查项 | 实测结果 |
|---|---|
| `TransportTests` | **PASS**：10 用例 / 84 断言 / 0 失败（新增第 10 个测试套件） |
| 加载与状态 | `Load` 后状态 `kReady`，格式 44100Hz / 1ch / 121275 帧，解码位置与播放位置均为 0 |
| 未加载防护 | `Play` / `SeekToSample` 返回 false；`Pause` / `Stop` / `SetLooping` / `SetLoopRange` 不崩溃 |
| 加载失败 | 文件不存在或路径为 null 时返回 false，状态保持 `kIdle` |
| T13 暂停 | 暂停后 100ms 内 `DecodeFrame()` 不推进；状态为 `kPaused` |
| T13 恢复 | 恢复后解码位置 ≥ 暂停点，且偏差 < 4410 帧（100ms @44.1kHz），未回到 0 |
| T14 未播放 seek | `SeekToSample(50000)` 后解码位置精确为 50000，FIFO 为空 |
| T14 clamp | `-1 → 0`，`total+999999 → total`，`total/2` 精确落点 |
| T14 播放中 seek | 丢弃请求被消费者应用（`FlushCount` 递增、无挂起）；落点落在 `[90000, 90000+2s]`（解码线程立即预填 FIFO） |
| seek 后补偿清零 | 循环中 seek 后 `LoopCount == 0`，`PlaybackFrame() == EstimatePlaybackFrame()` |
| Stop 语义 | 回到 `kReady`、解码位置 0、计数 0、FIFO 空；可再次播放 |
| 幂等性 | 重复 `Play` / `Pause` / `Stop` 不产生额外线程或异常 |
| 重新加载 | 换文件后总帧数、采样率、声道数全部更新，位置归零 |
| HAP 构建（回归） | BUILD SUCCESSFUL；`playback_engine.cpp` 已编入 `libentry.so` |

### 测试过程中发现并修正的缺陷

| 编号 | 缺陷 | 处理 |
|---|---|---|
| B1 | 首版 `PlanWrite` 的测试断言把 `droppedTailBytes` 重复计入守恒式（`1024+2+2=1028 ≠ 1026`） | 确认为**测试断言错误**，实现本身正确。已在 `audio_output_logic.h` 明确 `droppedTailBytes` 是 `silenceBytes` 的子集、不参与求和；断言改为 `bytesFromFifo + silenceBytes == size`，并新增「子集关系」断言防止回归 |
| B2 | 任务 4.2 立体声用例首跑失败：用帧号直接索引 `int16` 参考数组，未乘声道数 | 确认为**测试断言错误**（单声道用例同时通过即可佐证实现无误）。改为 `all.data() + loopStart * 2` 后通过，并加注释说明交错数据的偏移换算 |
| B3 | 任务 4.4 首跑 5 处失败 | 其中 2 处为**断言错误**（改点后立即读 `LoopCount()==0`，但解码线程已继续计数）；1 处为**用例设计错误**（没有消费者，丢弃请求永不应用）；1 处为**实现缺陷**：解码线程可能把「改点前读出的数据」写入消费者已清空的 FIFO，导致旧音频残留。已用 `FlushRequestCount()` 代次比对修复实现，并新增 `testChangeOutsideRangeFlushesBufferedAudio` 覆盖 |
| B4 | 任务 5.2 首跑 1 处失败：播放中 seek 后断言 `DecodeFrame() == 90000`，实测 114576 | 确认为**断言错误**：解码线程 seek 后立即预填 2 秒 FIFO，位置必然前移。改为区间断言 `[90000, 90000+88200]` |
| B5 | 任务 5.2 二跑 `testStopResetsToStart` 失败：`AvailableToRead()` 残留 78096 字节 | 确认为**实现缺陷**：`Stop()` 在消费者仍在运行时直接 `Clear()`，与 `Read()` 竞争写 `readCounter_`，破坏 SPSC 前提。抽出 `FlushBuffer()`：优先 `RequestFlush` 等消费者应用，超时才直接 `Clear()` |
| B6 | 任务 5.2 三跑 `ProgressTests` 出现进度回退（3 处失败） | 确认为**实现缺陷**：`decodedPosition_` 与 `loopCounter_` 不是同一原子快照，循环跳转瞬间读到不一致组合导致读数回退，违反 T10。新增 `lastPlayback_` 单调保护，并在 seek / 改点 / 计数重置时放开 |

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

**说明**：Plan 阶段产物（proposal / design / tasks / delta spec）已完成并通过严格校验；合成 fixture 已生成并通过相位连续性数值检查；HAP 构建、真机安装、启动与 G0 正弦波听测均已实测通过。当前已完成任务 1.1、2.2~2.5、3.1~3.5、4.1~4.5 与 5.1~5.2，下一步为任务 5.3（napi 完整 API 与事件）。任务 6.2 / 6.3 的循环听测需等最小界面（6.1）完成后再执行。

已确认的决策：HarmonyOS SDK **26.0.0**、真实素材由用户提供、包名 `com.cpu.seamlessloopharmony`。

真机实测环境：HUAWEI **MRT-AL10**（序列号 `5KTUT25424016774`），`OpenHarmony-7.0.0.111(SP12C00E111R5P2)`，`const.ohos.apiversion = 26`，与 `compatibleSdkVersion 26.0.0` 一致；签名已配置，signed HAP 安装成功。

仍未完成：`fixtures/real/` 真实素材（任务 1.3）；循环边界听测（T15 / T16 / T05，随 6.2 / 6.3）。
