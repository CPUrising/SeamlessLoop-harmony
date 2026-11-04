# 黑盒观察：原项目无缝循环机制

> 目的：把"要移植的行为"从原项目代码里读出来，作为基线规范与验收场景的事实来源。
> 观察对象：`..\seamless loop music`（WPF 基准）、`..\SeamlessLoopMobile`（Android 参考）。
> 观察方式：静态阅读源码，非运行观察。**未在真机验证，结论以源码为准。**
> 日期：2026-11-04

## 1. 循环跳转的字节层实现（WPF）

来源：[LoopStream.cs](file:///d:/seamless%20loop%20music/seamless%20loop%20music/seamless%20loop%20music/Core/LoopStream.cs)

| 观察项 | 事实 |
|---|---|
| 循环点单位 | 构造参数是采样点，内部乘 `BlockAlign` 转字节（`LoopStartPosition = loopStartSample * _bytesPerSample`） |
| 终点归一化 | `loopEndSample <= 0`、`endPos > Length`、`endPos <= LoopStartPosition` 三种情况一律归到文件末尾 |
| 起点归一化 | `LoopStartPosition < 0` 或 `>= Length` 时归到 `0` |
| 初始位置 | 构造后强制 `Position = 0`，注释明确说明是为了实现 Intro + Loop（首次从头播，循环时才回 LoopStart） |
| 跳转时机 | `Read()` 内 `currentPos >= effectiveEnd` 时立刻 `SafeSetPosition(LoopStartPosition)` |
| 对齐方式 | `SafeSetPosition` 里 `targetPos -= targetPos % _bytesPerSample`，并 clamp 到 `[0, Length]` |
| 读取粒度 | `count` 与 `bytesToRead` 都做 `count -= count % _bytesPerSample`，保证整块 |
| 不足一块的处理 | 若剩余不足一块，循环模式下强制跳转后 `continue`，避免返回 0 字节 |
| 事件 | 每次跳转触发 `OnLoopCompleted` |
| 线程安全 | 整个 `Read` 与 `Position` 读写都在 `_lockObject` 内 |

**移植结论**：跳转必须发生在"读数据"这一侧，且必须块对齐。这一条直接对应规范 `T01`。

## 2. 预缓冲与填充线程（WPF）

来源：[AudioLooper.cs](file:///d:/seamless%20loop%20music/seamless%20loop%20music/seamless%20loop%20music/Core/AudioLooper.cs#L288-L322)、[AudioLooper.Mixing.cs](file:///d:/seamless%20loop%20music/seamless%20loop%20music/seamless%20loop%20music/Core/AudioLooper.Mixing.cs)

| 观察项 | 事实 |
|---|---|
| 缓冲容器 | `BufferedWaveProvider`，`BufferDuration = 3s`，`DiscardOnBufferOverflow = true`，`ReadFully = false` |
| 启动预填 | `Play()` 时先同步读约 1 秒数据塞入缓冲，注释说明"否则 WaveOutEvent 会立即停止" |
| 填充线程 | 独立 `Thread`，`Priority = AboveNormal`，名称 `AudioLooperFiller` |
| 填充水位 | 缓冲低于 **2500 ms** 时补数据，否则 `Sleep(50)` |
| 读取块大小 | 32768 字节 |
| 自然结束 | 非循环模式读到 0 时置 `_isEndingNaturally = true` 并退出线程，等播放器排空后停止 |
| 设备丢失恢复 | `MmException` 时置 `_deviceLost`，下次 `Play()` 重建播放器但保留流位置 |

**移植结论**：必须有独立解码线程 + 约 2~3 秒缓冲 + 水位控制。对应规范 `T17`，以及 `design.md` 决策 2。

## 3. 进度回绕补偿（WPF）

来源：`AudioLooper.CurrentTime`（同文件）

```
fillerPos   = _audioStream.Position          // 解码侧已读位置
bufferedBytes = _bufferedProvider.BufferedBytes
speakerPos  = fillerPos - bufferedBytes      // 反推扬声器位置
if (发生过循环 && speakerPos < loopStart && fillerPos >= loopStart)
    speakerPos += loopLen                     // 回绕补偿
```

| 观察项 | 事实 |
|---|---|
| 补偿前提 | `_hasLoopedSinceSeek == true`，即**至少完成过一次循环跳转** |
| 补偿条件 | `speakerPos < loopStart && fillerPos >= loopStart` |
| 补偿量 | `loopLen = loopEnd - loopStart` |
| 重置时机 | `SeekToSample` 时置 `false` |
| 目的 | 避免 Intro 阶段（0 → LoopStart）误补偿，导致进度条提前跳 |

**移植结论**：这是最容易被漏掉、也最容易出 bug 的一条。对应规范 `T10` / `T11`。

## 4. 运行时修改循环点（WPF）

来源：`AudioLooper.SetLoopEndSample`

- 若新终点小于当前播放位置，立即 `SeekToSample(_loopStartSample)`。
- 原因注释原文：*"如果新的循环终点在当前位置之前，必须立即清除缓冲并 Seek，否则用户会听到已经被截掉的音乐"*。

**移植结论**：对应规范 `T05` / `T06`。

## 5. Android 端的同构验证

来源：[AudioEngine.cpp](file:///d:/seamless%20loop%20music/SeamlessLoopMobile/app/src/main/cpp/AudioEngine.cpp)、[seamless_loop_files.md](file:///d:/seamless%20loop%20music/SeamlessLoopMobile/docs/seamless_loop_files.md)、[Project_Architecture.md](file:///d:/seamless%20loop%20music/SeamlessLoopMobile/docs/Project_Architecture.md)

| 观察项 | 事实 |
|---|---|
| 输出 | Oboe 回调 + 双缓冲区 FIFO 生产消费 |
| 解码 | NDK 解码器 / minimp3，支持精确采样点寻道 |
| 循环 | 在回调线程内按 `LoopStart` / `LoopEnd` 重定向采样指针 |
| 修改循环点 | 指令到 JNI 后立即清空解码缓冲并精准 seek |
| 后台 | `MediaBrowserService` + `MediaSession` |

**差异点**：Android 在回调线程内跳转；WPF 在填充线程内跳转。HarmonyOS 侧选**解码线程内跳转**（见 `design.md` 决策 3），因为 OHAudio 回调不允许做 seek，且解码线程跳转无锁竞争。

## 6. 数据契约（跨端共享，不可擅改）

来源：[DatabaseHelper.cs](file:///d:/seamless%20loop%20music/seamless%20loop%20music/seamless%20loop%20music/Data/DatabaseHelper.cs#L102-L111)、`SeamlessLoopMobile/AGENTS.md`

WPF 端表：`Artists / Albums / Tracks / LoopPoints / UserRatings / Playlists / PlaylistItems / MusicFolders / AppSettings / QueuedTracks`
Android 端表：`Songs / Artists / Albums / LoopPoints / UserRatings / Playlists / PlaylistItems / PlaylistFolders / PlayQueue`

关键字段：

| 表 | 字段 | 类型 | 说明 |
|---|---|---|---|
| `LoopPoints` | `TrackId` | INTEGER PK | 1:1 关联曲目 |
| | `LoopStart` | INTEGER | **采样点数**，默认 0 |
| | `LoopEnd` | INTEGER | **采样点数**，0 表示文件末尾 |
| | `LoopCandidatesJson` | TEXT | 分析候选缓存 |
| `Tracks` | `TotalSamples` | INTEGER | 总采样数，与 `FileName` 组成双指纹 |
| | `DurationMs` | INTEGER | 展示用 |

**移植结论**：HarmonyOS 侧 RDB schema 必须与上述字段语义一致；循环点单位必须是采样点数。对应 `AGENTS.md` 硬约束 2、5。

## 7. 待确认项（未观察到的）

以下内容我**没有**从代码中确认，不作为事实使用：

- OHAudio 在目标 API 版本上的回调周期与最小缓冲要求。
- HarmonyOS 长时任务对音频播放的保活策略细节。
- `loopfinder` 在 HarmonyOS NDK 工具链下的编译结果（未尝试构建）。
- 原项目在真机上的实际听感指标（无测量数据，仅有设计意图）。
