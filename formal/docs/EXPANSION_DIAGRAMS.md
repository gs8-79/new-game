# 《燧火纪：部落黎明》大型扩展版设计图

本页用于课程报告和课堂展示，不替代旧正式版设计图。四张图统一使用以下状态边界：

- **V1 已完成**：当前源码中已经存在并可从主菜单进入的“苍林狩猎”纵切，包括 `expansion_types`、`ExpansionGame`、`ExpansionState`、2至8人小队、探索/遭遇/三段战线/拾取/回营，以及对应的 `ConsoleUI` 显示。
- **V2 待实现**：六部落、16地点、长期32季、动态外交与派系、供需经济、扩展状态存档、五种独立结局和完整编年史仍是后续目标，不能作为当前完成能力。
- **存档边界**：现有 `SaveRepository` 只保存和读取旧正式模式的 `GameState`；扩展 V1 退出后不会保存 `ExpansionState`，版本2存档属于 V2。

> 2026-09-01 已从当前 Mermaid 图源重新生成 SVG 和 PNG；`.mmd`、`.svg`、`.png` 三种格式内容一致。

## 1. WBS 工作分解图

图源：[01-wbs-expanded.mmd](diagrams/expanded/01-wbs-expanded.mmd)

![扩展版WBS](diagrams/expanded/01-wbs-expanded.svg)

WBS 已拆成“V1 已完成”和“V2 待实现”两棵分支。V1覆盖基础稳定、扩展领域类型、苍林纵切和现有接入验证；长期战役、复杂外交经济、结局和完整跨平台交付证据保留在 V2。

## 2. Use Case 用例图

图源：[02-use-case-expanded.mmd](diagrams/expanded/02-use-case-expanded.mmd)

![扩展版用例图](diagrams/expanded/02-use-case-expanded.svg)

V1 的实际参与者是玩家：从主菜单进入扩展试玩，选择小队/种子，探索采集，和平贸易或进入三段战线，最后搜取并回营。其他部落 AI 和季节系统只出现在 V2 规划中。

## 3. UML 核心类图

图源：[03-class-diagram-expanded.mmd](diagrams/expanded/03-class-diagram-expanded.mmd)

![扩展版核心类图](diagrams/expanded/03-class-diagram-expanded.svg)

当前实现中，主循环分别创建 `GameEngine` 和 `ExpansionGame`；`ConsoleUI` 分别通过 `renderGame`、`renderExpansion` 显示两种模式。`ExpansionGame` 持有 `ExpansionState`，后者组合使用 `expansion_types` 中的 `Squad`、`Character`、`Inventory`、`Item` 和 `Attributes`。`SaveRepository` 仍只依赖 `GameState` 并调用 `GameEngine::validateState`；它与扩展统一状态/版本2存档的关系明确标为 V2 计划，不伪装成现有代码。

## 4. 核心游戏流程图

图源：[04-core-flow-expanded.mmd](diagrams/expanded/04-core-flow-expanded.mmd)

![扩展版核心流程图](diagrams/expanded/04-core-flow-expanded.svg)

V1 流程按当前状态机展开：营地准备→森林探索→外族遭遇→和平贸易或三段战线→免费搜取→回营结算。非法操作走“说明原因、状态不变”分支。统一长期战役、季节外交和独立结局单列为 V2，尚未接入当前扩展试玩。
