#include "core.hpp"
#include "scenarios.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main() {
    mud::enableUtf8Console();
    mud::writeStyled(std::cout, mud::ConsoleStyle::Title, "C++ MUD 选题试玩集\n");

    std::string line;
    for (;;) {
        std::cout
            << "\n请选择 Demo：\n"
            << "  1. 《星港危机：最后的维修员》\n"
            << "  2. 《荒岛求生7日》\n"
            << "  3. 《燧火纪：部落黎明》\n"
            << "  q. 退出\n"
            << "> ";

        if (!std::getline(std::cin, line)) {
            std::cout << "\n输入结束。\n";
            break;
        }

        const auto command = mud::parseCommand(line);
        if (mud::commandIs(command, {"q", "quit", "退出"})) {
            break;
        }

        auto scenario = mud::makeScenarioForMenu(command);
        if (!scenario) {
            mud::writeStyled(std::cout, mud::ConsoleStyle::Warning, "无效选择，请输入 1、2、3 或中文名称。\n");
            continue;
        }

        const auto savePath = std::filesystem::current_path() / "saves" / (scenario->id() + ".sav");
        const auto result = mud::runScenario(*scenario, std::cin, std::cout, savePath);
        if (result == mud::RunnerExit::QuitApplication) {
            break;
        }
    }

    std::cout << "感谢试玩。\n";
    return 0;
}
