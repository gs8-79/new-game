# 《燧火纪：部落黎明》大型扩展版设计图

本页用于课程报告和课堂展示，不替代旧正式版设计图。四张图统一使用以下状态边界：

- **V1 已完成**：`expansion_types`、`ExpansionGame`、`ExpansionState` 和“苍林狩猎”纵切已经存在并可从主菜单试玩。
- **V2 已实现范围通过本机验证**：`CampaignGame`、`CampaignSaveRepository`、`CampaignMigration`、`EndingPresentation`、主菜单和 `ConsoleUI` 已接入，覆盖8/16/32季、六部落、16地点、长期人物与背包、外交贸易、派系AI、战争、版本2兼容存档、五结局和编年史。
- **自动测试边界**：正式版独立仓库已于2026-09-04在本机 Visual Studio 2026 Debug、Release 下分别通过 CTest `1/1`，测试程序内部为正式版/V1/V2 `94/94`；V2另有程序烟测。三个早期选题 Demo 已退出活动仓库，其历史测试记录仍保留在测试报告中。
- **版本发布已完成**：GitHub上的V1、V2提交与两个版本标签已经推送并核对远端哈希。
- **外部证据未完成**：Mac实机、交换小组试玩和课堂验收仍未验证，图中以灰色交付项表示。
- **本轮兼容修补已完成**：旧正式档只读转换为V2副本、结局动画任意键跳过、长期人物/装备/背包回写、首领性格与外部派系驱动AI、四类结局纯玩家命令路线均已接入。

> 四个 `.mmd` 已按V2最终源码刷新；SVG/PNG由同一图源重新导出后使用。

## 1. WBS 工作分解图

图源：[01-wbs-expanded.mmd](diagrams/expanded/01-wbs-expanded.mmd)

![扩展版WBS](diagrams/expanded/01-wbs-expanded.svg)

WBS 把“代码与基础设施完成”“本机验证完成”“仍需队员完成的外部证据”分开，防止把Mac或交换组验证提前写成已完成。

## 2. Use Case 用例图

图源：[02-use-case-expanded.mmd](diagrams/expanded/02-use-case-expanded.mmd)

![扩展版用例图](diagrams/expanded/02-use-case-expanded.svg)

玩家用例覆盖部落经营、小队任务、外交贸易、战争、派系、存档、旧档升级与结局；其他部落AI、季节系统和新旧存储模块作为外部参与者单列。

## 3. UML 核心类图

图源：[03-class-diagram-expanded.mmd](diagrams/expanded/03-class-diagram-expanded.mmd)

![扩展版核心类图](diagrams/expanded/03-class-diagram-expanded.svg)

类图对应V2源码：`MainModule`表示`main.cpp`中的主循环与自由函数，不是额外C++类；`CampaignGame`协调规则，`CampaignSaveRepository`只负责版本2持久化，`CampaignMigration`只读升级旧档，`EndingPresentation`只读显示并接受跳过选项，`ConsoleUI`负责主页面；苍林任务继续复用独立的 `ExpansionGame` / `ExpansionState`。

## 4. 核心游戏流程图

图源：[04-core-flow-expanded.mmd](diagrams/expanded/04-core-flow-expanded.mmd)

![扩展版核心流程图](diagrams/expanded/04-core-flow-expanded.svg)

流程图从主菜单开始，串联旧档只读升级、管理、小队任务、长期人物回写、战争、性格/派系驱动的季节结算、可跳过结局演出、版本2存档和长期沙盒；所有失败分支都回到原阶段且不修改状态。
