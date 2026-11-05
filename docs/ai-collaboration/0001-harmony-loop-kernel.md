# AI 协作留痕：0001-harmony-loop-kernel

> 规范依据：`AI协作严谨工作流规范-SDD-OpenSpec.md` 第 12 节。
> 原则：没有 prompt 原文就标注「无独立留痕」，不补造、不推测。

## 1. prompt 范围

| 日期 | 授权范围 | 明确不做 |
|---|---|---|
| 2026-11-04 | 建立 SDD 规范体系（openspec / docs / fixtures / scripts）；生成合成 fixture；建立验证脚本；创建鸿蒙工程骨架 | 不写播放内核代码、不做 UI、不做数据库 |
| 2026-11-04 | 将 `openspec/` `docs/` `fixtures/` `scripts/` 迁入工程根目录 `SeamlessLoopHarmony` | 不改动业务代码 |
| 2026-11-04 | 包名 `com.example.*` 改为 `com.cpu.*` | 不改其他配置 |
| 2026-11-04 | 按 tasks.md 推进任务 2.2 / 2.3 / 2.4：CMake 链接 OHAudio 与 hilog、AudioOutput 正弦波冒烟、napi 桥接 + ArkTS 按钮 | 不做解码、不做循环、不做数据库、不做 UI 美化；2.5 真机听测跳过 |
| 2026-11-05 | 按 tasks.md 推进任务 3.1 / 3.2 / 3.3：引入 dr_libs（仅 wav/mp3）、实现 AudioDecoder、实现 RingBuffer；建立 cpp/tests 与本机 ctest 链路 | 不引入 flac/vorbis、不实现解码线程与循环逻辑、不做进度计算、不引入第三方测试框架 |
| 2026-11-05 | 按 tasks.md 推进任务 3.4 / 3.5：实现 DecodeThread、写回调改从 FIFO 取数并上报欠载；补充两组单测 | 不实现循环跳转（属 4.2）、不做进度计算、不改 ArkTS 页面 |
| 2026-11-05 | 按 tasks.md 推进任务 4.1：实现 `LoopConfig` 的循环点校验与归一化，补 `LoopConfigTests` | 不实现循环跳转与计数（属 4.2 / 4.3）、不做运行时改点（属 4.4）、不改 UI |
| 2026-11-05 | 真机重新接入后选择「1 → 2 连做」：先完成任务 2.5 真机 G0 冒烟（安装 + 启动 + 听测），再回填任务 1.1 的 `docs/reference-env.md` | 不对循环听测（T15 / T16 / T05）下结论——循环链路与界面尚未实现 |
| 2026-11-05 | 按 tasks.md 推进任务 4.2：解码线程在 LoopEnd 处按帧 seek 回 LoopStart，补 5 个循环用例 | 不实现循环计数与事件（4.3）、不做运行时改点与 FIFO 清理（4.4）、不做进度补偿（5.1） |
| 2026-11-05 | 按 tasks.md 推进任务 4.3：新增 `LoopCounter` 统计与回调，跳转时递增、改点与显式重置时归零，补 `LoopCounterTests` | 不实现事件到 JS 的投递（属 5.3 ThreadSafeFunction）、不做运行时改点（4.4）、不改 UI |
| 2026-11-05 | 按 tasks.md 推进任务 4.4：运行时改循环点，位置在新区间外则清空 FIFO 并跳转，补 `LoopRangeChangeTests` | 不做进度上报（5.1）、不做 play/pause/stop/seek（5.2）、不改 UI |
| 2026-11-05 | 按 tasks.md 推进任务 4.5：关闭循环后播到末尾停止并上报结束事件，补 `LoopStreamTests` | 不做进度上报（5.1）、不做 play/pause/stop/seek（5.2）、不改 UI |

## 2. AI 产出

| 类别 | 文件 |
|---|---|
| 规范 | `openspec/project.md`、`openspec/AGENTS.md`、`openspec/config.yaml`、`openspec/changes/0001-harmony-loop-kernel/{proposal,design,tasks}.md`、`specs/audio-loop-playback/spec.md` |
| 证据 | `docs/traceability.md`、`docs/verify/0001-harmony-loop-kernel.md`、`docs/regression-log.md`、`docs/verification-debt.md`、`docs/reference-env.md`、`docs/observation/loop-mechanism.md` |
| 素材 | `scripts/make-fixtures.py`、`fixtures/synthetic/*`、`fixtures/manifest.json` |
| 脚本 | `scripts/verify.ps1` |
| 工程 | 鸿蒙 Stage 工程骨架（DevEco 模板 + 包名调整） |
| Native（任务 2.2~2.4） | `entry/src/main/cpp/CMakeLists.txt`、`audio_output.h`、`audio_output.cpp`、`napi_init.cpp`、`cpp/types/libentry/{index.d.ts,oh-package.json5,package.json}` |
| 配置（任务 2.2~2.4） | `entry/build-profile.json5`（`externalNativeOptions`）、`entry/oh-package.json5`（`libentry.so` 依赖） |
| UI（任务 2.4） | `entry/src/main/ets/pages/Index.ets`（单个按钮触发 `startTone()` / `stopTone()`） |
| Native（任务 3.1~3.3） | `cpp/third_party/{dr_wav.h,dr_mp3.h,dr_libs_impl.c}`、`cpp/audio_decoder.{h,cpp}`、`cpp/ring_buffer.{h,cpp}` |
| 测试（任务 3.x） | `cpp/tests/{test_common.h,ring_buffer_test.cpp,audio_decoder_test.cpp}`；`cpp/CMakeLists.txt` 增加 `BUILD_TESTS` 双路径 |
| Native（任务 3.4~3.5） | `cpp/decode_thread.{h,cpp}`、`cpp/audio_output_logic.h`；`cpp/audio_output.{h,cpp}` 改为 FIFO 驱动 |
| 测试（任务 3.4~3.5） | `cpp/tests/{audio_output_logic_test.cpp,decode_thread_test.cpp}` |
| Native（任务 4.1） | `cpp/loop_config.{h,cpp}` |
| 测试（任务 4.1） | `cpp/tests/loop_config_test.cpp`；`cpp/CMakeLists.txt` 增加 `LoopConfigTests` |
| Native（任务 4.2） | `cpp/decode_thread.{h,cpp}`：循环区间原子存储、LoopEnd 处跳转、区间不变量防护 |
| 测试（任务 4.2） | `cpp/tests/decode_thread_test.cpp`：新增 `CaptureSink` 与 5 个循环用例 |
| Native（任务 4.3） | `cpp/loop_counter.h`；`cpp/decode_thread.{h,cpp}` 接入计数与回调 |
| 测试（任务 4.3） | `cpp/tests/loop_counter_test.cpp`；`cpp/CMakeLists.txt` 增加 `LoopCounterTests` |
| Native（任务 4.4） | `cpp/ring_buffer.{h,cpp}` 新增 `RequestFlush` / `FlushCount` / `FlushRequestCount` / `HasPendingFlush`；`cpp/decode_thread.{h,cpp}` 新增 `pendingSeek_`、`decodedPosition_` 与运行时改点分支 |
| 测试（任务 4.4） | `cpp/tests/loop_range_change_test.cpp`；`cpp/CMakeLists.txt` 增加 `LoopRangeChangeTests` |
| Native（任务 4.5） | `cpp/decode_thread.{h,cpp}` 新增 `SetEndCallback`、`NotifyEnd`（只上报一次） |
| 测试（任务 4.5） | `cpp/tests/loop_stream_test.cpp`；`cpp/CMakeLists.txt` 增加 `LoopStreamTests` |

关键决策记录在 `openspec/changes/0001-harmony-loop-kernel/design.md`（8 条决策，含放弃的方案）。

## 3. 人工修正点

| 日期 | 内容 |
|---|---|
| 2026-11-04 | 人类指定 HarmonyOS SDK 26.0.0 |
| 2026-11-04 | 人类确认真实素材由自己提供 |
| 2026-11-04 | 人类决定将规范目录迁入工程根目录（符合规范第 4 节） |
| 2026-11-04 | 人类指定包名使用 `cpu` 替换 `example` |
| 2026-11-04 | 人类确认 tasks.md 第 3 行约束：构建与静态校验本机可跑，仅 2.5 / 6.2 / 6.3 需真机 |
| 2026-11-04 | 人类确认本机无真机（`hdc list targets` 为空），不尝试安装与听测，2.5 登记为阻塞 |
| 2026-11-05 | 人类接入真机 HUAWEI MRT-AL10（API 26），安装报 `no signature file`；随后断开连接 |
| 2026-11-05 | 人类确认 dr_libs 只引入 wav/mp3、单测用裸断言 + ctest（不引入第三方框架） |
| 2026-11-05 | 人工审查发现：`verify.ps1` 原注释把「观察到一次挂起」写成「daemon 启动会挂起」，属过度断言；已改为事实陈述（根因未定位） |
| 2026-11-05 | 人工审查要求：单测首次跑出失败后，先确认是实现错还是断言错，再修；本次确认为断言错（B1），实现未改语义，仅补注释澄清 |
| 2026-11-05 | 人工审查指出 T17 台账不准确：原「阻塞」实为代码未接线（`SetUnderrunCallback` 无调用点），且第 2 条「恢复后继续播放」完全漏记。已拆条重记并补测 |

## 4. 回归结果

| 日期 | 命令 | 结果 | 证据 |
|---|---|---|---|
| 2026-11-04 | `openspec validate --all --strict` | PASS（1 passed, 0 failed） | `artifacts/verify-0001-harmony-loop-kernel-20261104-143637.log` |
| 2026-11-04 | `python scripts/make-fixtures.py` | PASS（3 文件 + manifest） | 同上 |
| 2026-11-04 | `hvigorw assembleHap` | PASS（BUILD SUCCESSFUL） | 同上 |
| 2026-11-04 | `verify.ps1 --scope build`（改包名后） | PASS | `artifacts/verify-0001-harmony-loop-kernel-20261104-144638.log` |
| 2026-11-04 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 2.2~2.4 后） | PASS（3 steps, 0 failed） | `artifacts/verify-0001-harmony-loop-kernel-20261104-223519.log` |
| 2026-11-04 | `llvm-readelf -d libentry.so` | PASS（NEEDED 含 `libohaudio.so`、`libhilog_ndk.z.so`） | 同上报告第 3 节 |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 3.1~3.3 后） | PASS（4 steps, 0 failed；单测 2 套件 / 153 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-102022.log` |
| 2026-11-05 | `ctest -C Release`（本机 MSVC） | PASS（RingBufferTests / DecoderTests 全通过） | 同上；`artifacts/native-build/` |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 3.4~3.5 后） | PASS（4 steps, 0 failed；单测 4 套件 / 796 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-112403.log` |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 4.1 后） | PASS（4 steps, 0 failed；单测 5 套件 / 870 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-115309.log` |
| 2026-11-05 | `hdc install -r entry-default-signed.hap` + `aa start`（真机 MRT-AL10） | PASS（安装成功、启动成功，进程存活） | `docs/verify/0001-harmony-loop-kernel.md`「任务 2.5 真机 G0 冒烟」 |
| 2026-11-05 | 真机 G0 听测（任务 2.5） | PASS（人类判定：清晰 1 秒 440Hz 正弦音、无爆音；日志 `total underrun bytes=0`） | 同上 |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 4.2 后） | PASS（4 steps, 0 failed；单测 5 套件 / 918 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-134253.log` |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 4.3 后） | PASS（4 steps, 0 failed；单测 6 套件 / 964 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-135348.log` |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 4.4 后） | PASS（4 steps, 0 failed；单测 7 套件 / 1007 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-182218.log` |
| 2026-11-05 | `verify.ps1 --change 0001-harmony-loop-kernel`（任务 4.5 后） | PASS（4 steps, 0 failed；单测 8 套件 / 1043 断言） | `artifacts/verify-0001-harmony-loop-kernel-20261105-183703.log` |

未执行项：T10~T11、T13、T15~T16 为 `NOT RUN`（代码未实现）；T02 / T05 为部分通过（运行时链路与听感条款）；T17 第 3 条（事件上报）代码未接线；任务 6.2 / 6.3 循环听测待 5.1~6.1 完成后执行。详见 `docs/verification-debt.md`。
