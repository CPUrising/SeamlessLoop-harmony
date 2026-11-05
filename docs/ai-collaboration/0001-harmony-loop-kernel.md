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

未执行项：T01~T11、T13、T15~T17 为 `NOT RUN`（代码未实现）；任务 2.5 / 6.2 / 6.3 真机听测阻塞（签名未配置 + 真机已断开）。详见 `docs/verification-debt.md`。
