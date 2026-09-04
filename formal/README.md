# 《燧火纪：部落黎明》正式版

## 大型扩展 V2 战役

当前源码已接入 `CampaignGame`：包含快速8季、课程16季、长期32季三种模式，以及六部落、16地点、永久小队、苍林任务、动态外交、贸易与贝币、派系稳定、三段战争、结局选择和编年史数据。主菜单可用数字或中英文进入：

- `7` / `campaign quick` / `战役 快速`：快速8季。
- `8` / `campaign course` / `战役 课程`：课程16季。
- `9` / `campaign long` / `战役 长期`：长期32季。
- `campaignseed <quick|course|long> <种子>`：固定种子复现。

当前独立正式版仓库已在 Visual Studio 2026 Debug、Release 下通过：正式版、V1与V2合计 `94/94`。三个早期选题 Demo 已从活动开发代码和默认构建中移除。V2已接入6个手动档、自动档、旧正式档只读升级、备份恢复和五种可跳过的独立结局演出；完整证据边界见 [V2_VERSION_SUMMARY.md](docs/V2_VERSION_SUMMARY.md)。

## 大型扩展 V1 试玩

V1已经加入具名角色、八项属性、八栏装备、2至8人小队以及可操控的苍林狩猎与三段战线战斗。运行 `tribe-dawn.exe` 后输入 `e` 即可开始；输入 `expanded <2至8>` 可以选择小队人数。

扩展V1暂不写入正式存档，完成任务后输入 `back` 返回主菜单。完整路线和验证结果见 `docs/V1_VERSION_SUMMARY.md`。

旧正式模式是一款 C++17 单机文字策略 MUD。玩家代表整个燧火部落，在九地点地图中经营资源、建设营地、研究技术，并与河鹿、白羽和岩牙三个部落互动；V2战役的16地点与六部落范围见上文。

## 最简单的启动方法

Windows 用户在项目根目录双击 `开始正式版.cmd`。第一次会自动用 Visual Studio 2026 编译，完成后进入主菜单；以后启动会更快。

也可以在 PowerShell 中执行：

```powershell
.\build-formal.ps1 -Configuration Release
.\run-formal.ps1 -Configuration Release -SkipBuild
```

macOS 用户先安装 Xcode Command Line Tools 和 CMake，再执行：

```bash
bash build-formal-macos.sh Release
bash run-formal-macos.sh Release
```

## 生成队员试玩包

Windows 上在项目根目录运行：

```powershell
.\package-formal.ps1
```

脚本会先完成 Release 构建和测试，再把 Windows 试玩 ZIP 与 Windows/macOS 源码 ZIP 放到桌面。

注意：只有本轮重新生成并解压复核的桌面包才能代表V2；GitHub推送、Mac实机和交换组试玩需要单独验证。

## Visual Studio 2026

1. 安装 Visual Studio 2026 的“使用 C++ 的桌面开发”，并勾选 CMake 与 Ninja。
2. 在 Visual Studio 中选择“打开本地文件夹”，打开包含 `CMakeLists.txt` 的项目根目录。
3. 选择 `tribe-dawn.exe` 作为启动项；或直接在 Visual Studio 终端运行上面的 PowerShell 命令。
4. 构建结果位于 `out/Formal-Debug` 或 `out/Formal-Release`。

## 游戏模式

- 旧正式模式：第一年春季开始，16季，完整体验约60分钟。
- 旧快速展示模式：第三年春季开始，8季，约15分钟。
- V2快速战役：8个可玩季节，用于课堂快速验证。
- V2课程战役：16季，从完整初始局开始。
- V2长期战役：32季，结局后可选择继续沙盒。
- 高级复现：主菜单输入 `seed 1` 开始指定种子的正式模式，输入 `quickseed 6` 开始指定种子的快速模式。

## 常用命令

以下存档说明适用于旧正式模式：

- `3`、`gather food`、`采集 食物`：三种写法完全等价。
- `8`、`endturn`、`结束回合`：结算当前季节。
- `9`、`help`、`帮助`：查看所有数字和双语命令。
- `save 1` / `保存 1`：保存到手动档1；手动档共有1、2、3三个。
- `load auto` / `读取 自动`：读取每季自动保存的进度。
- `back` / `返回`：自动保存并回主菜单。

V2战役内同样支持数字、中英文命令：`1/status/状态`、`2/map/地图`、`3/gather food/采集 食物`、`5/mission forest/出任务 苍林`、`8/endturn/结束回合`、`9/help/帮助`。V2存档位为1至6和自动档，使用 `save 1` / `保存 1`、`load auto` / `读取 auto`；每季结算和正常返回/退出都会自动保存。主菜单可用 `v2load 1` 或 `读取战役 1` 继续。

旧正式版存档可在主菜单输入 `migrate 1 4` 或 `升级存档 1 4`，把旧档位1转换为V2手动档位4。源档支持1、2、3和`auto`，目标必须是1至6中的空档；程序只读旧主档及其恢复候选，完整校验后另建V2副本，不会覆盖、恢复或修改任何旧档文件，也不会覆盖已有V2档。

## 课程资料

- [需求、WBS和用例](docs/REQUIREMENTS.md)
- [类图、流程图和系统设计](docs/DESIGN.md)
- [存档格式](docs/SAVE_FORMAT.md)
- [测试计划](docs/TEST_PLAN.md)
- [测试报告](docs/TEST_REPORT.md)
- [演示路线](docs/SHOWCASE_ROUTES.md)
- [小组分工模板](docs/TEAM_ASSIGNMENT.md)
- [大型扩展V1版本总结](docs/V1_VERSION_SUMMARY.md)
- [大型扩展V2版本总结与证据边界](docs/V2_VERSION_SUMMARY.md)
- [大型扩展课程报告](docs/EXPANSION_REPORT.md)
- [大型扩展四张设计图（V2当前实现与外部验证边界）](docs/EXPANSION_DIAGRAMS.md)
- [V1程序烟雾测试记录](docs/V1_SMOKE_TEST.md)
- [V2程序烟雾测试记录](docs/V2_SMOKE_TEST.md)
