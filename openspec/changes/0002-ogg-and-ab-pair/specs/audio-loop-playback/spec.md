# Delta Spec: audio-loop-playback

## MODIFIED Requirements

### Requirement: Sample-Accurate Loop Jump

系统 SHALL 以采样点数（int64）作为循环点的唯一单位，并在音频数据流层面完成循环跳转，不得依赖整曲重播。循环点坐标的基准是**数据源的虚拟时间轴**：单文件时即文件内采样点，A/B 双文件时为两文件拼接后的采样点。

#### Scenario: T01 循环终点跳回循环起点

- **GIVEN** 已加载音频，采样率为 `SR`，块对齐字节数为 `BA`，循环区间为 `[LoopStart, LoopEnd)`
- **WHEN** 解码位置推进到 `LoopEnd`
- **THEN** 系统 MUST 将解码位置置为 `LoopStart`，并继续输出后续音频
- **AND** 跳转后的字节位置 MUST 按 `BA` 取整，不得出现非整块偏移
- **AND** 系统 MUST NOT 重新打开文件、重建播放器或重启音频流

#### Scenario: T02 循环终点非法值归一化

- **GIVEN** 已加载音频，虚拟时间轴总采样数为 `Total`
- **WHEN** 业务层设置 `LoopEnd` 为 `0`、负数，或大于 `Total`
- **THEN** 系统 MUST 将生效的循环终点归一化为 `Total`
- **AND** MUST NOT 因此中断播放

#### Scenario: T03 循环起点非法值拒绝

- **GIVEN** 已加载音频，虚拟时间轴总采样数为 `Total`
- **WHEN** 业务层设置 `LoopStart` 为负数、或大于等于 `Total`
- **THEN** 系统 MUST 拒绝该设置并保持原循环起点不变
- **AND** MUST 通过错误回调向业务层报告失败原因

#### Scenario: T04 循环区间退化保护

- **GIVEN** 已加载音频
- **WHEN** 业务层设置的 `LoopEnd` 小于等于 `LoopStart`
- **THEN** 系统 MUST 拒绝该设置并保持原循环区间不变
- **AND** MUST NOT 进入死循环或无音频输出状态
