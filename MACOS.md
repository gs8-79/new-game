# macOS 构建与试玩

这个源码包使用标准 C++17 和 CMake，不依赖第三方 C++ 库。Windows 控制台颜色代码使用条件编译，在 macOS 上会自动退化为普通文本输出，不影响玩法和测试。

## 1. 安装编译工具

打开“终端”，运行：

```bash
xcode-select --install
```

如果电脑没有 CMake，可使用 Homebrew 安装：

```bash
brew install cmake
```

已经安装 CMake 的电脑不需要重复安装。

## 2. 编译并运行测试

完整解压源码 ZIP，然后在终端进入解压后的目录：

```bash
cd "/你的路径/MUD选题试玩Demo-源码"
bash build-macos.sh Release
```

脚本会编译 `mud-demos`，然后运行全部自动测试。

## 3. 开始试玩

```bash
bash run-macos.sh Release
```

也可以在完成构建后直接运行：

```bash
./out/macos-Release/mud-demos
```

进入游戏后输入 `help` 或 `帮助` 查看命令。存档会写入源码目录下的 `saves/`。

## 常见问题

- `xcode-select: error`：先运行 `xcode-select --install` 并完成安装。
- `cmake: command not found`：安装 CMake，或确认 CMake 已加入 `PATH`。
- 脚本没有执行权限：直接使用 `bash build-macos.sh Release`，不需要修改权限。
- 中文显示异常：使用 macOS 自带 Terminal 或 iTerm2，并确保终端使用 UTF-8。
