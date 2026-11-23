# Tasks

> 约束：本变更的构建、单测与真机听测均可在本机执行（DevEco Studio 26.0.0 + 真机 MRT-AL10 已就绪）。
> 若环境缺失，未执行任务必须登记到 `docs/verification-debt.md`，不得勾选。

## 1. 规范与素材准备

- [x] 1.1 冻结真实素材清单：`fixtures/real/README.md` 填写 `bgm03_2_a/b.ogg` 与 `水月陵 - flower.mp3` 的格式、总采样数、循环点
- [x] 1.2 生成合成 A/B fixture：`scripts/make-fixtures.py` 增加一对内容不同、边界相位连续的 `ab_pair_44100_stereo_{a,b}.wav`，并写入 `manifest.json`（A→B 突变比值 0.157、循环点比值 0.50，均 ≤ 1.5）

## 2. OGG 解码

- [x] 2.1 引入 `stb_vorbis.c` 与 `stb_vorbis_impl.c` 到 `entry/src/main/cpp/third_party/`，接入 `CMakeLists.txt` 两条构建路径
- [x] 2.2 `AudioDecoder` 支持 `.ogg`：`Open` 分派、`Format` 上报、`SeekToFrame`、`ReadFrames`（覆盖 T12 的 OGG 分支）
- [x] 2.3 补 `DecoderTests` 的 OGG 用例：格式上报、总帧数、seek 落点、读到末尾（新增 3 用例，DecoderTests 50 → 75 断言）

## 3. A/B 配对识别

- [x] 3.1 实现 `ab_pair.{h,cpp}`：`_a`/`_b`、`_A`/`_B`、`_start`/`_end` 三组后缀，大小写不敏感，同目录同扩展名
- [x] 3.2 补 `AbPairTests`：三组后缀各正反两向、找不到配对、非 A/B 文件、大小写混用

## 4. 虚拟时间轴数据源

- [x] 4.1 实现 `audio_source.{h,cpp}`：单文件与 A/B 两种模式，对外暴露 `Format` / `SeekToFrame` / `ReadFrames`
- [x] 4.2 跨文件读取：读到 A 末尾自动切 B，不返回 0；虚拟轴末尾才返回 0
- [x] 4.3 补 `AudioSourceTests`：`p = NA-1` / `p = NA` / `p = NA+NB-1` 三个边界、跨文件连续读取、seek 跨文件

## 5. 接入循环内核

- [x] 5.1 `DecodeThread` 数据源类型由 `AudioDecoder*` 改为 `AudioSource*`，循环跳转逻辑不改语义
- [x] 5.2 `PlaybackEngine` 相应调整，`Load` 时自动识别 A/B 对
- [x] 5.3 回归 0001 的全部 10 个测试套件，确认单文件路径无退化

## 6. 界面与真机验收

- [ ] 6.1 ArkTS 页面支持加载 A/B 曲目（按 A 侧路径加载，自动配对）
- [ ] 6.2 真机听测：`bgm03_2_a/b.ogg` 连续跨越 10 次循环边界，判定 A→B 切换与循环边界均无间隙、无爆音
- [ ] 6.3 真机听测：`水月陵 - flower.mp3` 单文件真实素材，连续跨越 10 次循环边界

## 7. 验证与归档

- [ ] 7.1 运行 `scripts/verify.ps1 --change 0002-ogg-and-ab-pair`，保存日志到 `artifacts/`
- [ ] 7.2 回填 `docs/verify/0002-ogg-and-ab-pair.md`，未执行项登记到 `docs/verification-debt.md`
- [ ] 7.3 更新 `docs/traceability.md` 与 `docs/regression-log.md`
- [ ] 7.4 人类审查证据后执行 `openspec archive 0002-ogg-and-ab-pair`
