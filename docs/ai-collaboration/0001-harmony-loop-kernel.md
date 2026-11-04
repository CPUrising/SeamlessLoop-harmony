# AI 协作留痕：0001-harmony-loop-kernel

> 规范依据：`AI协作严谨工作流规范-SDD-OpenSpec.md` 第 12 节。
> 原则：没有 prompt 原文就标注「无独立留痕」，不补造、不推测。

## 1. prompt 范围

| 日期 | 授权范围 | 明确不做 |
|---|---|---|
| 2026-11-04 | 建立 SDD 规范体系（openspec / docs / fixtures / scripts）；生成合成 fixture；建立验证脚本；创建鸿蒙工程骨架 | 不写播放内核代码、不做 UI、不做数据库 |
| 2026-11-04 | 将 `openspec/` `docs/` `fixtures/` `scripts/` 迁入工程根目录 `SeamlessLoopHarmony` | 不改动业务代码 |
| 2026-11-04 | 包名 `com.example.*` 改为 `com.cpu.*` | 不改其他配置 |

## 2. AI 产出

| 类别 | 文件 |
|---|---|
| 规范 | `openspec/project.md`、`openspec/AGENTS.md`、`openspec/config.yaml`、`openspec/changes/0001-harmony-loop-kernel/{proposal,design,tasks}.md`、`specs/audio-loop-playback/spec.md` |
| 证据 | `docs/traceability.md`、`docs/verify/0001-harmony-loop-kernel.md`、`docs/regression-log.md`、`docs/verification-debt.md`、`docs/reference-env.md`、`docs/observation/loop-mechanism.md` |
| 素材 | `scripts/make-fixtures.py`、`fixtures/synthetic/*`、`fixtures/manifest.json` |
| 脚本 | `scripts/verify.ps1` |
| 工程 | 鸿蒙 Stage 工程骨架（DevEco 模板 + 包名调整） |

关键决策记录在 `openspec/changes/0001-harmony-loop-kernel/design.md`（8 条决策，含放弃的方案）。

## 3. 人工修正点

| 日期 | 内容 |
|---|---|
| 2026-11-04 | 人类指定 HarmonyOS SDK 26.0.0 |
| 2026-11-04 | 人类确认真实素材由自己提供 |
| 2026-11-04 | 人类决定将规范目录迁入工程根目录（符合规范第 4 节） |
| 2026-11-04 | 人类指定包名使用 `cpu` 替换 `example` |

## 4. 回归结果

| 日期 | 命令 | 结果 | 证据 |
|---|---|---|---|
| 2026-11-04 | `openspec validate --all --strict` | PASS（1 passed, 0 failed） | `artifacts/verify-0001-harmony-loop-kernel-20261104-143637.log` |
| 2026-11-04 | `python scripts/make-fixtures.py` | PASS（3 文件 + manifest） | 同上 |
| 2026-11-04 | `hvigorw assembleHap` | PASS（BUILD SUCCESSFUL） | 同上 |
| 2026-11-04 | `verify.ps1 --scope build`（改包名后） | PASS | `artifacts/verify-0001-harmony-loop-kernel-20261104-144638.log` |

未执行项：全部功能场景（T01~T17）为 `NOT RUN`，代码未实现；真机听测阻塞。详见 `docs/verification-debt.md`。
