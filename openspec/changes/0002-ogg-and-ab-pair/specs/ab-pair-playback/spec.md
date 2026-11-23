# Delta Spec: ab-pair-playback

## ADDED Requirements

### Requirement: A/B Pair Detection

系统 SHALL 能识别同一目录下成对的 A/B 音频文件，并把它们视为一首曲目。

#### Scenario: P01 三组后缀均可配对

- **GIVEN** 同一目录下存在 `xxx_a.ogg` 与 `xxx_b.ogg`
- **WHEN** 业务层以任一文件路径请求加载
- **THEN** 系统 MUST 识别出这是一对 A/B 文件
- **AND** MUST 归一化为「A 侧路径 + B 侧路径」
- **AND** 后缀 `_a`/`_b`、`_A`/`_B`、`_start`/`_end` MUST 均被支持，且大小写不敏感

#### Scenario: P02 找不到配对时退化为单文件

- **GIVEN** 同一目录下只有 `xxx_a.ogg`，没有对应的 B 侧文件
- **WHEN** 业务层请求加载
- **THEN** 系统 MUST 按单文件播放处理
- **AND** MUST NOT 报错或拒绝加载

#### Scenario: P03 非 A/B 文件不受影响

- **GIVEN** 文件名不含任何 A/B 后缀
- **WHEN** 业务层请求加载
- **THEN** 系统 MUST 按单文件播放处理

### Requirement: Virtual Timeline Playback

系统 SHALL 把 A/B 两个文件串成一条虚拟时间轴，循环点坐标在该轴上表示。

#### Scenario: P04 虚拟轴总长等于两文件之和

- **GIVEN** A 有 `NA` 帧、B 有 `NB` 帧
- **WHEN** 加载完成
- **THEN** 系统 MUST 上报总采样数为 `NA + NB`
- **AND** 采样率与声道数 MUST 取 A 的值

#### Scenario: P05 A 播完无缝接 B

- **GIVEN** 正在播放，当前位置为 `NA - 1`
- **WHEN** 继续读取
- **THEN** 系统 MUST 返回 A 的最后一帧，随后返回 B 的第 0 帧
- **AND** MUST NOT 返回 0（除非已到虚拟轴末尾）
- **AND** MUST NOT 重新打开文件或重建音频流

#### Scenario: P06 循环点在虚拟轴上生效

- **GIVEN** A/B 已加载，`LoopStart = NA`、`LoopEnd = NA + NB`
- **WHEN** 播放推进到 `LoopEnd`
- **THEN** 系统 MUST 跳回 `LoopStart`（即 B 的第 0 帧）
- **AND** 循环计数 MUST 加一
- **AND** 跳转 MUST 按块对齐

#### Scenario: P07 虚拟轴 seek 跨文件

- **GIVEN** A/B 已加载
- **WHEN** 业务层请求跳转到虚拟轴位置 `p`
- **THEN** 若 `p < NA`，系统 MUST 在 A 内 seek 到 `p`
- **AND** 若 `p >= NA`，系统 MUST 在 B 内 seek 到 `p - NA`
- **AND** 后续读取 MUST 从该位置连续输出
