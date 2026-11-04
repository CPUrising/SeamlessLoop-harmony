# AGENTS.md

本仓库使用 OpenSpec + SDD（规范驱动开发）。所有 AI 智能体必须按本文件执行。
完整方法论见仓库根目录 `AI协作严谨工作流规范-SDD-OpenSpec.md`。

## 0. 开工前必读

1. `openspec/project.md`（项目边界、技术栈锁定）
2. `openspec/specs/`（当前基线规范）
3. `docs/traceability.md`（验收场景与测试映射）
4. 本文件

## 1. 角色与边界

- 人类：定义意图、批准 change、审查证据、决定归档。
- 智能体：读上下文、拆任务、写代码与测试、跑验证、报告偏差。
- 智能体**没有权限**：扩大范围、跳过测试、直接改基线规范、伪造验证结果。

## 2. 工作循环

| 阶段 | 命令 | 产物 |
|---|---|---|
| Plan | `openspec new change <id>` | proposal / design / tasks / 增量 spec |
| Check | `openspec validate <id> --strict` | 校验通过 |
| Build | 按 `tasks.md` 顺序实现 | 源码 + 测试 + 勾选的 tasks |
| Verify | `powershell -File scripts/verify.ps1 --change <id>` | 日志 + `docs/verify/<id>.md` |
| Merge | `openspec archive <id>`（**需人类批准**） | 更新基线规范 |

## 3. 命名与规模

- change id：`NNNN-kebab-case`，单调递增，不复用。
- 一个 change 只含一个可命名的行为增量；超过 8 个任务必须拆分。
- 纯重构 / 纯格式化 / 纯文档，各自单独成 change，并在 `.openspec.yaml` 写 `skip_specs: true`。

## 4. 硬性技术约束

以下为不可协商的约束，违反即视为缺陷：

1. **循环必须走 PCM 通道**。禁止用 AVPlayer 的 `loop` 属性或 `completed` 事件重播来实现"采样级无缝循环"——那是整曲循环，且有断点。AVPlayer 只能用于"整曲从头循环"的降级场景。
2. **循环点持久化单位是采样点数（int64）**，不是毫秒、不是百分比。跨端传输与存储都按采样点数。
3. **音频数据写入必须块对齐**：任何 seek / 跳转的字节位置必须按 `BlockAlign` 取整，否则产生爆音。参考原项目 `LoopStream.SafeSetPosition`。
4. **UI 线程不得做解码或分析**。解码在 Native 线程，分析走 TaskPool 或 Native 线程。
5. **跨端数据格式（SQLite schema、播放统计 v2 JSON）不得擅自变更**。需要变更时单独开 change 并给出三端兼容说明。
6. **测试只用合成音频**，禁止提交真实音乐文件或包含个人路径的 fixture。

## 5. 验证与证据

- 唯一验证入口：`scripts/verify.ps1`。
- 每次验证必须产出 `docs/verify/<change-id>.md`，未执行的项写 `NOT RUN` 并登记到 `docs/verification-debt.md`。
- 失败先读日志定位；最多自愈 3 轮；**禁止放宽断言或删除用例**。
- 音频"无缝"的判定包含人工听感验收，必须在报告中写明听感结论与听测设备。

## 6. 范围纪律

- 顺手优化必须单独成 change。
- 概率性功能（如循环点自动分析）必须保留全链路留痕（原始输出 → 解析结果 → 人工校正），并按 `docs/eval/` 规范评估。
- 不得为了通过 `openspec validate` 而虚构需求。

## 7. 交接

- 编辑端不得声称"已验证"。
- 验证端只产出证据；发现缺陷按本文件修复后重跑，并在偏差表登记。
- 环境缺失（无真机、无 DevEco）必须显式登记为阻塞，不得用"应该没问题"替代。
