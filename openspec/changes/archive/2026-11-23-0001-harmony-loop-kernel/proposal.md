# Proposal: 鸿蒙播放内核验证（采样级无缝循环）

## Why

原项目（WPF）与 Android 端的无缝循环能力，依赖三件事叠加才成立：字节级跳转、块对齐、预缓冲 + 独立填充线程。这套机制在 HarmonyOS 上没有现成等价物：

- AVPlayer 的 `loop` 属性只做整曲循环，无法指定 `LoopStart` / `LoopEnd`。
- 监听 `completed` 事件后重新 `play()` 必然产生可听见的断点。
- 官方推荐的 PCM 通道（OHAudio / AudioRenderer）只提供裸的写回调，循环跳转、缓冲、进度补偿全部要自己实现。

也就是说，**移植的全部技术风险集中在这一个点上**。如果它不成立，上层 UI、数据库、歌单、统计同步写得再完整也没有产品价值。因此第一阶段只做这一件事：用最小可运行工程证明"在鸿蒙上能做出听感无缝的采样级循环"。

## What Changes

- 新建 HarmonyOS Stage 工程骨架（单 module + Native CMake 子工程）。
- 引入 Native 音频链路：dr_libs 解码 → 解码线程 → 环形 FIFO → OHAudio 写回调。
- 实现循环跳转：按采样点跳转、`BlockAlign` 对齐、循环计数上报。
- 提供薄 napi 桥接，暴露 `load / play / pause / stop / setLoopPoints / getProgress`。
- 提供一个最小 ArkTS 页面：播放/暂停、当前进度、循环次数。**不做列表、不做数据库、不做歌单。**
- 建立合成测试音频 fixtures 与验证脚本，产出第一份验证证据。

## Capabilities

### New Capabilities

- `audio-loop-playback`: 采样级无缝循环播放，包含循环点配置、跳转对齐、循环次数上报与播放进度回绕补偿。

### Modified Capabilities

（无。这是首个变更，基线规范由本变更归档时产生。）

## Impact

- 新增代码：`entry/src/main/ets/`（ArkTS 页面与桥接）、`entry/src/main/cpp/`（音频引擎与解码）。
- 复用资产：`..\seamless loop music\loopfinder\third_party`（dr_libs、stb_vorbis）。
- 新增 `fixtures/`（合成音频）与 `scripts/verify.ps1`。
- 新增文档：`docs/observation/`、`docs/traceability.md`、`docs/reference-env.md`。
- 影响测试：全部为新增，无既有用例回归风险。

### Out of scope（本变更明确不做）

- 音乐库扫描、数据库、歌单、搜索。
- A/B 双文件拼接。
- `loopfinder` 循环点自动分析（另开 change）。
- 后台播放、AVSession、通知栏播控。
- 投播 / 分布式流转。
- 播放统计与 GitHub 同步。
- 任何 UI 美化、主题、多语言。
- 音频格式的完整覆盖（本变更只验证 MP3 与 WAV 各一条）。
