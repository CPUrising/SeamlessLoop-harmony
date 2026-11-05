# 验证债台账（verification-debt）

> 把"写了但没跑过"的验证显式记账，防止把未验证代码当成已通过。
> 状态取值：`未执行` / `执行失败` / `通过` / `阻塞`
> 结算规则：每一行必须由真实执行结果关闭；关闭时把证据链接写进 `docs/verify/`。

| 模块 | 应测项 | 测试文件 | 状态 | 备注 |
|---|---|---|---|---|
| 全项目 | HAP 构建 | — | 通过 | 2026-11-04 实测 `assembleHap` BUILD SUCCESSFUL（unsigned），见 `docs/reference-env.md` |
| 全项目 | 真机安装与启动 | — | 阻塞 | 2026-11-05 真机 MRT-AL10 曾接入，但安装失败：`code:9568320 / no signature file`；当前真机已断开，且签名未配置 |
| `AudioOutput` | OHAudio 回调输出正弦波（G0 冒烟，任务 2.5） | — | 阻塞 | 代码已实现、HAP 构建通过、`libentry.so` 已链接 `libohaudio.so`；仍缺真机听测 |
| `LoopStream` | T01 循环跳转 | `LoopStreamTests` | 未执行 | 代码未实现 |
| `LoopConfig` | T02~T06 循环点校验与运行时变更 | `LoopConfigTests` | 未执行 | 代码未实现 |
| `LoopCounter` | T08 / T09 循环计数与事件 | `LoopCounterTests` | 未执行 | 代码未实现 |
| `Progress` | T10 / T11 进度连续性与回绕补偿 | `ProgressTests` | 未执行 | 代码未实现 |
| `Decoder` | T12 加载与格式上报 | `DecoderTests` | 通过 | 2026-11-05 实测：10 用例 / 50 断言 / 0 失败，含 WAV 单双声道与 MP3 |
| `Decoder` | MP3 解码后实际采样位置与 encoder delay 处理 | `DecoderTests` | 通过 | `drmp3_get_pcm_frame_count` 已扣除 delay/padding，实测总帧数落在 (118000, 124000)；`manifest.json` 仍待回填精确值 |
| `Transport` | T13 暂停恢复 | `TransportTests` | 未执行 | 代码未实现（任务 5.2） |
| `Transport` | T14 按采样点跳转（解码侧 clamp） | `DecoderTests` | 部分通过 | clamp 已实测；FIFO 清空待任务 5.2 |
| `RingBuffer` | T17 欠载静音填充 | `RingBufferTests` | 通过 | 2026-11-05 实测：7 用例 / 103 断言 / 0 失败 |
| 听感验收 | T15 循环边界无静音间隙 | 人工听测 | 阻塞 | 需真机；合成 fixture 已就绪 |
| 听感验收 | T16 循环边界无爆音 | 人工听测 | 阻塞 | 需真机；合成 fixture 相位连续性已数值验证 |
| 听感验收 | T05 播放中改循环点无明显中断 | 人工听测 | 阻塞 | 需真机 |
| 真实素材 | `fixtures/real/` 真实曲目听测 | 人工听测 | 阻塞 | 待用户提供素材与循环点数值 |

## 已关闭

| 日期 | 模块 | 结果 | 证据 |
|---|---|---|---|
| 2026-11-04 | 合成 fixture 生成与相位连续性 | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节 |
| 2026-11-04 | CMake 链接 `libohaudio.so` / `libhilog_ndk.z.so`（任务 2.2） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节「任务 2.2 / 2.3 / 2.4 实测证据」；`llvm-readelf -d libentry.so` 的 NEEDED 列表 |
| 2026-11-04 | napi 桥接 + ArkTS 按钮编译通过（任务 2.4） | 通过 | 同上；`CompileArkTS` Finished，HAP 内含 `libentry.so` |
| 2026-11-05 | `RingBufferTests`（T17 欠载静音） | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节；`artifacts/verify-0001-harmony-loop-kernel-20261105-102022.log` |
| 2026-11-05 | `DecoderTests`（T12 格式上报 + T14 解码侧 clamp） | 通过 | 同上；10 用例 / 50 断言 / 0 失败 |
| 2026-11-05 | 本机单测链路打通（`verify.ps1 --scope unit` 首次真实执行） | 通过 | 同上；`cmake + ctest` 2 套件全通过 |
