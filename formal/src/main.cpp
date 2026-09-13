#include "tribe/application.hpp"
#include "tribe/console_ui.hpp"

#include <filesystem>
#include <iostream>

int main() {
    const bool interactive = tribe::ConsoleUI::standardStreamsAreInteractive();
    const bool ansiEnabled = interactive && tribe::ConsoleUI::initializeTerminal();
    return tribe::runApplication(std::cin, std::cout, std::filesystem::current_path() / "saves" / "game", interactive,
                                 ansiEnabled);
}
