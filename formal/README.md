# 游戏核心目录

本目录提供游戏源码、头文件、测试和打包入口。

启动、三种模式、存档和构建方法见项目根目录的 [README](../README.md)。
当前架构见 [DESIGN](docs/DESIGN.md)，验证证据见 [TEST_REPORT](docs/TEST_REPORT.md)。

苍林任务通过 `GameEngine` 进入；`ExpansionGame` 是内部任务组件，没有独立的玩家入口。
