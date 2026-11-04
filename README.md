# SeamlessLoopHarmony

HarmonyOS 版无缝循环音乐播放器。把 Windows（WPF）与 Android 端已验证的**采样级无缝循环**能力移植到鸿蒙。

- 功能与数据契约基准：`..\seamless loop music`（WPF）
- 移动端音频引擎参考：`..\SeamlessLoopMobile`（Kotlin + Oboe）
- 工作方法：`AI协作严谨工作流规范-SDD-OpenSpec.md`（SDD + OpenSpec）

## 阶段看板

| Gate | 目标 | 状态 | 证据 |
|---|---|---|---|
| G0 | 工程可构建 + OHAudio 冒烟（输出正弦波） | **部分完成** | 工程已可构建（`assembleHap` BUILD SUCCESSFUL）；OHAudio 冒烟未实现，且无真机连接，见 [reference-env](docs/reference-env.md) |
| G1 | 规范冻结（严格校验全绿、场景全部映射） | **进行中** | `openspec validate --all --strict` 通过；[traceability](docs/traceability.md) 17 条场景已映射；合成 fixture 已生成 |
| G2 | 播放内核完成（循环跳转 / 计数 / 进度） | 未开始 | — |
| G3 | 真机听感验收（无断点、无爆音） | 未开始 | — |
| G4 | 音乐库与数据层 | 未开始 | — |
| G5 | 后台播放与播控中心 | 未开始 | — |
| G6 | 播放统计与跨端同步 | 未开始 | — |
| G7 | 验收完成、变更归档 | 未开始 | — |

## 当前变更

| change | 阶段 | 说明 |
|---|---|---|
| [`0001-harmony-loop-kernel`](openspec/changes/0001-harmony-loop-kernel/proposal.md) | Build 进行中（3/30 任务） | 只做播放内核验证，不做列表 / 数据库 / 歌单 |

## 关键文档

| 文档 | 作用 |
|---|---|
| [project.md](openspec/project.md) | 项目边界与技术栈锁定 |
| [AGENTS.md](openspec/AGENTS.md) | AI 操作规程与硬性技术约束 |
| [loop-mechanism.md](docs/observation/loop-mechanism.md) | 从原项目读出的无缝循环机制（移植事实来源） |
| [traceability.md](docs/traceability.md) | 17 条验收场景 ↔ 规范 ↔ 测试映射 |
| [fixtures/README.md](fixtures/README.md) | 测试素材说明（合成 + 真实） |
| [verification-debt.md](docs/verification-debt.md) | 未执行的验证台账 |
| [regression-log.md](docs/regression-log.md) | 回归基线 |

## 命令

```powershell
openspec validate --all --strict                          # 规范自检
python scripts/make-fixtures.py                           # 生成合成测试音频
powershell -File scripts/verify.ps1 --change <id>          # 唯一验证入口

# 构建（需 DevEco Studio，本机已安装于 D:\program\DevEco Studio）
$env:DEVECO_SDK_HOME = "D:\program\DevEco Studio\sdk"
& "D:\program\DevEco Studio\tools\hvigor\bin\hvigorw.bat" --mode module -p product=default assembleHap
```

## 下一步

1. 冻结 [reference-env](docs/reference-env.md) 的剩余项：真机型号与系统版本。
2. 按 [fixtures/real/README.md](fixtures/real/README.md) 提供真实素材（或 WPF 端 `.db`）。
3. 进入 Build 阶段：OHAudio 冒烟 → 解码 → 循环核心。
