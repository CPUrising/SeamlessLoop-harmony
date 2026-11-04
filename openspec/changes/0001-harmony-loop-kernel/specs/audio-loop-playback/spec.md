# audio-loop-playback Delta

## Purpose

在 HarmonyOS 上提供采样级无缝循环播放能力：以采样点为单位指定循环区间，播放到循环终点时在音频数据流层面跳回循环起点，使听感连续、无断点、无爆音，并向业务层上报循环次数与不跳变的播放进度。

## ADDED Requirements

### Requirement: Sample-Accurate Loop Jump

系统 SHALL 以采样点数（int64）作为循环点的唯一单位，并在音频数据流层面完成循环跳转，不得依赖整曲重播。

#### Scenario: T01 循环终点跳回循环起点

- **GIVEN** 已加载音频，采样率为 `SR`，块对齐字节数为 `BA`，循环区间为 `[LoopStart, LoopEnd)`
- **WHEN** 解码位置推进到 `LoopEnd`
- **THEN** 系统 MUST 将解码位置置为 `LoopStart`，并继续输出后续音频
- **AND** 跳转后的字节位置 MUST 按 `BA` 取整，不得出现非整块偏移
- **AND** 系统 MUST NOT 重新打开文件、重建播放器或重启音频流

#### Scenario: T02 循环终点非法值归一化

- **GIVEN** 已加载音频，总采样数为 `Total`
- **WHEN** 业务层设置 `LoopEnd` 为 `0`、负数，或大于 `Total`
- **THEN** 系统 MUST 将生效的循环终点归一化为 `Total`
- **AND** MUST NOT 因此中断播放

#### Scenario: T03 循环起点非法值拒绝

- **GIVEN** 已加载音频，总采样数为 `Total`
- **WHEN** 业务层设置 `LoopStart` 为负数、或大于等于 `Total`
- **THEN** 系统 MUST 拒绝该设置并保持原循环起点不变
- **AND** MUST 通过错误回调向业务层报告失败原因

#### Scenario: T04 循环区间退化保护

- **GIVEN** 已加载音频
- **WHEN** 业务层设置的 `LoopEnd` 小于等于 `LoopStart`
- **THEN** 系统 MUST 拒绝该设置并保持原循环区间不变
- **AND** MUST NOT 进入死循环或无音频输出状态

### Requirement: Loop Configuration At Runtime

系统 SHALL 支持在播放过程中修改循环区间，且修改后立即生效。

#### Scenario: T05 播放中修改循环点立即生效

- **GIVEN** 正在播放，当前播放位置为 `P`，原循环区间为 `[S0, E0)`
- **WHEN** 业务层将循环区间改为 `[S1, E1)`，且 `P` 落在 `[S1, E1)` 之外
- **THEN** 系统 MUST 清空已缓冲的音频数据并跳转到 `S1` 继续播放
- **AND** 跳转 MUST 在 200 毫秒内被听感察觉不到明显中断
- **AND** 系统 MUST 重置循环计数为 `0`

#### Scenario: T06 播放中修改循环点不影响当前位置

- **GIVEN** 正在播放，当前播放位置为 `P`，原循环区间为 `[S0, E0)`
- **WHEN** 业务层将循环区间改为 `[S1, E1)`，且 `P` 仍落在 `[S1, E1)` 之内
- **THEN** 系统 MUST NOT 跳转，从 `P` 继续播放

#### Scenario: T07 关闭循环后的整曲播放

- **GIVEN** 正在播放且循环已开启
- **WHEN** 业务层关闭循环
- **THEN** 系统 MUST 在到达音频文件末尾时停止推进并保持静音输出
- **AND** MUST 通过播放结束回调通知业务层

### Requirement: Loop Cycle Reporting

系统 SHALL 统计并上报每一次循环跳转。

#### Scenario: T08 循环次数单调递增

- **GIVEN** 循环已开启且循环区间有效
- **WHEN** 每完成一次从 `LoopEnd` 回到 `LoopStart` 的跳转
- **THEN** 系统 MUST 将循环计数加一
- **AND** MUST 通过循环事件回调向业务层上报当前累计次数
- **AND** 在发生 seek 或循环点变更后，计数 MUST 被重置为 `0`

#### Scenario: T09 关闭循环时不上报循环事件

- **GIVEN** 循环已关闭
- **WHEN** 播放到达音频文件末尾
- **THEN** 系统 MUST NOT 触发任何循环事件回调

### Requirement: Playback Progress Reporting

系统 SHALL 提供不因循环跳转而回退或跳变的播放进度。

#### Scenario: T10 进度在循环边界保持连续

- **GIVEN** 循环已开启，已至少完成一次循环跳转，循环长度为 `L`
- **WHEN** 业务层读取当前播放位置
- **THEN** 返回值 MUST 单调不减（除 seek 与循环点变更外）
- **AND** 在循环边界处 MUST NOT 出现向前跳变超过 `L` 或向后回退的读数

#### Scenario: T11 进度在 Intro 阶段不误补偿

- **GIVEN** 循环区间为 `[S, E)` 且 `S > 0`，尚未发生任何循环跳转
- **WHEN** 播放位置从 `0` 推进到 `S`
- **THEN** 返回的进度 MUST 等于真实解码位置，不得叠加任何回绕补偿

### Requirement: Playback Transport Control

系统 SHALL 提供加载、播放、暂停、停止、按采样点跳转的基本控制能力。

#### Scenario: T12 加载后可播放

- **GIVEN** 业务层提供一个可解码的音频文件路径
- **WHEN** 加载完成
- **THEN** 系统 MUST 返回采样率、声道数、总采样数
- **AND** 播放 MUST 从采样点 `0` 开始（不直接跳到 `LoopStart`）

#### Scenario: T13 暂停后恢复不丢位置

- **GIVEN** 正在播放，当前播放位置为 `P`
- **WHEN** 业务层暂停后再播放
- **THEN** 系统 MUST 从 `P` 附近继续播放
- **AND** 恢复点与 `P` 的偏差 MUST 小于 100 毫秒

#### Scenario: T14 按采样点跳转

- **GIVEN** 已加载音频，总采样数为 `Total`
- **WHEN** 业务层请求跳转到采样点 `X`
- **THEN** 系统 MUST 将播放位置设置为 `clamp(X, 0, Total)` 并按块对齐
- **AND** MUST 清空已缓冲数据，避免播放出跳转前的残留音频

### Requirement: Continuous Audio Output

系统 SHALL 保证循环边界处音频输出连续，不出现静音间隙或爆音。

#### Scenario: T15 循环边界无静音间隙

- **GIVEN** 循环已开启，循环长度不小于 1 秒
- **WHEN** 连续播放跨越 10 次循环边界
- **THEN** 输出音频 MUST NOT 出现长度超过 5 毫秒的静音间隙

#### Scenario: T16 循环边界不引入额外突变

- **GIVEN** 循环已开启，fixture 的 `LoopStart` 与 `LoopEnd` 处波形相位连续（已知良好循环点）
- **WHEN** 连续播放跨越 10 次循环边界
- **THEN** 边界处相邻采样差 `|x[LoopStart] - x[LoopEnd-1]|` MUST NOT 超过循环体内相邻采样差最大值的 1.5 倍
- **AND** 人工听测 MUST 判定为无爆音
- **AND** 若 fixture 的循环点非相位连续，产生的突变 MUST 完全归因于素材本身，播放器 MUST NOT 额外引入或放大突变

#### Scenario: T17 缓冲欠载可恢复

- **GIVEN** 正在播放
- **WHEN** 解码线程短暂阻塞导致输出缓冲欠载
- **THEN** 系统 MUST 继续输出静音填充而非崩溃或停止
- **AND** 缓冲恢复后 MUST 自动继续播放
- **AND** MUST 通过欠载事件回调上报该情况
