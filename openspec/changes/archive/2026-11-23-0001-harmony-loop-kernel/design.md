# Design: 鸿蒙播放内核验证（采样级无缝循环）

## Context

原项目两个可用实现给出了同一套机制：

- **WPF 端**：`LoopStream.Read()` 在字节层面监控位置，到达 `LoopEndPosition` 时 `SafeSetPosition(LoopStartPosition)`（块对齐后跳转）；`BufferedWaveProvider` 维持 3 秒缓冲；独立填充线程持续供数；`CurrentTime` 用「写入位置 − 缓冲字节数」反推扬声器位置，并在循环边界做回绕补偿。
- **Android 端**：Oboe 回调 + 自建解码 FIFO，在回调线程内按 `LoopStart` / `LoopEnd` 重定向采样指针。

两端结论一致：**无缝 = 字节级跳转 + 块对齐 + 预缓冲 + 独立解码线程 + 进度补偿**，缺一不可。

HarmonyOS 侧的现实约束：

- AVPlayer 只支持整曲 `loop`，无法指定区间；`completed` 事件重播必然断点。
- OHAudio 是 C API，写回调里必须**立刻**返回足够数据；不允许在回调内做解码或文件 IO。
- ArkTS 的 `AudioRenderer` 回调运行在 JS 线程，容易受 UI 负载影响，长期不利于稳定供数。
- 现有 C++ 资产（dr_libs、stb_vorbis、loopfinder）可以直接用，但要换 HarmonyOS NDK 工具链重编。

## Goals / Non-Goals

**Goals:**

- 证明在 HarmonyOS 真机上能做到听感无缝的采样级循环。
- 把 C++ 音频链路（解码 + FIFO + 循环跳转）建起来，为后续 `loopfinder` 移植和 A/B 拼接留好接口。
- 产出一份可复现的验证证据，而不是"应该没问题"。

**Non-Goals:**

- 不追求极致低延迟（目标 100~200 ms 缓冲即可，稳定优先）。
- 不覆盖全部音频格式，只验证 MP3 与 WAV。
- 不做后台播放、锁屏播控、投播。
- 不设计最终 UI。

## Decisions

### 1. 音频输出走 OHAudio（NDK），不走 AVPlayer

- **选择**：`OH_AudioStreamBuilder` 构建 renderer，使用 `OH_AudioRenderer_Start` + 写回调。
- **理由**：
  - 循环跳转需要直接控制 PCM 数据指针，AVPlayer 的封装层不暴露此能力。
  - 可直接复用 dr_libs 解码器，避免"AVPlayer 解码 + 自己跳转"这种不可能的混合。
  - 回调在系统音频线程，不受 ArkTS 主线程卡顿影响。
- **放弃的方案**：
  - AVPlayer + `loop`：无法指定区间，产品功能不成立。
  - AVPlayer + `completed` 重播：存在 50~200 ms 可听断点，直接否掉。
  - ArkTS `AudioRenderer`：可做原型，但解码与循环逻辑要在 JS 层完成，性能与稳定性都劣于 C++，且无法复用 loopfinder。

### 2. 数据流：解码线程 → 环形 FIFO → 写回调

```
[解码线程 Native]                     [OHAudio 回调线程]
  dr_libs 解码                          OH_AudioRenderer_WriteData
      ↓                                        ↑
  按块对齐读取                          从 FIFO 取数据
      ↓                                        ↑
  到达 LoopEnd → 跳回 LoopStart          FIFO 空 → 写静音 + 上报欠载
      ↓
  写入环形 FIFO
```

- **选择**：单生产者（解码线程）单消费者（回调线程）的环形 FIFO，容量按 2 秒音频配置。
- **理由**：回调线程内绝不做文件 IO 或解码；FIFO 提供解耦与抖动吸收。
- **放弃的方案**：在回调内直接解码（会被系统判定为回调超时，产生 glitch）。

### 3. 循环跳转在解码侧完成，不在回调侧完成

- **选择**：解码线程读到 `LoopEnd` 时，把文件位置 seek 回 `LoopStart`，并递增循环计数。
- **理由**：
  - 跳转只发生在一个线程，无并发竞争，不需要在实时回调里加锁。
  - FIFO 里永远是"已按循环拼接好的连续数据"，回调侧完全不知道有循环存在。
- **放弃的方案**：在回调侧判断位置并跳转——需要跨线程共享可变位置，锁竞争会引入抖动。

### 4. 循环点单位用采样点数，不用字节

- **选择**：对外 API 与持久化一律用 int64 采样点；内部转字节时统一乘 `BlockAlign`。
- **理由**：与桌面端 `LoopPoints` 表、Android 端 `LoopPoints` 表完全一致，跨端同步不需要换算；字节单位在不同格式（MP3/WAV/OGG）间不一致，容易出错。
- **放弃的方案**：毫秒——受采样率影响，重采样后会漂移；百分比——精度不足。

### 5. 块对齐用 BlockAlign，不用采样数对齐

- **选择**：所有 seek / 跳转的字节位置按 `BlockAlign` 取整（`pos -= pos % blockAlign`）。
- **理由**：这是原项目 `LoopStream.SafeSetPosition` 的做法。BlockAlign = 声道数 × 每采样字节数，按它对齐才能保证不撕裂声道、不产生爆音。
- **放弃的方案**：按"帧"对齐——本质等价，但需要在多处换算，容易漏。

### 6. 进度上报采用「已解码位置 − FIFO 剩余字节」，并做回绕补偿

- **选择**：与 WPF 端 `CurrentTime` 同构。
- **理由**：OHAudio 不提供精确的扬声器播放位置。用「解码位置减去未消耗缓冲」可以得到接近实际的输出位置。
- **关键细节**：必须维护 `hasLoopedSinceSeek` 标志。只有在发生过至少一次循环跳转后，才在"扬声器位置 < LoopStart 且 解码位置 ≥ LoopStart"时给位置加一个循环长度。否则 Intro 阶段（0 → LoopStart）会误补偿，进度条会提前跳。
- **放弃的方案**：直接返回解码位置——会领先实际听感约一个缓冲时长，进度条与声音不同步。

### 7. napi 桥接层保持极薄

- **选择**：只暴露 `load / play / pause / stop / seekToSample / setLoopPoints / setLooping / getProgress / getState`，事件通过 `ThreadSafeFunction` 回传。
- **理由**：桥接层是 bug 高发区（线程、生命周期、内存）。逻辑全部放 C++ 引擎内部，桥接层只做类型转换。
- **放弃的方案**：在 napi 层做状态机——会把业务逻辑分散到两处。

### 8. 工程结构：单 module + CMake 子工程

- **选择**：`entry` 单模块，C++ 代码放 `entry/src/main/cpp/`，CMake 链接 `libohaudio.so`。
- **理由**：第一阶段只需验证内核，多模块（HAR/HSP）会引入额外构建复杂度。
- **放弃的方案**：一开始就拆 `feature-audio` HAR——过早抽象。

## Risks / Trade-offs

| 风险 | 影响 | 应对 |
|---|---|---|
| **无 DevEco Studio 与真机** | 无法构建、无法验证，全部任务阻塞 | 立刻在 `docs/verification-debt.md` 登记为阻塞；先完成可静态校验的部分（规范、C++ 代码、CMake、fixtures） |
| OHAudio 回调欠载 | 出现可听 glitch | FIFO 容量设 2 秒；解码线程优先级提升；欠载时写静音而非中断 |
| dr_libs 对某些 MP3 变体解码异常 | 加载失败或时长错误 | 只承诺 MP3/WAV；失败时明确报错而非静默；后续 change 再评估格式扩展 |
| 循环点位于非零采样值 | 跳转处理论上有波形不连续 | 这属于"循环点选得不好"，不是播放器缺陷；验收用已知良好循环点的 fixture；同时验证 `loopfinder` 会给出更优循环点（后续 change） |
| 进度补偿逻辑出错 | 进度条跳变，比断音更容易被用户发现 | T10 / T11 两条场景专门覆盖；必须有单元测试而非只靠听 |
| HarmonyOS API 版本差异 | OHAudio 接口签名变动 | `docs/reference-env.md` 冻结 API 版本；不使用低于 API 12 的接口 |

## Migration Plan

1. 建立工程骨架与 CMake，链接 OHAudio，跑通"输出 1 秒正弦波"（G0 冒烟）。
2. 引入 dr_libs，实现单文件加载与解码（不循环）。
3. 加入环形 FIFO 与解码线程，实现连续播放。
4. 加入循环跳转与块对齐。
5. 加入循环计数、进度补偿与事件回调。
6. napi 桥接 + 最小 ArkTS 页面。
7. 生成 fixtures，跑验证脚本，人工听测，回填证据。

## Open Questions

以下问题已由人类决策，记录于此：

1. ~~DevEco Studio 与真机何时就绪~~ → **正在准备中**。未就绪前任务 2 之后全部阻塞，已登记。
2. ~~目标 API 版本~~ → **HarmonyOS SDK 26.0.0**（SemVer 格式，不再带 `(N)`）。已冻结至 `docs/reference-env.md`。
3. **音乐文件的获取方式**：本变更先用"应用沙箱内固定路径"绕过（由用户把文件放入沙箱目录）。**后续必须定方案**：`AudioViewPicker` + 持久化 URI 授权（需 `ohos.permission.FILE_ACCESS_PERSIST`，冷启动需 `activatePermission` 重新激活），或用户手动导入。
4. ~~测试音频从哪来~~ → **用户提供真实音乐文件**到指定目录。本变更仍需合成 fixture 用于可判定的自动化断言（见 `fixtures/README.md`）。

**仍待定**：

- `compatibleSdkVersion` 下限（需与真机系统版本对齐后冻结）。
- 目标真机型号与系统版本。
