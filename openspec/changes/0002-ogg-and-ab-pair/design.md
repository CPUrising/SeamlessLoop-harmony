# Design: OGG Vorbis 解码与 A/B 双文件拼接播放

## 决策 1：OGG 用 stb_vorbis，与桌面端同版本

**选择**：直接复制 `..\seamless loop music\loopfinder\third_party\stb_vorbis.c` 与 `stb_vorbis_impl.c` 到 `entry/src/main/cpp/third_party/`。

**理由**：
- project.md 第 3 节已锁定「复用 `loopfinder` 内的 dr_libs + stb_vorbis，与桌面端一致，避免双解码器行为差异」。
- 同一份源码意味着解码出的采样点与桌面端逐帧一致，循环点数值可直接跨端复用。
- stb_vorbis 是单文件 C，无外部依赖，与现有 `dr_libs_impl.c` 的接入方式完全一致。

**放弃的方案**：
- 用系统 `OH_AVCodec` 解码 OGG：解码结果与桌面端不保证逐帧一致，循环点会漂移；且引入异步解码模型，与现有同步 `ReadFrames` 接口冲突。
- 用 ffmpeg：体积与许可成本都不合适。

## 决策 2：A/B 配对识别放在独立模块，不塞进解码器

**选择**：新增 `ab_pair.{h,cpp}`，只做「给定一个路径，判断它是否属于 A/B 对，并返回配对路径」。

**配对规则**（大小写不敏感）：

| 后缀组 | A 侧 | B 侧 |
|---|---|---|
| 小写 | `_a` | `_b` |
| 大写 | `_A` | `_B` |
| 语义 | `_start` | `_end` |

- 匹配范围：仅同目录、同扩展名。
- 输入可以是 A 侧或 B 侧，统一归一化为「A 侧路径 + B 侧路径」。
- 找不到配对时返回「非 A/B 对」，退化为单文件播放。

**理由**：配对是纯字符串 + 文件存在性判断，与解码、循环都无关。独立成模块才能单测，也避免 `AudioDecoder` 承担文件系统职责。

**放弃的方案**：
- 在 `AudioDecoder::Open()` 内部自动探测配对：会让「打开一个文件」产生隐式副作用，且无法表达「我就是要单独打开 B」。
- 在 ArkTS 侧配对：违反 project.md 第 4 节「UI 不得直接调 Native」的分层，且配对逻辑无法单测。

## 决策 3：虚拟时间轴数据源，循环内核零改动

**选择**：新增 `AudioSource` 抽象，对外暴露与 `AudioDecoder` 相同的 `Format()` / `SeekToFrame()` / `ReadFrames()`，内部持有 1 或 2 个 `AudioDecoder`。

**坐标映射**（A 帧数记为 `NA`）：

| 虚拟轴位置 `p` | 实际读取 |
|---|---|
| `0 <= p < NA` | A 的第 `p` 帧 |
| `NA <= p < NA + NB` | B 的第 `p - NA` 帧 |

- `TotalFrames = NA + NB`。
- `SeekToFrame(p)` 先判断落在哪个文件，再对该文件 seek；跨文件时切换当前解码器。
- `ReadFrames` 读到当前文件末尾时，自动切到下一个文件继续读，**不返回 0**（除非已到虚拟轴末尾）。

**关键约束**：`LoopStart` / `LoopEnd` 由业务层按虚拟轴传入，`AudioSource` 不做任何坐标变换。这样 `DecodeThread` 的循环跳转逻辑（比较 `decodedPosition_` 与 `loopEnd_`、seek 回 `loopStart_`）完全不用改。

**实测依据**：`bgm03_2_a.ogg` + `bgm03_2_b.ogg` 的 DB 记录 `TotalSamples=4377596`、`LoopStart=460798`、`LoopEnd=4377596`，而 A=460798 帧、B=3916798 帧、A+B=4377596。三个数值完全吻合，证明桌面端就是用虚拟轴存储的。

**放弃的方案**：
- 预先把 A+B 解码拼接成一个临时文件：启动延迟大（真实曲目上百 MB），且违背「不落盘中间产物」。
- 在 `DecodeThread` 里加「当前文件索引」状态：会把文件切换逻辑混进循环内核，破坏 0001 已验证的 SPSC 与跳转语义。

## 决策 4：跨文件切换点不做淡入淡出

**选择**：A 末帧直接接 B 首帧，不做任何交叉淡化。

**理由**：
- 原项目（WPF / Android）也没有交叉淡化，project.md 第 2 节把「淡入淡出」列为 out of scope。
- 实测 `bgm03_2` 的 A→B 边界突变 31.5，循环体最大相邻差 5490.5，比值 0.006，素材本身已连续。
- 若素材边界不连续，按 T16 第三条，突变应归因于素材，播放器不得额外引入或放大。

## 决策 5：OGG 的 TotalSamples 以解码器实测为准

**选择**：`AudioDecoder` 对 OGG 报出的 `totalFrames` 用 `stb_vorbis_stream_length_in_samples()`，并在 `Open()` 时不做任何修正。

**已知偏差**：实测 `dohnadohna_08.ogg` 的 DB `TotalSamples=5932199`，ffmpeg 解出 5932200 帧，差 1 帧；`music03_03.ogg` 差 2 帧。这是 OGG 容器 granule position 与解码器输出之间的固有差异，不是缺陷。

**处理**：`LoopEnd` 归一化（T02）已把「超过 Total」的值 clamp 到 Total，因此 ±2 帧的差异不会导致越界。真实素材验收时以解码器实测值为准，并在验证报告中记录差异。

## 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| stb_vorbis 在 OHOS 工具链下的编译告警 | 构建噪音 | 与 `dr_libs_impl.c` 同样处理，必要时局部关闭告警 |
| OGG 解码速度慢于 MP3 | 解码线程可能供数不足 | 0001 的 FIFO 为 2 秒，实测 MP3 无欠载；OGG 需在真机验证欠载计数 |
| A/B 切换瞬间的 seek 开销 | 可能产生欠载 | 切换发生在 `ReadFrames` 内部，不涉及 FIFO 清空；需实测欠载字节数 |
| 虚拟轴 seek 跨文件 | 边界 off-by-one | 单测覆盖 `p = NA-1`、`p = NA`、`p = NA+NB-1` 三个边界 |
