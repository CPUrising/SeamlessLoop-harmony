# 追踪矩阵（traceability）

> 维护规则：新增/改名场景时，必须同步规范标题、本表、测试方法名。
> `状态=通过` 必须能链接到 `docs/verify/<change>.md` 的 PASS 行。

## audio-loop-playback

| ID | 场景 | 规范位置 | 自动化测试 | 状态 |
|---|---|---|---|---|
| T01 | 循环终点跳回循环起点 | `specs/audio-loop-playback` → `T01` | `DecodeThreadTests.testLoopJumpsBackToStart` | 通过 |
| T02 | 循环终点非法值归一化 | 同上 → `T02` | `LoopConfigTests.testInvalidEndNormalized` | 部分通过（归一化与不拒绝已通过；「不中断播放」待 4.2 集成验证） |
| T03 | 循环起点非法值拒绝 | 同上 → `T03` | `LoopConfigTests.testInvalidStartRejected` | 通过 |
| T04 | 循环区间退化保护 | 同上 → `T04` | `LoopConfigTests.testDegenerateRangeRejected` | 通过 |
| T05 | 播放中修改循环点立即生效 | 同上 → `T05` | `LoopRangeChangeTests.testChangeOutsideRangeSeeks` + `testChangeOutsideRangeFlushesBufferedAudio` + 真机听测 | 部分通过（跳转落点与 FIFO 清空已通过；200ms 无感中断待 6.3） |
| T06 | 播放中修改循环点不影响当前位置 | 同上 → `T06` | `LoopRangeChangeTests.testChangeInsideRangeKeepsPosition` | 通过 |
| T07 | 关闭循环后的整曲播放 | 同上 → `T07` | `LoopStreamTests.testLoopDisabledStopsAtEnd` | 通过 |
| T08 | 循环次数单调递增 | 同上 → `T08` | `LoopCounterTests.testCounterIncrementsAndResets` | 通过 |
| T09 | 关闭循环时不上报循环事件 | 同上 → `T09` | `LoopCounterTests.testNoEventWhenDisabled` | 通过 |
| T10 | 进度在循环边界保持连续 | 同上 → `T10` | `ProgressTests.testProgressMonotonicAcrossRealLoops` | 通过 |
| T11 | 进度在 Intro 阶段不误补偿 | 同上 → `T11` | `ProgressTests.testNoCompensationDuringIntro` | 通过 |
| T12 | 加载后可播放 | 同上 → `T12` | `DecoderTests.testLoadReportsFormat` | 通过 |
| T13 | 暂停后恢复不丢位置 | 同上 → `T13` | `TransportTests.testPauseResumeKeepsPosition` | 通过 |
| T14 | 按采样点跳转 | 同上 → `T14` | `TransportTests.testSeekClampsAndAligns` + `testSeekWhilePlayingFlushesBuffer` | 通过 |
| T15 | 循环边界无静音间隙 | 同上 → `T15` | 离线波形分析 + 真机听测 | 已实现待执行（6.1 界面已就绪；6.2 听测阻塞于真机断开） |
| T16 | 循环边界无爆音 | 同上 → `T16` | 离线波形分析 + 真机听测 | 已实现待执行（相位连续性已数值验证；6.2 听测阻塞于真机断开） |
| T17 | 缓冲欠载可恢复 | 同上 → `T17` | `RingBufferTests` + `AudioOutputLogicTests` + `underrun` 事件 | 部分通过（第 1、2 条已通过；第 3 条事件已接线，真机端到端待 6.2） |

## 状态取值

`未实现` / `已实现待执行` / `部分通过` / `通过` / `失败`

## 变更记录

| 日期 | change | 变更内容 |
|---|---|---|
| 2026-11-04 | `0001-harmony-loop-kernel` | 建立 T01~T17 全部 17 条场景 |
| 2026-11-05 | `0001-harmony-loop-kernel` | T12 → 通过（`DecoderTests`）；T14 / T17 → 部分通过（解码侧 clamp、欠载静音已实测，播放侧待后续任务） |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 3.4 / 3.5 完成：新增 `DecodeThreadTests`（34 断言）与 `AudioOutputLogicTests`（528 断言）；T17 仍为部分通过（真实欠载需真机） |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 4.1 完成：新增 `LoopConfigTests`（9 用例 / 74 断言），T03 / T04 → 通过，T02 → 部分通过（播放不中断待 4.2） |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 4.2 完成：`DecodeThreadTests` 扩至 12 用例 / 82 断言，T01 → 通过（拼接结果与原始 PCM 逐字节一致） |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 4.3 完成：新增 `LoopCounterTests`（7 用例 / 46 断言），T08 / T09 → 通过 |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 4.4 完成：新增 `LoopRangeChangeTests`（5 用例 / 43 断言），T06 → 通过，T05 → 部分通过（200ms 听感条款待 6.3） |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 4.5 完成：新增 `LoopStreamTests`（5 用例 / 36 断言），T07 → 通过 |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 5.1 完成：新增 `progress.h` 与 `ProgressTests`（8 用例 / 45 断言），T10 / T11 → 通过 |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 5.2 完成：新增 `PlaybackEngine` 与 `TransportTests`（10 用例 / 84 断言），T13 / T14 → 通过 |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 5.3 完成：napi 暴露完整 API 与 `loop` / `end` / `underrun` 事件（ThreadSafeFunction）；T17 第 3 条已接线，真机端到端待 6.2 |
| 2026-11-05 | `0001-harmony-loop-kernel` | 任务 6.1 完成：最小 ArkTS 页面（fixture 加载、播放/暂停、进度与循环次数）；T15 / T16 由「未实现」改为「已实现待执行」，听测仍阻塞于真机断开 |
