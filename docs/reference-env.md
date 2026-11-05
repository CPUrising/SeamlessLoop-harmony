# 参考环境锁定表

> 冻结后如需变更，必须走 change。未冻结项标注"待定"。

| 项 | 值 | 冻结日期 | 备注 |
|---|---|---|---|
| 操作系统（开发机） | Windows 11（当前开发机） | 2026-11-04 | |
| HarmonyOS SDK 版本 | **26.0.0** | 2026-11-04 | 用户指定。从 26.0.0 起改用 SemVer（X.Y.Z），不再带 `(N)` 括号形式 |
| `compileSdkVersion` | `26.0.0` | 2026-11-04 | 编译所用 SDK |
| `targetAPIVersion` | `26.0.0` | 2026-11-04 | |
| `compatibleSdkVersion` | `26.0.0` | 2026-11-04 | 工程 `build-profile.json5` 已设定；2026-11-05 真机 MRT-AL10 实测 `const.ohos.apiversion = 26`，已对齐，不下调 |
| DevEco Studio | 26.0.0 Release，`D:\program\DevEco Studio`（build `DS-261.23567.138.36.2600865`） | 2026-11-04 | 已安装并实测可构建 |
| hvigor / ohpm 版本 | hvigor 6.26.9（随 DevEco 安装） | 2026-11-04 | |
| 目标真机型号 | **HUAWEI MRT-AL10**（设备序列号 `5KTUT25424016774`） | 2026-11-05 | 无缝听测必须真机，模拟器不作为证据 |
| 真机系统版本 | **OpenHarmony 7.0.0.111(SP12C00E111R5P2)**，`const.ohos.apiversion = 26` | 2026-11-05 | 与 `compatibleSdkVersion 26.0.0` 一致，无需下调 |
| OpenSpec CLI | 1.14.3 | 2026-11-04 | 已验证可用 |
| CMake | 4.2.3 | 2026-11-04 | 已验证可用 |
| Node.js | 已安装（`C:\Program Files\nodejs`） | 2026-11-04 | |
| Git | 2.4x（`D:\program\Git`） | 2026-11-04 | |

## OHAudio 接口可用性（基于 26.0.0）

| 能力 | 起始版本 | 说明 |
|---|---|---|
| `OH_AudioStreamBuilder_SetLatencyMode` | API 10 | 普通 / 低时延两种模式；本变更新增**普通模式**，低时延作为后续优化项 |
| `OH_AudioStreamManager_IsFastPlaybackSupported` | API 20 | 查询设备是否支持低时延播放 |
| 播放写回调 | API 10 | 本变更使用的核心接口 |

**结论**：26.0.0 完全覆盖本变更所需接口，无需降级 API。

## 本机现状核查（2026-11-05 更新）

| 检查项 | 结果 |
|---|---|
| DevEco Studio 安装目录 | `D:\program\DevEco Studio`（build `DS-261.23567.138.36.2600865`） |
| HarmonyOS SDK | `D:\program\DevEco Studio\sdk\default`，版本 `26.0.0.105`（apiVersion 26，Release） |
| hdc 命令 | `D:\program\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe`，Ver 3.2.0f |
| 已连接真机 | `5KTUT25424016774`（HUAWEI MRT-AL10，API 26） |
| openspec | 可用（1.14.3） |
| cmake | 可用 |
| node / npm | 可用（node v24.18.0） |

**构建实测**：设置 `DEVECO_SDK_HOME=D:\program\DevEco Studio\sdk` 后执行 `hvigorw --mode module -p product=default assembleHap`，结果 `BUILD SUCCESSFUL`，产出 `entry/build/default/outputs/default/entry-default-signed.hap`（已配置签名）。

**真机实测（2026-11-05 12:19）**：`hdc install -r` 安装 signed HAP 成功；`aa start -a EntryAbility -b com.cpu.seamlessloopharmony` 启动成功；点击 G0 按钮听到 1 秒 440Hz 正弦波、无爆音；`SLH_AudioOutput` 日志 `renderer started: 48000 Hz, 2 ch`，`total underrun bytes=0`。证据见 `docs/verify/0001-harmony-loop-kernel.md`。

**结论**：工程可构建、规范可校验、真机可安装并真实发声。循环听测（T15 / T16 / T05）仍待任务 4.2~6.1 完成后执行。
