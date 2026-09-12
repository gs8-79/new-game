# 《燧火纪：部落黎明》

C++17 单机控制台策略游戏。领导燧火部落经营资源、组织小队沿道路探索16处地点并在营地或前哨结算，处理六部落外交与战争，在季节结束时决定部落的去向。

## 开始游玩

Windows 双击项目根目录的 `开始正式版.cmd`，会先构建和测试，再进入封面。首次启动需安装 Visual Studio 的 C++ 桌面开发、CMake 和 Ninja 组件。不想装开发环境的话，安装 Docker 后双击 `开始Docker版.cmd` 即可，见下文「Docker 运行」一节。

封面采用居中篝火封面，输入数字后按 Enter：

| 封面选项 | 功能 |
| --- | --- |
| 1 开始游戏 | 再选择1快速、2正式、3长期 |
| 2 读取存档 | A自动档，1至6手动档，B返回 |
| 3 游戏帮助 | 1至6查看分类，B或Enter逐级返回 |
| 4 退出游戏 | 关闭游戏 |

快速游戏从第9季到第16季，共8个可玩季节；正式游戏从第1季到第16季；长期游戏从第1季到第32季，非覆灭结局后可继续沙盒。
推荐至少80列、30行的终端窗口；更窄时文本会按显示列数换行。需要使用支持中文的等宽字体。

## Docker 运行（免装开发环境）

任何装有 Docker 的电脑（Windows / macOS / Linux）都能直接运行，无需安装 Visual Studio、CMake 或 C++ 编译器。镜像构建时会在干净的 Debian 环境里编译并跑完全部自动测试，换机器结果一致。这里需要的是 Docker Desktop 或 Docker Engine（命令为 `docker`），不是 Docker Sandboxes（命令为 `sbx`）。

Windows：安装并启动 Docker Desktop 后，双击根目录的 `开始Docker版.cmd`（首次会自动构建镜像，需要联网），或手动执行：

```powershell
.\docker-build.ps1
.\docker-run.ps1
```

macOS / Linux：

```bash
bash docker-build.sh
bash docker-run.sh
```

也可以使用 Compose：`docker compose run --rm tribe-dawn`。

双击 `开始Docker版.cmd` 时若 Docker Desktop 尚未运行，脚本会自动启动它并等待引擎就绪。国内网络拉取基础镜像若超时或过慢，可在 Docker Desktop 的 Settings → Docker Engine 中加入 `registry-mirrors` 镜像加速地址后重启引擎。

存档通过目录挂载与宿主机互通：容器内 `/game/saves` 对应项目根目录的 `saves/`，删除容器不丢存档；本机构建版与 Docker 版共用同一套存档文件。终端窗口同样建议至少 80 列、30 行。

## 操作与存档

游戏内：`1`状态、`2`地图、`5`小队地图任务、`6`外交、`7`小队、`8`结束季节、`9`帮助。`3`、`4`、经营界面的`gather`和`scout`已移除；进入小队地图后使用`move`、`gather`、`build outpost`、`settle`，并可在部落接触点外交、在岩牙要塞进行小队遭遇。
完整参数、资源名称和注意事项在游戏帮助分类中。

`save 1` / `保存 1` 写入手动档，覆盖前需输入 `y` 或 `是`；`load 1` / `读取 1` 读取手动档，`load auto` 读取自动档。
新局、季节结算、结局以及正常返回或退出时自动保存。`back` / `返回主菜单` 返回封面，`quit` / `退出` 保存后退出。失败时游戏继续等待操作；`forcequit` / `强制退出` 放弃未保存进度。

启动脚本以项目目录运行，存档在 `saves/game`，包含 `slot1.sav` 至 `slot6.sav` 和 `autosave.sav`。存档页显示文件本地修改时间、模式、季节、首领和资源摘要，支持备份及中断保存恢复。直接运行可执行文件时，存档相对于当前工作目录。

需要复现时在封面输入 `seed quick 7`、`seed standard 123` 或 `seed long 123`。文件格式与目录已统一，不提供历史格式迁移。

## 构建和测试

Windows PowerShell：

```powershell
.\build-formal.ps1 -Configuration Debug -Clean
.\build-formal.ps1 -Configuration Release -Clean
.\run-formal.ps1 -Configuration Release -SkipBuild
```

`-Clean` 会重新编译生成物。常规开发可以省略。构建结果在 `out/Formal-Debug` 或 `out/Formal-Release`，自动测试由 CTest 调用 `tribe-formal-tests` 执行。

macOS 安装 CMake 和 Xcode Command Line Tools 后：

```bash
bash build-formal-macos.sh Release
bash run-formal-macos.sh Release
```

macOS 实机验证仍待完成。

## 代码结构

- `formal/src/main.cpp`：终端初始化与启动。
- `application`：注入输入输出流的页面导航、保存与游戏循环。
- `console_ui`：篝火主题、中文宽度、折行、帮助和状态面板。
- `game_engine`：唯一主游戏的状态与规则。
- `expansion_game/expansion_types`：主游戏使用的16地点地图任务、人物、装备、小队、前哨和背包。
- `save_repository`：严格文件校验、只读摘要、原子保存与恢复。
- `ending_presentation`：五种结局的演出与结算。

## 交付与文档

`package-formal.ps1` 默认构建测试后生成 Windows 试玩包和源码包。可用 `-Destination <目录>` 指定输出位置。

`.github/workflows/docker-ci.yml` 在每次推送时于 GitHub Actions 的 Linux 环境自动编译、运行全部测试，并构建 Docker 镜像、在容器内启动游戏验证可运行。

各平台（Windows / Linux / macOS，含 Intel 与 Apple Silicon）预编译二进制见 [GitHub Releases](https://github.com/gs8-79/new-game/releases)，由 `.github/workflows/release.yml` 在打 tag（`v*`）时自动构建并发布。

[源码目录](formal/README.md) · [页面与架构](formal/docs/DESIGN.md) · [存档格式](formal/docs/SAVE_FORMAT.md) · [验证记录](formal/docs/TEST_REPORT.md) · [试玩路线](formal/docs/SHOWCASE_ROUTES.md)

本轮改动在 `qianduan1` 分支开发，后续通过 Pull Request 合并。
