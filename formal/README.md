# 《燧火纪：部落黎明》正式版

## 大型扩展 V1 试玩

V1已经加入具名角色、八项属性、八栏装备、2至8人小队以及可操控的苍林狩猎与三段战线战斗。运行 `tribe-dawn.exe` 后输入 `e` 即可开始；输入 `expanded <2至8>` 可以选择小队人数。

扩展V1暂不写入正式存档，完成任务后输入 `back` 返回主菜单。完整路线和验证结果见 `docs/V1_VERSION_SUMMARY.md`。

这是一款 C++17 单机文字策略 MUD。玩家代表整个燧火部落，在九地点地图中经营资源、建设营地、研究技术，并与河鹿、白羽和岩牙三个部落互动。

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

注意：现有桌面包属于此前正式版；大型扩展V1工作区预览尚未重新生成和验证ZIP，完成V1提交后需要重新执行打包脚本。

## Visual Studio 2026

1. 安装 Visual Studio 2026 的“使用 C++ 的桌面开发”，并勾选 CMake 与 Ninja。
2. 在 Visual Studio 中选择“打开本地文件夹”，打开包含 `CMakeLists.txt` 的项目根目录。
3. 选择 `tribe-dawn.exe` 作为启动项；或直接在 Visual Studio 终端运行上面的 PowerShell 命令。
4. 正式版单独构建在 `out/Formal-Debug` 或 `out/Formal-Release`，不会覆盖三个旧 Demo。

## 游戏模式

- 正式模式：第一年春季开始，16季，完整体验约60分钟。
- 快速展示模式：第三年春季开始，8季，约15分钟。
- 高级复现：主菜单输入 `seed 1` 开始指定种子的正式模式，输入 `quickseed 6` 开始指定种子的快速模式。

## 常用命令

- `3`、`gather food`、`采集 食物`：三种写法完全等价。
- `8`、`endturn`、`结束回合`：结算当前季节。
- `9`、`help`、`帮助`：查看所有数字和双语命令。
- `save 1` / `保存 1`：保存到手动档1；手动档共有1、2、3三个。
- `load auto` / `读取 自动`：读取每季自动保存的进度。
- `back` / `返回`：自动保存并回主菜单。

## 课程资料

- [需求、WBS和用例](docs/REQUIREMENTS.md)
- [类图、流程图和系统设计](docs/DESIGN.md)
- [存档格式](docs/SAVE_FORMAT.md)
- [测试计划](docs/TEST_PLAN.md)
- [测试报告](docs/TEST_REPORT.md)
- [演示路线](docs/SHOWCASE_ROUTES.md)
- [小组分工模板](docs/TEAM_ASSIGNMENT.md)
- [大型扩展V1版本总结](docs/V1_VERSION_SUMMARY.md)
- [大型扩展课程报告](docs/EXPANSION_REPORT.md)
- [大型扩展四张设计图（区分V1已实现与V2蓝图）](docs/EXPANSION_DIAGRAMS.md)
- [V1程序烟雾测试记录](docs/V1_SMOKE_TEST.md)
