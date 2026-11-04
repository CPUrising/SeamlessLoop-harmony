# SeamlessLoopHarmony 项目上下文

> 本文件是全局项目上下文（单一真实源之一）。任何 change 的产物都必须与本文件一致。
> 版本：1.0 · 建立日期：2026-11-04

## 1. 项目是什么

把已有的「无缝循环音乐播放器」移植到 HarmonyOS。原项目已有三个成熟实现：

| 端 | 目录 | 技术栈 | 定位 |
|---|---|---|---|
| Windows 桌面端 | `..\seamless loop music` | .NET Framework 4.8 + WPF + NAudio | **功能与数据契约的基准**（最高权威） |
| Android 端 | `..\SeamlessLoopMobile` | Kotlin + Jetpack Compose + C++/Oboe | **移动端交互与 NDK 音频引擎的参考** |
| HarmonyOS 端 | 本目录（`SeamlessLoopHarmony`） | ArkTS + C++/OHAudio | 本次移植目标 |

核心价值不是"能播放音乐"，而是**采样级无缝循环**：每首曲子存一对采样点 `LoopStart` / `LoopEnd`，播放时从文件头（或 `LoopStart`）播放到 `LoopEnd`，然后**在字节层面跳回** `LoopStart`，听感上无断点、无爆音、进度条不跳变。

配套能力：A/B 双文件拼接（Intro + Loop）、循环点自动分析（`loopfinder` C++ 库）、歌单与音乐库、播放统计与 GitHub 跨端同步。

## 2. 边界

### In scope

- HarmonyOS 单端本地音乐播放，含采样级无缝循环。
- 复用现有 C++ 资产：`loopfinder`（循环点分析）、音频解码器。
- 与桌面端 / Android 端的数据契约兼容（SQLite schema、播放统计 v2 同步格式）。
- 后台播放与系统播控中心（AVSession）。

### Out of scope（当前阶段明确不做）

- 投播 / 分布式流转到智慧屏、车机、音箱。
- 在线流媒体、歌词、MV、云盘。
- 音效处理（EQ、变速变调、淡入淡出）。
- 桌面端 / Android 端的功能新增或修复。
- iOS / 其他平台。

## 3. 技术栈（锁定，变更需走 change）

| 项 | 选择 | 说明 |
|---|---|---|
| UI 框架 | ArkTS + ArkUI 声明式 | Stage 模型，`@Component` / `@State` 体系 |
| 开发工具 | DevEco Studio 26.0.0 | HarmonyOS SDK 26.0.0（SemVer，无 `(N)` 后缀）；详见 `docs/reference-env.md` |
| 音频输出 | **OHAudio（NDK / C API）** | 需自建解码 FIFO + 循环跳转，理由见 `changes/0001/design.md` |
| 音频解码 | 复用 `loopfinder` 内的 dr_libs（wav/mp3/flac）+ stb_vorbis | 与桌面端一致，避免双解码器行为差异 |
| 循环点分析 | 复用 `..\seamless loop music\loopfinder` | 纯 C++ + C API，换工具链重编 |
| 数据存储 | HarmonyOS `relationalStore`（RDB，底层 SQLite） | schema 对齐桌面端 3NF |
| 后台播放 | 长时任务 + AVSession | 对应 Android 的 ForegroundService + MediaSession |
| 测试 | DevEco 单元测试（ohosTest / test）+ 真机手工听感验收 | 音频无缝无法纯自动化判定 |

**禁止**：不引入第三方音频播放 SDK（如 SmartXPlayer）作为主播放通道；不引入与上述冲突的框架。

## 4. 工程标准

- 语言：代码注释、文档、提交信息用中文；标识符用英文。
- 分层：UI（ArkUI）→ Service（播放控制 / 库管理）→ Data（RDB Repository）→ Native（C++ 音频引擎与分析引擎）。UI 不得直接调 Native。
- 单文件职责单一；Native 与 ArkTS 的边界只通过一层薄 napi 桥接暴露，桥接层不含业务逻辑。
- 所有跨端共享的数据格式（数据库 schema、同步 JSON）变更必须单独开 change，并同时给出三端兼容性说明。
- 循环点的单位统一为**采样点数**（int64），不使用毫秒或百分比作为持久化单位。

## 5. 数据与隐私

- 音乐文件与数据库全部本地存储，不上传。
- GitHub 同步仅上传歌单、循环点、评分、播放统计等结构化数据，不含音频文件、不含文件绝对路径。
- 测试与 fixtures 只使用合成音频（自建正弦波 / 噪声 / 已知循环点的拼接样本），**禁止使用真实个人音乐库**。
- 日志不得输出用户的完整文件路径。

## 6. 规范资产约定

| 路径 | 作用 |
|---|---|
| `openspec/specs/` | 基线规范，实施阶段禁止直接编辑 |
| `openspec/changes/` | 增量变更沙箱 |
| `docs/observation/` | 对原项目的黑盒观察记录（移植的事实来源） |
| `docs/traceability.md` | 验收场景 ↔ 规范 ↔ 测试 映射 |
| `docs/verify/` | 每个 change 的验证证据 |
| `docs/verification-debt.md` | 未执行的验证台账 |
| `docs/regression-log.md` | 回归基线（只增不改） |
| `docs/reference-env.md` | 工具链与设备版本锁定 |
| `fixtures/` | 合成测试音频与 Ground Truth |
| `artifacts/` | 日志与结果包（不入库） |

## 7. 命令

```powershell
# 规范校验（唯一自检入口）
openspec validate --all --strict

# 验证（唯一执行入口）
powershell -File scripts/verify.ps1                    # 全量
powershell -File scripts/verify.ps1 --scope unit       # 只跑单元测试
powershell -File scripts/verify.ps1 --change 0001-harmony-loop-kernel

# 构建（DevEco Studio 已安装于 D:\program\DevEco Studio）
$env:DEVECO_SDK_HOME = "D:\program\DevEco Studio\sdk"
& "D:\program\DevEco Studio\tools\hvigor\bin\hvigorw.bat" --mode module -p product=default assembleHap
```

## 8. AI 协作纪律

完整规程见 `openspec/AGENTS.md` 与仓库根目录 `AI协作严谨工作流规范-SDD-OpenSpec.md`。三条最容易被违反的：

1. **不得伪造证据**：没跑过就是 `NOT RUN`，写进 `docs/verification-debt.md`。
2. **不得扩大范围**：只做当前 change 授权的事。
3. **不得直接改基线规范**：行为变化走增量规范，归档时合并。
