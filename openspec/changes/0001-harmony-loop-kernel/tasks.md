# Tasks

> 约束：本阶段仅验证播放内核。构建与静态校验已可在本机执行（DevEco Studio 26.0.0 已就绪）；仅真机安装与听测（2.5 / 6.2 / 6.3）依赖 HarmonyOS 真机。
> 若环境缺失，未执行任务必须登记到 `docs/verification-debt.md`，不得勾选。

## 1. 规范与环境准备

- [x] 1.1 冻结 `docs/reference-env.md`：DevEco 26.0.0 实际安装版本、真机型号与系统版本、`compatibleSdkVersion` 下限（真机 MRT-AL10 / API 26 / OpenHarmony 7.0.0.111 已回填）
- [x] 1.2 写 `scripts/make-fixtures.py`，生成 `fixtures/synthetic/` 三个相位连续样本与 `manifest.json`
- [ ] 1.3 收集 `fixtures/real/` 素材并填写清单：1 首带已知循环点的整曲（或对应 `.db`）、1 组 A/B 对
- [x] 1.4 建立 `scripts/verify.ps1`（唯一验证入口），支持 `--scope unit` 与 `--change`

## 2. 工程骨架与 OHAudio 冒烟

- [x] 2.1 创建 HarmonyOS Stage 工程：`AppScope` + `entry` 单模块，`module.json5` 声明 Native 库（工程已创建并可 `assembleHap` 构建成功；Native 库声明已在任务 2.2 完成）
- [x] 2.2 配置 `entry/src/main/cpp/CMakeLists.txt`，链接 `libohaudio.so` 与 `libhilog_ndk.z.so`
- [x] 2.3 实现 `AudioOutput`：构建 OHAudio renderer，写回调输出 1 秒 440Hz 正弦波
- [x] 2.4 napi 桥接暴露 `startTone() / stopTone()`，ArkTS 页面加一个按钮触发
- [x] 2.5 真机验证：点击按钮听到 1 秒正弦波，无爆音（G0 冒烟；2026-11-05 12:19 MRT-AL10 实测）

## 3. 解码与连续播放

- [x] 3.1 引入 dr_libs（wav/mp3）到 `entry/src/main/cpp/third_party/`，保持与原项目同版本
- [x] 3.2 实现 `AudioDecoder`：加载文件，输出采样率、声道数、总采样数，支持按采样点 seek
- [x] 3.3 实现 `RingBuffer`（单生产者单消费者，容量 2 秒）
- [x] 3.4 实现解码线程：持续解码并写入 FIFO，FIFO 满时等待
- [x] 3.5 写回调改为从 FIFO 取数据；FIFO 空时写静音并上报欠载事件（覆盖 T17）

## 4. 循环核心

- [x] 4.1 实现 `LoopConfig`：校验并归一化 LoopStart / LoopEnd（覆盖 T02 / T03 / T04）
- [x] 4.2 解码线程实现循环跳转：到达 LoopEnd 时按 BlockAlign 对齐 seek 回 LoopStart（覆盖 T01）
- [x] 4.3 实现循环计数与事件回调，seek 与循环点变更时重置计数（覆盖 T08 / T09）
- [x] 4.4 实现运行时修改循环点：若当前播放位置落在新区间外则清空 FIFO 并跳转（覆盖 T05 / T06；T05 的 200ms 听感条款待 6.3）
- [ ] 4.5 实现关闭循环后播放到末尾停止并上报结束事件（覆盖 T07）

## 5. 进度与控制接口

- [ ] 5.1 实现进度计算：解码位置 − FIFO 剩余字节，含 `hasLoopedSinceSeek` 回绕补偿（覆盖 T10 / T11）
- [ ] 5.2 实现 `play / pause / stop / seekToSample`，seek 时清空 FIFO 并重置补偿标志（覆盖 T13 / T14）
- [ ] 5.3 napi 桥接完整暴露 API 与事件（`ThreadSafeFunction`）

## 6. 最小界面与真机验收

- [ ] 6.1 ArkTS 页面：加载固定路径 fixture、播放/暂停、显示进度与循环次数
- [ ] 6.2 真机听测：连续跨越 10 次循环边界，判定无静音间隙、无爆音（覆盖 T15 / T16）
- [ ] 6.3 真机验证：播放中修改循环点，听感无明显中断（覆盖 T05 的 200ms 条款）

## 7. 验证与归档

- [ ] 7.1 为 T01~T04、T08~T11、T14 编写单元测试（C++ 侧，可在本机跑）
- [ ] 7.2 运行 `scripts/verify.ps1 --change 0001-harmony-loop-kernel`，保存日志到 `artifacts/`
- [ ] 7.3 回填 `docs/verify/0001-harmony-loop-kernel.md`，未执行项登记到 `docs/verification-debt.md`
- [ ] 7.4 更新 `docs/traceability.md` 与 `docs/regression-log.md`
- [ ] 7.5 人类审查证据后执行 `openspec archive 0001-harmony-loop-kernel`
