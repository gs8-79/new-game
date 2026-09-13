#include "tribe/application.hpp"
#include "tribe/console_ui.hpp"

#include <filesystem>
#include <iostream>

/// 用途：初始化终端并启动正式版应用。输入：标准流与默认 saves 路径；输出：应用退出码。
/// 状态影响：仅可能写入当前工作目录下的 saves；失败：终端初始化失败时仍以非 ANSI 方式运行；不变量：不直接修改游戏规则。
int main() {
    const bool interactive = tribe::ConsoleUI::standardStreamsAreInteractive();
    const bool ansiEnabled = interactive && tribe::ConsoleUI::initializeTerminal();
    return tribe::runApplication(std::cin, std::cout, std::filesystem::current_path() / "saves" / "game", interactive,
                                 ansiEnabled);
}
