# 追踪矩阵（traceability）

> 维护规则：新增/改名场景时，必须同步规范标题、本表、测试方法名。
> `状态=通过` 必须能链接到 `docs/verify/<change>.md` 的 PASS 行。

## audio-loop-playback

| ID | 场景 | 规范位置 | 自动化测试 | 状态 |
|---|---|---|---|---|
| T01 | 循环终点跳回循环起点 | `specs/audio-loop-playback` → `T01` | `LoopStreamTests.testLoopJumpsBackToStart` | 未实现 |
| T02 | 循环终点非法值归一化 | 同上 → `T02` | `LoopConfigTests.testInvalidEndNormalized` | 未实现 |
| T03 | 循环起点非法值拒绝 | 同上 → `T03` | `LoopConfigTests.testInvalidStartRejected` | 未实现 |
| T04 | 循环区间退化保护 | 同上 → `T04` | `LoopConfigTests.testDegenerateRangeRejected` | 未实现 |
| T05 | 播放中修改循环点立即生效 | 同上 → `T05` | `LoopConfigTests.testChangeOutsideRangeSeeks` + 真机听测 | 未实现 |
| T06 | 播放中修改循环点不影响当前位置 | 同上 → `T06` | `LoopConfigTests.testChangeInsideRangeKeepsPosition` | 未实现 |
| T07 | 关闭循环后的整曲播放 | 同上 → `T07` | `LoopStreamTests.testLoopDisabledStopsAtEnd` | 未实现 |
| T08 | 循环次数单调递增 | 同上 → `T08` | `LoopCounterTests.testCounterIncrementsAndResets` | 未实现 |
| T09 | 关闭循环时不上报循环事件 | 同上 → `T09` | `LoopCounterTests.testNoEventWhenDisabled` | 未实现 |
| T10 | 进度在循环边界保持连续 | 同上 → `T10` | `ProgressTests.testProgressContinuousAcrossLoop` | 未实现 |
| T11 | 进度在 Intro 阶段不误补偿 | 同上 → `T11` | `ProgressTests.testNoCompensationDuringIntro` | 未实现 |
| T12 | 加载后可播放 | 同上 → `T12` | `DecoderTests.testLoadReportsFormat` | 通过 |
| T13 | 暂停后恢复不丢位置 | 同上 → `T13` | `TransportTests.testPauseResumeKeepsPosition` | 未实现 |
| T14 | 按采样点跳转 | 同上 → `T14` | `TransportTests.testSeekClampsAndAligns` | 部分通过（解码侧 clamp 已通过；FIFO 清空待 5.2） |
| T15 | 循环边界无静音间隙 | 同上 → `T15` | 离线波形分析 + 真机听测 | 未实现 |
| T16 | 循环边界无爆音 | 同上 → `T16` | 离线波形分析 + 真机听测 | 未实现 |
| T17 | 缓冲欠载可恢复 | 同上 → `T17` | `RingBufferTests.testUnderrunWritesSilence` | 部分通过（静音填充已通过；欠载事件上报待 3.5） |

## 状态取值

`未实现` / `已实现待执行` / `部分通过` / `通过` / `失败`

## 变更记录

| 日期 | change | 变更内容 |
|---|---|---|
| 2026-11-04 | `0001-harmony-loop-kernel` | 建立 T01~T17 全部 17 条场景 |
| 2026-11-05 | `0001-harmony-loop-kernel` | T12 → 通过（`DecoderTests`）；T14 / T17 → 部分通过（解码侧 clamp、欠载静音已实测，播放侧待后续任务） |
