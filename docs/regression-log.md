# 回归基线（regression-log）

> 每次验证通过后**追加**一行，历史行不得修改。

| 日期 | change | 通过用例 | 失败用例 | 跳过 | 证据 |
|---|---|---|---|---|---|
| 2026-11-04 | `0001-harmony-loop-kernel`（Plan 阶段） | 规范校验 `openspec validate --strict` | 0 | 全部代码与听测任务（环境未就绪） | `docs/verification-debt.md` |
| 2026-11-04 | `0001-harmony-loop-kernel`（fixture 生成） | 合成 fixture 生成 + 相位连续性数值检查（比值 0.50 ≤ 1.5） | 0 | HAP 构建、真机听测 | [verify/0001](verify/0001-harmony-loop-kernel.md) |
| 2026-11-04 | `0001-harmony-loop-kernel`（迁移后复验） | `openspec validate --strict`、`make-fixtures.py`、`hvigorw assembleHap` 三步全 PASS | 0 | 真机听测 | [verify/0001](verify/0001-harmony-loop-kernel.md)、`artifacts/verify-0001-harmony-loop-kernel-20261104-143637.log` |
| 2026-11-05 | `0001-harmony-loop-kernel`（Build 阶段收口，任务 1.1~5.3、6.1、7.1~7.4） | `openspec validate --all --strict` + 合成 fixture + 10 个 native 套件（1172 断言）+ `hvigorw assembleHap` 四步全 PASS | 0 | 6.2 / 6.3 真机听测（真机断开）、7.5 归档（待人类批准）、任务 1.3 真实素材 | [verify/0001](verify/0001-harmony-loop-kernel.md)、`artifacts/verify-0001-harmony-loop-kernel-20261105-191954.log` |
