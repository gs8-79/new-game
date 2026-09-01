# 《燧火纪：部落黎明》大型扩展版设计图

本页用于课程报告和课堂展示，内容对应大型扩展版，不替代旧正式版设计图。

## 1. WBS 工作分解图

![扩展版WBS](diagrams/expanded/01-wbs-expanded.svg)

WBS 将开发工作分为基础稳定、苍林狩猎纵切、人物小队与世界、战争与动态外交、贸易派系与稳定、界面结局与交付六部分。

## 2. Use Case 用例图

![扩展版用例图](diagrams/expanded/02-use-case-expanded.svg)

主要参与者是玩家；其他部落 AI 和季节系统作为辅助参与者，共同触发自主外交、贸易、派系变化和结局。

## 3. UML 核心类图

![扩展版核心类图](diagrams/expanded/03-class-diagram-expanded.svg)

`CampaignState` 保存完整局面；角色、小队、军队、任务、战斗、外交、贸易、结局演出、界面和存档模块各自承担独立职责。

## 4. 核心游戏流程图

![扩展版核心流程图](diagrams/expanded/04-core-flow-expanded.svg)

流程覆盖开局、部落管理、可操控小队任务、可操控战斗、季节结算、动态外交、派系变化和独立结局结算。
