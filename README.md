# 《燧火纪：部落黎明》

C++17 单机控制台策略游戏。你将带领燧火部落探索十六地点道路地图、组织人口分工、制造装备、征召军队，并通过外交、战争、占领和季末抉择延续火种。

## 开始游玩

Windows 下双击项目根目录的 `开始正式版.cmd`。它会构建并运行测试后进入封面。首次运行需要 Visual Studio 的“使用 C++ 的桌面开发”组件、CMake 与 Ninja。

封面输入数字后按 Enter：

| 选项 | 功能 |
| --- | --- |
| 1 开始游戏 | 选择快速、正式或长期模式 |
| 2 读取存档 | 读取 A 自动档或 1 至 6 手动档 |
| 3 游戏帮助 | 查看分类指令说明 |
| 4 退出游戏 | 关闭游戏 |

三种模式均从第一季开始：快速 8 季、正式 16 季、长期 32 季；长期模式在非覆灭结局后可继续沙盒。建议使用至少 80 列、30 行并支持中文的等宽终端。

首季经营页会给出一条可直接执行的木材补给路线；首次任务结算后，该位置改为联盟、征服、繁荣三条道路的实时进展。

## 当前玩法循环

```text
地图探索采集 → 营地或前哨结算 → 劳力维护建筑 → 制造并装备物品
       ↑                                               ↓
季末事件、派系与外交 ← 战争、占领、驻军 ← 按库存征召军队
```

- 资源只能由小队在十六地点地图中装载后，回到燧火营地或有守卫的前哨结算获得；经营界面没有直接采集或侦察。
- `workforce/劳力` 查看分工，`assign/分配 <岗位> <人数>` 调整岗位。资源队可分配 2 至 6 人并提高下一季行动容量；工匠、医者、侦察、使者、营地守卫和前哨守卫只区分未配置或已配置，使用 0 或 1。劳力、前哨守卫、驻军、已组建军队和出任务小队共同占用 `人口-2`；人口损失造成超额时，必须先降低这些占用。
- 建筑与技术的材料、前置和收益可用 `buildings/建筑清单`、`technologies/技术清单` 查询。武备工坊与医者小屋均需建筑、对应劳力和职业匹配的负责人；`appoint` / `unappoint` 任免负责人，负责人不能加入永久小队。
- `inventory/仓库` 显示装备品质与实际属性；工坊负责人的耐力、感知决定普通至传说品质，军队锁定的品质战力最多额外 `+4`。
- `formarmy/组建军队` 按未锁定装备自动配发兵种；`disbandarmy/解散军队` 归还装备并释放人口。`war targets/战争目标`、`power/战力` 展示占领目标与完整战力计算；占领后要用 `garrison/驻军` 维持据点。

## 常用操作

游戏内：`1` 状态、`2` 地图、`3` 劳力、`4` 仓库、`5` 小队地图任务、`6` 外交、`7` 小队、`8` 结束季节、`9` 帮助。

在经营界面先分配劳力，再输入例如：

```text
assign wood 2
mission wood
```

地图任务只显示道路图、任务指令和现场记录。移动、采集、回营结算的一条完整路线是：

```text
move forest → gather wood → move camp → settle
```

也可在前哨建设任务中携带 6 木材和 4 石料，抵达非营地地点后输入 `build outpost`。地图内使用 `look` 查看当前位置、相邻道路、载货、可采资源和结算点；`gather/采集` 仅在对应地图地点可用。

经营界面没有直接采集、侦察或贝币命令；五种资源只能在地图任务或以物易物中流转。

## 存档

`save 1` / `保存 1` 写入手动档；覆盖前需输入 `y` 或 `是`。`load 1` / `读取 1` 读取手动档，`load auto` 读取自动档。新局、季节结算、结局和正常退出时都会自动保存。

存档位于 `saves/game`，含 `slot1.sav` 至 `slot6.sav` 及 `autosave.sav`。当前格式版本为 5；v4 及其他版本会提示需要新开局，且不会修改原文件。

## 构建与测试

Windows PowerShell：

```powershell
.\build-formal.ps1 -Configuration Debug -Clean
.\build-formal.ps1 -Configuration Release -Clean
.\run-formal.ps1 -Configuration Release -SkipBuild
```

构建产物位于 `out/Formal-Debug` 或 `out/Formal-Release`。CTest 会执行 `tribe-formal-tests`；当前测试覆盖 v5 存档、两类任务、统一人口池、负责人、装备品质、治疗休整、四类季节事件、资源贸易与玩家界面。

### 开发工具

- `clang-format`：根目录的 `.clang-format` 是唯一格式规则。先构建一次，再运行 `cmake --build out/Formal-Release --target format-check` 检查；`--target format` 会改写所有 C++ 源文件，提交前才手动执行。
- `clang-tidy`：默认关闭，避免旧代码告警影响日常构建。执行 `./build-formal.ps1 -Configuration Release -EnableClangTidy` 可在编译时运行空指针、未初始化、可疑逻辑、无效拷贝等静态检查。
- `vcpkg`：清单 `vcpkg.json` 当前不含依赖，只锁定工具基线。未来新增第三方库后，执行 `./build-formal.ps1 -Configuration Release -UseVcpkg`；该模式使用独立的 `out/Formal-Release-vcpkg`，不干扰普通构建。
- `Doxygen`：执行 `cmake --build out/Formal-Release --target docs` 生成类、状态与存档结构参考文档，入口为 `formal/docs/generated/html/index.html`；生成物不提交。

macOS 安装 CMake 与 Xcode Command Line Tools 后：

```bash
bash build-formal-macos.sh Release
bash run-formal-macos.sh Release
```

macOS 实机验证仍待完成。

## 代码结构与文档

- `formal/src/game_engine.cpp`：部落管理、劳力、外交、战争、结局和规则校验。
- `formal/src/expansion_game.cpp`：十六地点任务地图、载货、前哨与遭遇。
- `formal/src/console_ui.cpp`：控制台面板、道路图、现场记录与帮助页。
- `formal/src/save_repository.cpp`：校验、原子保存、恢复与当前格式读取。

[游戏核心目录](formal/README.md) · [页面与架构](formal/docs/DESIGN.md) · [存档格式](formal/docs/SAVE_FORMAT.md) · [验证记录](formal/docs/TEST_REPORT.md) · [试玩路线](formal/docs/SHOWCASE_ROUTES.md)
