# C++ MUD 选题试玩集

这个目录包含三个同规模、可重复试玩的控制台 Demo，用于在正式课程设计前比较题材：

1. 《星港危机：最后的维修员》
2. 《荒岛求生7日》
3. 《燧火纪：部落黎明》

## 构建与运行

环境要求：Windows、Visual Studio 2026 Community（含 C++、CMake 与 Ninja 组件）。构建脚本自动加载 MSVC 14.51 开发环境。

```powershell
.\build.ps1
.\run.ps1 -SkipBuild
```

程序使用中文叙事，并同时接受中英文命令。每款 Demo 有独立的文本存档，保存在 `saves/`。

完成三款官方路线和自由试玩后，请使用 [EVALUATION.md](EVALUATION.md) 记录评分。

## 官方试玩路线

在场景内可随时输入 `help` 或 `帮助` 查看完整命令；输入 `save/保存`、`load/读取`、`back/返回` 或 `quit/退出` 使用公共功能。

### 1. 星港危机

```text
go north
go west
attack drone
attack drone
search
use medkit
go east
go east
repair reactor
```

这条路线展示房间移动、确定性战斗、搜索、物品使用、氧气压力和任务结局。

### 2. 荒岛求生7日

```text
go jungle
gather vine
gather vine
gather wood
use coconut
craft rope
gather wood
gather wood
go spring
collect water
use water
go jungle
go cliff
build signal
```

行动点归零会自动进入下一天。这条路线展示资源采集、制作、地点门槛、生存数值和第七天救援。

### 3. 燧火纪：部落黎明

```text
gather food
diplomacy riverdeer
endturn
diplomacy riverdeer
gather food
endturn
gather food
gather wood
endturn
gather food
gather food
endturn
```

这条路线进入“联盟之主”结局。部落 Demo 不会自动换季，需要输入 `endturn` 或 `下一季` 结算。

## Demo 边界

这三个程序是用于选择题材的可玩纵切原型，不是最终课程作品。它们故意取消随机数并控制地图、资源和命令规模，以便公平比较和重复测试；选题确定后仍需重新完成正式 OOA/OOD、UML 和完整系统设计。
