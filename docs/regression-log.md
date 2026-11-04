# 回归基线（regression-log）

> 每次验证通过后**追加**一行，历史行不得修改。

| 日期 | change | 通过用例 | 失败用例 | 跳过 | 证据 |
|---|---|---|---|---|---|
| 2026-11-04 | `0001-harmony-loop-kernel`（Plan 阶段） | 规范校验 `openspec validate --strict` | 0 | 全部代码与听测任务（环境未就绪） | `docs/verification-debt.md` |
| 2026-11-04 | `0001-harmony-loop-kernel`（fixture 生成） | 合成 fixture 生成 + 相位连续性数值检查（比值 0.50 ≤ 1.5） | 0 | HAP 构建、真机听测 | [verify/0001](verify/0001-harmony-loop-kernel.md) |
| 2026-11-04 | `0001-harmony-loop-kernel`（迁移后复验） | `openspec validate --strict`、`make-fixtures.py`、`hvigorw assembleHap` 三步全 PASS | 0 | 真机听测 | [verify/0001](verify/0001-harmony-loop-kernel.md)、`artifacts/verify-0001-harmony-loop-kernel-20261104-143637.log` |
