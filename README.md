# 《燧火纪：部落黎明》

《燧火纪：部落黎明》是一款使用 C++17 编写的单机控制台文字策略 MUD。玩家作为燧火部落首领，在季节推进中经营资源、建设营地、探索地图、组织小队，并处理外交、贸易、派系和战争，最终决定部落的去向。

本仓库现在只维护正式课程作品、V1任务扩展和V2长期战役。星港、荒岛及早期部落三个选题 Demo 已从活动代码和默认构建中移除。

## 主要内容

- 三种V2战役长度：快速8季、课程16季、长期32季。
- 16个地点、六个部落、动态关系、贸易路线、联姻、联盟与战争。
- 具名人物、八项属性、装备、背包、永久小队、疲劳、经验和伤亡继承。
- 苍林任务和三段战线战斗，支持固定种子复现。
- 三个内部派系、首领更替、部落自主行动和最多200条编年史。
- 联盟、征服、繁荣、迁徙、覆灭五类结局及ASCII演出。
- 数字、中文和英文命令并存。
- 六个V2手动档、自动档、备份恢复，以及旧正式版存档只读迁移。

## 游戏模式

| 入口 | 模式 | 季节数 | 用途 |
| --- | --- | ---: | --- |
| `7` / `campaign quick` / `战役 快速` | 快速战役 | 8 | 课堂演示和短流程验证 |
| `8` / `campaign course` / `战役 课程` | 课程战役 | 16 | 标准课程体验 |
| `9` / `campaign long` / `战役 长期` | 长期战役 | 32 | 完整经营与结局后沙盒 |
| `e` / `expanded` | V1苍林任务 | 单次任务 | 展示人物、装备、小队和战术战斗 |

使用 `campaignseed <quick|course|long> <种子>` 可以复现指定战役。

## Windows构建与运行

环境要求：

- Windows 10或更高版本。
- Visual Studio 2026 Community。
- 安装“使用C++的桌面开发”、CMake与Ninja组件。

在项目根目录执行：

```powershell
.\build-formal.ps1 -Configuration Debug
.\build-formal.ps1 -Configuration Release
```

脚本会完成配置、编译和自动测试。运行Release版本：

```powershell
.\run-formal.ps1 -Configuration Release -SkipBuild
```

也可以直接双击 `开始正式版.cmd`。

## macOS构建

安装Xcode Command Line Tools与CMake后执行：

```bash
bash build-formal-macos.sh Release
bash run-formal-macos.sh Release
```

macOS脚本和源码已经提供，但仍需要在真实Mac设备上完成验证。

## 常用命令

进入战役后可以使用数字、中文或英文命令：

```text
1 / status / 状态
2 / map / 地图
3 / gather food / 采集 食物
5 / mission forest / 出任务 苍林
8 / endturn / 结束回合
9 / help / 帮助
save 1 / 保存 1
load auto / 读取 自动
back / 返回
quit / 退出
```

## 存档与迁移

V2提供1至6号手动档和一个自动档。每季结算以及正常返回或退出时都会自动保存，主菜单可以使用 `v2load 1` 继续游戏。

旧正式版存档可以只读转换为V2副本：

```text
migrate 1 4
升级存档 1 4
```

转换会先校验旧主档及恢复候选，再创建新的V2目标档；不会覆盖旧存档，也不会覆盖已有V2档。

## 项目结构

```text
.
├─ formal/
│  ├─ include/tribe/     模块接口和领域类型
│  ├─ src/               游戏、战役、存档、迁移和界面实现
│  ├─ tests/             正式版、V1和V2测试
│  ├─ docs/              需求、设计、测试与课程报告
│  └─ package/           Windows试玩包入口文件
├─ tests/                轻量测试框架和统一入口
├─ CMakeLists.txt
├─ build-formal.ps1
├─ run-formal.ps1
└─ package-formal.ps1
```

核心运行关系：

```text
main / ConsoleUI
├─ GameEngine               旧正式模式
├─ ExpansionGame            V1苍林任务
└─ CampaignGame             V2长期战役
   ├─ BattleSystem
   ├─ CampaignSaveRepository
   ├─ CampaignMigration
   └─ EndingPresentation
```

游戏操作采用“复制候选状态 → 完整校验 → 一次提交”。非法命令、资源不足、损坏存档或迁移失败不会留下半次状态修改。

## 当前验证状态

迁移到独立正式版仓库后，已于2026-09-04重新执行：

| 验证项 | 结果 |
| --- | --- |
| Visual Studio 2026 Debug构建 | 通过 |
| Visual Studio 2026 Release构建 | 通过 |
| Debug自动测试 | `94/94`通过 |
| Release自动测试 | `94/94`通过 |
| macOS实机构建 | 待验证 |
| 交换小组试玩 | 待验证 |
| 课堂验收 | 待验证 |

上述结果代表代码存在和本机测试通过，不代表Mac兼容性、外部试玩或课堂验收已经完成。

## 课程与设计资料

- [正式版玩法说明](formal/README.md)
- [需求、WBS与用例](formal/docs/REQUIREMENTS.md)
- [系统设计与UML](formal/docs/DESIGN.md)
- [大型扩展课程报告](formal/docs/EXPANSION_REPORT.md)
- [存档格式](formal/docs/SAVE_FORMAT.md)
- [测试计划](formal/docs/TEST_PLAN.md)
- [测试报告](formal/docs/TEST_REPORT.md)
- [演示路线](formal/docs/SHOWCASE_ROUTES.md)
- [版本与证据边界](formal/docs/V2_VERSION_SUMMARY.md)

## Git协作

`main` 是受保护分支：禁止强制推送和删除，修改应通过功能分支与Pull Request合并。提交前请至少运行一次Debug或Release完整构建和测试。
