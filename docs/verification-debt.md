# 验证债台账（verification-debt）

> 把"写了但没跑过"的验证显式记账，防止把未验证代码当成已通过。
> 状态取值：`未执行` / `执行失败` / `通过` / `阻塞`
> 结算规则：每一行必须由真实执行结果关闭；关闭时把证据链接写进 `docs/verify/`。

| 模块 | 应测项 | 测试文件 | 状态 | 备注 |
|---|---|---|---|---|
| 全项目 | HAP 构建 | — | 通过 | 2026-11-04 实测 `assembleHap` BUILD SUCCESSFUL（unsigned），见 `docs/reference-env.md` |
| 全项目 | 真机安装与启动 | — | 阻塞 | `hdc list targets` 为空，无 HarmonyOS 真机连接 |
| `AudioOutput` | OHAudio 回调输出正弦波（G0 冒烟） | — | 阻塞 | HAP 构建已通过；仍缺真机 |
| `LoopStream` | T01 循环跳转 | `LoopStreamTests` | 未执行 | 代码未实现 |
| `LoopConfig` | T02~T06 循环点校验与运行时变更 | `LoopConfigTests` | 未执行 | 代码未实现 |
| `LoopCounter` | T08 / T09 循环计数与事件 | `LoopCounterTests` | 未执行 | 代码未实现 |
| `Progress` | T10 / T11 进度连续性与回绕补偿 | `ProgressTests` | 未执行 | 代码未实现 |
| `Decoder` | T12 加载与格式上报 | `DecoderTests` | 未执行 | 代码未实现 |
| `Decoder` | MP3 解码后实际采样位置与 encoder delay 处理 | `DecoderTests` | 未执行 | manifest 中 MP3 条目待实测回填（D6） |
| `Transport` | T13 / T14 暂停恢复与跳转 | `TransportTests` | 未执行 | 代码未实现 |
| `RingBuffer` | T17 欠载静音填充 | `RingBufferTests` | 未执行 | 代码未实现 |
| 听感验收 | T15 循环边界无静音间隙 | 人工听测 | 阻塞 | 需真机；合成 fixture 已就绪 |
| 听感验收 | T16 循环边界无爆音 | 人工听测 | 阻塞 | 需真机；合成 fixture 相位连续性已数值验证 |
| 听感验收 | T05 播放中改循环点无明显中断 | 人工听测 | 阻塞 | 需真机 |
| 真实素材 | `fixtures/real/` 真实曲目听测 | 人工听测 | 阻塞 | 待用户提供素材与循环点数值 |

## 已关闭

| 日期 | 模块 | 结果 | 证据 |
|---|---|---|---|
| 2026-11-04 | 合成 fixture 生成与相位连续性 | 通过 | [verify/0001](verify/0001-harmony-loop-kernel.md) 第 3 节 |
