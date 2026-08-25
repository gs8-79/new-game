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
