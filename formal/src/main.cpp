#include "tribe/console_ui.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"

#include <chrono>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Words {
    std::string verb;
    std::vector<std::string> args;
};

std::string asciiLower(std::string text) {
    for (char& character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 128U) character = static_cast<char>(std::tolower(byte));
    }
    return text;
}

Words words(const std::string& input) {
    std::istringstream stream(input);
    Words parsed;
    stream >> parsed.verb;
    parsed.verb = asciiLower(parsed.verb);
    std::string argument;
    while (stream >> argument) parsed.args.push_back(asciiLower(std::move(argument)));
    return parsed;
}

bool verbIs(const Words& command, const std::initializer_list<std::string_view> aliases) {
    for (const auto alias : aliases) {
        if (command.verb == alias) return true;
    }
    return false;
}

std::uint32_t freshSeed() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint32_t>(static_cast<unsigned long long>(now) & 0xFFFFFFFFULL);
}

bool parseSeed(const std::string& text, std::uint32_t& seed) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seed);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

void waitForEnter(tribe::ConsoleUI& ui) {
    if (!ui.interactive()) return;
    std::string ignored;
    std::getline(std::cin, ignored);
}

bool runGame(tribe::GameEngine& engine, const tribe::SaveRepository& saves, tribe::ConsoleUI& ui,
    const bool saveInitial) {
    std::string message = engine.openingMessage();
    if (saveInitial) {
        std::string saveError;
        if (!saves.save(engine.state(), tribe::SaveSlot::Autosave, saveError)) {
            message += "\n自动保存失败：" + saveError;
        }
    }

    std::string line;
    for (;;) {
        ui.renderGame(engine, message);
        if (!std::getline(std::cin, line)) return false;
        const Words command = words(line);
        if (command.verb.empty()) {
            message = "请输入命令；第一次游玩可输入9或帮助。";
            continue;
        }
        if ((command.verb == "9" || verbIs(command, {"help", "帮助"})) && command.args.empty()) {
            ui.showStandalone("完整帮助", engine.helpText());
            waitForEnter(ui);
            message = "帮助页已关闭。";
            continue;
        }
        if (verbIs(command, {"save", "保存"})) {
            if (command.args.size() != 1U) {
                message = "用法：save 1|2|3 / 保存 1|2|3";
                continue;
            }
            const auto slot = tribe::SaveRepository::parseSlot(command.args.front());
            if (!slot || *slot == tribe::SaveSlot::Autosave) {
                message = "手动保存只能选择1、2或3；自动档由程序维护。";
                continue;
            }
            std::string error;
            message = saves.save(engine.state(), *slot, error)
                ? tribe::SaveRepository::slotName(*slot) + "保存成功。"
                : "保存失败：" + error;
            continue;
        }
        if (verbIs(command, {"load", "读取"})) {
            if (command.args.size() != 1U) {
                message = "用法：load 1|2|3|auto / 读取 1|2|3|自动";
                continue;
            }
            const auto slot = tribe::SaveRepository::parseSlot(command.args.front());
            if (!slot) {
                message = "存档位必须是1、2、3或auto。";
                continue;
            }
            tribe::GameState candidate;
            std::string error;
            if (!saves.load(*slot, candidate, error) || !engine.replaceState(candidate, error)) {
                message = "读取失败，当前游戏未改变：" + error;
            } else {
                message = tribe::SaveRepository::slotName(*slot) + "读取成功。";
            }
            continue;
        }
        if (verbIs(command, {"back", "返回"}) && command.args.empty()) {
            std::string error;
            message = saves.save(engine.state(), tribe::SaveSlot::Autosave, error)
                ? "已自动保存并返回主菜单。" : "返回主菜单；自动保存失败：" + error;
            return true;
        }
        if (verbIs(command, {"quit", "退出"}) && command.args.empty()) {
            std::string error;
            saves.save(engine.state(), tribe::SaveSlot::Autosave, error);
            return false;
        }

        const tribe::ActionResult result = engine.execute(line);
        message = result.recognized ? result.message : "无法识别该命令。输入9、help或帮助查看完整操作。";
        if (result.seasonAdvanced || (result.stateChanged && engine.state().phase == tribe::Phase::Finished)) {
            std::string error;
            if (!saves.save(engine.state(), tribe::SaveSlot::Autosave, error)) {
                message += "\n自动保存失败：" + error;
            }
        }
        if (engine.state().phase == tribe::Phase::Finished) {
            ui.renderGame(engine, message);
            std::cout << "\n本局结束，按 Enter 返回主菜单……";
            waitForEnter(ui);
            return true;
        }
    }
}

} // namespace

int main() {
    const bool interactive = tribe::ConsoleUI::standardStreamsAreInteractive();
    const bool ansi = interactive && tribe::ConsoleUI::initializeTerminal();
    tribe::ConsoleUI ui(std::cout, interactive, ansi);
    const tribe::SaveRepository saves(std::filesystem::current_path() / "saves" / "formal");

    std::string menuMessage;
    std::string line;
    for (;;) {
        ui.renderMainMenu(menuMessage);
        if (!std::getline(std::cin, line)) break;
        const Words command = words(line);
        if (verbIs(command, {"q", "quit", "退出"})) break;
        if (verbIs(command, {"h", "help", "帮助"})) {
            tribe::GameEngine example({tribe::GameMode::Standard, 1U});
            ui.showStandalone("新手帮助", example.helpText());
            waitForEnter(ui);
            menuMessage.clear();
            continue;
        }

        if (command.args.empty() && (command.verb == "1" || command.verb == "2")) {
            const tribe::GameMode mode = command.verb == "1" ? tribe::GameMode::Standard : tribe::GameMode::Quick;
            tribe::GameEngine engine({mode, freshSeed()});
            if (!runGame(engine, saves, ui, true)) break;
            menuMessage = "已经返回主菜单；最近游戏保存在自动档。";
            continue;
        }
        if ((command.verb == "seed" || command.verb == "quickseed") && command.args.size() == 1U) {
            std::uint32_t seed = 0;
            if (!parseSeed(command.args.front(), seed)) {
                menuMessage = "种子必须是0到4294967295之间的整数。";
                continue;
            }
            const tribe::GameMode mode = command.verb == "seed" ? tribe::GameMode::Standard : tribe::GameMode::Quick;
            tribe::GameEngine engine({mode, seed});
            if (!runGame(engine, saves, ui, true)) break;
            menuMessage = "已经返回主菜单；最近游戏保存在自动档。";
            continue;
        }
        if (command.args.empty() && (command.verb == "3" || command.verb == "4"
                || command.verb == "5" || command.verb == "6")) {
            const tribe::SaveSlot slot = command.verb == "3" ? tribe::SaveSlot::Slot1
                : command.verb == "4" ? tribe::SaveSlot::Slot2
                : command.verb == "5" ? tribe::SaveSlot::Slot3 : tribe::SaveSlot::Autosave;
            tribe::GameState loaded;
            std::string error;
            if (!saves.load(slot, loaded, error)) {
                menuMessage = "读取失败：" + error;
                continue;
            }
            tribe::GameEngine engine({loaded.mode, loaded.seed});
            if (!engine.replaceState(loaded, error)) {
                menuMessage = "读取失败：" + error;
                continue;
            }
            if (!runGame(engine, saves, ui, false)) break;
            menuMessage = "已经返回主菜单；最近游戏保存在自动档。";
            continue;
        }
        menuMessage = "无效选择，请输入1至6、h或q。";
    }

    std::cout << "\n燧火未熄，感谢游玩。\n";
    return 0;
}
