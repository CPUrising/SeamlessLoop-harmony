# Delta Spec: audio-format-support

## ADDED Requirements

### Requirement: OGG Vorbis Decoding

系统 SHALL 支持解码 OGG Vorbis 音频文件，并像 WAV / MP3 一样上报采样率、声道数与总采样数。

#### Scenario: F01 OGG 文件加载与格式上报

- **GIVEN** 业务层提供一个可解码的 `.ogg` 文件路径
- **WHEN** 加载完成
- **THEN** 系统 MUST 返回正确的采样率、声道数与总采样数
- **AND** 总采样数 MUST 与解码器实际可读出的帧数一致

#### Scenario: F02 OGG 按采样点跳转

- **GIVEN** 已加载 OGG 文件，总采样数为 `Total`
- **WHEN** 业务层请求跳转到采样点 `X`
- **THEN** 系统 MUST 将解码位置设置为 `clamp(X, 0, Total)`
- **AND** 跳转后读出的 PCM MUST 与从头顺序解码到该位置的结果一致

#### Scenario: F03 不支持的扩展名明确失败

- **GIVEN** 业务层提供一个扩展名不在 `.wav` / `.mp3` / `.ogg` 之内的文件
- **WHEN** 尝试加载
- **THEN** 系统 MUST 返回失败
- **AND** MUST NOT 猜测格式或崩溃
