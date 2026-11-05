# 验证债台账（verification-debt）

> 把"写了但没跑过"的验证显式记账，防止把未验证代码当成已通过。
> 状态取值：`未执行` / `执行失败` / `通过` / `阻塞`
> 结算规则：每一行必须由真实执行结果关闭；关闭时把证据链接写进 `docs/verify/`。

| 模块 | 应测项 | 测试文件 | 状态 | 备注 |
|---|---|---|---|---|
| 全项目 | HAP 构建 | — | 通过 | 2026-11-04 实测 `assembleHap` BUILD SUCCESSFUL（unsigned），见 `docs/reference-env.md` |
| 全项目 | 真机安装与启动 | — | 通过 | 2026-11-05 12:18 真机 MRT-AL10 实测：signed HAP `hdc install -r` 成功，`aa start` 启动成功，进程存活 |
| `AudioOutput` | OHAudio 回调输出正弦波（G0 冒烟，任务 2.5） | — | 通过 | 2026-11-05 12:19 真机实测：听到 1 秒 440Hz 正弦波、无爆音；日志 `renderer started: 48000 Hz, 2 ch`，`total underrun bytes=0` |
| `LoopStream` | T01 循环跳转 | `DecodeThreadTests` | 通过 | 2026-11-05 实测：12 用例 / 82 断言 / 0 失败；拼接输出与原始 PCM 逐字节一致 |
| `LoopStream` | T07 关闭循环后整曲播放与结束上报 | `LoopStreamTests` | 通过 | 2026-11-05 实测：5 用例 / 36 断言 / 0 失败；结束回调恰好一次，结束后持续输出静音 |
| `LoopConfig` | T02~T04 循环点校验与归一化 | `LoopConfigTests` | 通过 | 2026-11-05 实测：9 用例 / 74 断言 / 0 失败（T02 的「播放不中断」待运行时链路 4.4 / 5.3） |
| `LoopConfig` | T05 / T06 运行时修改循环点 | `LoopRangeChangeTests` | 通过 | 2026-11-05 实测：5 用例 / 43 断言 / 0 失败；T05 的 200ms 听感条款待 6.3 |
| `LoopCounter` | T08 / T09 循环计数与事件 | `LoopCounterTests` | 通过 | 2026-11-05 实测：7 用例 / 46 断言 / 0 失败；计数单调递增、改点后归零、关闭循环不上报 |
| `Progress` | T10 / T11 进度连续性与回绕补偿 | `ProgressTests` | 通过 | 2026-11-05 实测：8 用例 / 45 断言 / 0 失败；真实循环 3 次以上进度单调不减 |
| `Decoder` | T12 加载与格式上报 | `DecoderTests` | 通过 | 2026-11-05 实测：10 用例 / 50 断言 / 0 失败，含 WAV 单双声道与 MP3 |
| `Decoder` | MP3 解码后实际采样位置与 encoder delay 处理 | `DecoderTests` | 通过 | `drmp3_get_pcm_frame_count` 已扣除 delay/padding，实测总帧数落在 (118000, 124000)；`manifest.json` 仍待回填精确值 |
| `Transport` | T13 暂停恢复 | `TransportTests` | 通过 | 2026-11-05 实测：10 用例 / 84 断言 / 0 失败；恢复偏差 < 100ms |
| `Transport` | T14 按采样点跳转（含 FIFO 清空） | `TransportTests` | 通过 | clamp 与播放中 seek 清空均已实测；`FlushBuffer` 走消费者应用路径 |
| `RingBuffer` | T17 第 1 条：欠载时继续输出静音填充 | `RingBufferTests` | 通过 | 2026-11-05 实测：7 用例 / 103 断言 / 0 失败 |
| `AudioOutput` | 写回调数据规划（帧数换算 / 欠载判定 / 非对齐防护） | `AudioOutputLogicTests` | 通过 | 2026-11-05 实测：9 用例 / 528 断言 / 0 失败 |
| `DecodeThread` | 持续解码、FIFO 满等待、停止语义 | `DecodeThreadTests` | 通过 | 2026-11-05 实测：7 用例 / 34 断言 / 0 失败；解出帧数与 manifest 完全一致 |
| `RingBuffer` | T17 第 2 条：缓冲恢复后自动继续播放 | `RingBufferTests` | 通过 | 2026-11-05 实测：`testRecoversAfterUnderrun` + `testRecoversAfterUnderrunAcrossWrap`（含环绕边界）均通过 |
| `AudioOutput` | T17 第 3 条：欠载事件回调上报 | `underrun` 事件 | 部分通过 | 2026-11-05 已接线：`AttachEngineToOutput()` 调用 `SetUnderrunCallback`，napi 暴露 `on('underrun')`；HAP 构建通过，真机端到端待 6.2 |
| `napi` | 完整 API 与 ThreadSafeFunction 事件 | — | 部分通过 | 2026-11-05 构建通过；真机端到端（事件回调、真实播放）待 6.2 |
| 听感验收 | T15 循环边界无静音间隙 | 人工听测 | 阻塞 | 真机于 19:06 断开；循环链路已实现，待 6.1 界面与 6.2 听测 |
| 听感验收 | T16 循环边界无爆音 | 人工听测 | 阻塞 | 真机已断开；合成 fixture 相位连续性已数值验证；待 6.2 |
| 听感验收 | T05 播放中改循环点无明显中断 | 人工听测 | 阻塞 | 真机已断开；跳转与清空逻辑已通过单测，200ms 听感条款待 6.3 |
| 真实素材 | `fixtures/real/` 真实曲目听测 | 人工听测 | 阻塞 | 待用户提供素材与循环点数值 |

## 已关闭

| 日期 | 模块 | 结果 | 证据 |
|---|---|---|---|
| 2026-11-04 | 合成 fixture 生成与相位连续性 | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节 |
| 2026-11-04 | CMake 链接 `libohaudio.so` / `libhilog_ndk.z.so`（任务 2.2） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节「任务 2.2 / 2.3 / 2.4 实测证据」；`llvm-readelf -d libentry.so` 的 NEEDED 列表 |
| 2026-11-04 | napi 桥接 + ArkTS 按钮编译通过（任务 2.4） | 通过 | 同上；`CompileArkTS` Finished，HAP 内含 `libentry.so` |
| 2026-11-05 | `RingBufferTests`（T17 第 1 条：欠载静音填充） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节；`artifacts/verify-0001-harmony-loop-kernel-20261105-102022.log` |
| 2026-11-05 | `DecoderTests`（T12 格式上报 + T14 解码侧 clamp） | 通过 | 同上；10 用例 / 50 断言 / 0 失败 |
| 2026-11-05 | 本机单测链路打通（`verify.ps1 --scope unit` 首次真实执行） | 通过 | 同上；`cmake + ctest` 2 套件全通过 |
| 2026-11-05 | `AudioOutputLogicTests`（写回调规划） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节；`artifacts/verify-0001-harmony-loop-kernel-20261105-103408.log` |
| 2026-11-05 | `DecodeThreadTests`（解码线程） | 通过 | 同上；7 用例 / 34 断言 / 0 失败 |
| 2026-11-05 | `RingBufferTests`（T17 第 2 条：欠载后恢复继续播放） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-112403.log`；含环绕边界用例 |
| 2026-11-05 | 真机安装与启动（signed HAP） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节「任务 2.5 真机 G0 冒烟」；`hdc` 输出 `install bundle successfully`，`aa start ability successfully` |
| 2026-11-05 | OHAudio 正弦波真机发声（任务 2.5 / G0） | 通过 | 同上；人类听测判定「清晰 1 秒正弦音，无爆音」，日志 `renderer started: 48000 Hz, 2 ch`、`total underrun bytes=0` |
| 2026-11-05 | T01 循环跳转（任务 4.2） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-134253.log`；`testLoopJumpsBackToStart` 等 5 个新用例全通过 |
| 2026-11-05 | T08 / T09 循环计数与事件上报（任务 4.3） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-135348.log`；`LoopCounterTests` 7 用例 / 46 断言全通过 |
| 2026-11-05 | T05 / T06 运行时修改循环点（任务 4.4） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-182218.log`；`LoopRangeChangeTests` 5 用例 / 43 断言全通过；T05 的听感条款仍待 6.3 |
| 2026-11-05 | T07 关闭循环后的整曲播放与结束上报（任务 4.5） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-183703.log`；`LoopStreamTests` 5 用例 / 36 断言全通过 |
| 2026-11-05 | T10 / T11 进度连续性与回绕补偿（任务 5.1） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-184809.log`；`ProgressTests` 8 用例 / 45 断言全通过 |
| 2026-11-05 | T13 / T14 播放控制与按采样点跳转（任务 5.2） | 通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-185518.log`；`TransportTests` 10 用例 / 84 断言全通过 |
| 2026-11-05 | napi 完整 API 与事件接线（任务 5.3） | 部分通过 | `artifacts/verify-0001-harmony-loop-kernel-20261105-190816.log`；HAP 构建通过，真机端到端待 6.2 |
