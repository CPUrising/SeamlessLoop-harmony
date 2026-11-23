# Proposal: OGG Vorbis 解码与 A/B 双文件拼接播放

## Why

0001 已证明采样级无缝循环在 HarmonyOS 上成立（T01~T17 全部通过，真机听测完成）。但真实素材验收暴露两个缺口：

1. **OGG 打不开**。`AudioDecoder::Open()` 只按扩展名分派 `.wav` / `.mp3`，而用户音乐库中大量曲目是 OGG Vorbis（`dohnadohna_08.ogg`、`music03_03.ogg`、`bgm03_2_a/b.ogg` 等）。桌面端与 Android 端都支持 OGG，鸿蒙端不支持即为功能退化。
2. **A/B 双文件拼接未实现**。原项目用 `xxx_A` + `xxx_B` 两个文件表示「前奏 + 循环体」，播放时 A 播完无缝接 B，之后循环 B。这是原项目的既有能力，0001 的 proposal 明确把它列为 out of scope。

两者都是「真实曲库可用」的前置条件，且都只影响解码与数据源层，不改循环内核，因此合并为一个 change。

## What Changes

- 引入 `stb_vorbis`（与桌面端同版本，来自 `loopfinder/third_party`），`AudioDecoder` 支持 `.ogg`。
- 新增 A/B 配对识别：同目录下按 `_a`/`_b`、`_A`/`_B`、`_start`/`_end` 后缀配对，大小写不敏感。
- 新增「虚拟时间轴」数据源：把 A、B 两个解码器串成一个逻辑流，`LoopStart` / `LoopEnd` / `TotalSamples` 全部在拼接轴上表示。
- 循环内核（`DecodeThread` / `RingBuffer` / `PlaybackEngine`）**不改语义**，只把数据源从「单文件解码器」换成「可跨文件的数据源接口」。
- 最小界面增加 A/B 曲目加载入口，用于真机听测。

## Capabilities

### New Capabilities

- `audio-format-support`: 音频格式解码能力，本变更新增 OGG Vorbis。
- `ab-pair-playback`: A/B 双文件拼接播放，含配对识别与虚拟时间轴。

### Modified Capabilities

- `audio-loop-playback`: 循环点的坐标基准从「单文件采样点」扩展为「数据源虚拟时间轴采样点」。单文件场景下两者等价，因此既有 T01~T17 场景语义不变。

## Impact

- 新增代码：`entry/src/main/cpp/third_party/stb_vorbis.c`、`stb_vorbis_impl.c`；`audio_decoder` 增加 OGG 分支；新增 `ab_pair.{h,cpp}`（配对识别）与 `audio_source.{h,cpp}`（虚拟时间轴数据源）。
- 修改代码：`decode_thread` 的数据源类型由 `AudioDecoder*` 改为 `AudioSource*`；`playback_engine` 相应调整。
- 新增测试：`audio_decoder_test` 增加 OGG 用例；新增 `ab_pair_test`、`audio_source_test`。
- 新增 fixtures：`fixtures/synthetic/` 增加一对合成 A/B（内容不同、边界相位连续），用于自动化断言。
- 真实素材：`fixtures/real/bgm03_2_a.ogg` + `bgm03_2_b.ogg`（已核对：A=460798 帧、B=3916798 帧、DB `TotalSamples=4377596=A+B`、`LoopStart=460798=A 帧数`、`LoopEnd=4377596=A+B`）。

### Out of scope（本变更明确不做）

- FLAC 解码（`dr_flac.h` 已在 third_party，但本变更不接）。
- 循环点自动分析（`loopfinder`）。
- 音乐库扫描、数据库读写、歌单。
- 后台播放、AVSession。
- A/B 之外的更多分段（如 A/B/C）。
- 跨文件淡入淡出或交叉淡化（原项目也没有，属音效处理，project.md 明确 out of scope）。
