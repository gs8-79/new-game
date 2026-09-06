#include "tribe/application.hpp"

#include "tribe/console_ui.hpp"
#include "tribe/ending_presentation.hpp"
#include "tribe/save_repository.hpp"

#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tribe {
namespace {

struct Words {
    std::string verb;
    std::vector<std::string> args;
};

std::string asciiLower(std::string text) {
    for (char &character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 128U)
            character = static_cast<char>(std::tolower(byte));
    }
    return text;
}

Words words(const std::string &input) {
    std::istringstream stream(input);
    Words parsed;
    stream >> parsed.verb;
    parsed.verb = asciiLower(parsed.verb);
    std::string argument;
    while (stream >> argument)
        parsed.args.push_back(asciiLower(std::move(argument)));
    return parsed;
}

bool verbIs(const Words &command, const std::initializer_list<std::string_view> aliases) {
    for (const std::string_view alias : aliases) {
        if (command.verb == alias)
            return true;
    }
    return false;
}

std::uint32_t freshSeed() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint32_t>(static_cast<unsigned long long>(now) & 0xFFFFFFFFULL);
}

bool parseSeed(const std::string &text, std::uint32_t &seed) {
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seed);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::optional<GameMode> parseMode(const std::string_view text) {
    if (text == "1" || text == "quick" || text == "fast" || text == "快速")
        return GameMode::Quick;
    if (text == "2" || text == "standard" || text == "formal" || text == "正式")
        return GameMode::Standard;
    if (text == "3" || text == "long" || text == "长期")
        return GameMode::Long;
    return std::nullopt;
}

void waitForEnter(std::istream &input) {
    std::string ignored;
    std::getline(input, ignored);
}

void showHelp(ConsoleUI &ui, std::istream &input) {
    int topic = 0;
    std::string line;
    for (;;) {
        ui.renderHelpPage(topic);
        if (!std::getline(input, line))
            return;
        const Words command = words(line);
        if (command.args.empty() && (command.verb.empty() || verbIs(command, {"b", "back", "返回"}))) {
            if (topic == 0)
                return;
            topic = 0;
        } else if (command.args.empty() && command.verb.size() == 1U && command.verb[0] >= '1' &&
                   command.verb[0] <= '6') {
            topic = command.verb[0] - '0';
        }
    }
}

void playEnding(const GameEngine &game, ConsoleUI &ui, std::istream &input, std::ostream &output) {
    EndingPresentationOptions options;
    options.animated = ui.interactive();
    options.ansiEnabled = ui.ansiEnabled();
    options.clearBetweenFrames = ui.interactive() && ui.ansiEnabled();
    options.frameDelay = std::chrono::milliseconds{260};
    ui.clear();
    EndingPresentation::play(game.endingSummary(), output, options);
    output << "\n\n按 Enter 返回结算菜单。";
    output.flush();
    waitForEnter(input);
}

bool runGame(GameEngine &game, const SaveRepository &saves, ConsoleUI &ui, std::istream &input,
             std::ostream &output, const bool saveInitial, std::string &exitMessage) {
    exitMessage.clear();
    std::string message =
        GameEngine::modeName(game.state().mode) + "已经开始。输入1查看状态，输入9或帮助查看完整命令。";
    if (saveInitial) {
        std::string error;
        if (!saves.save(game.state(), SaveSlot::Autosave, error))
            message += "\n自动保存失败：" + error;
    }

    std::string line;
    for (;;) {
        ui.renderGame(game, message);
        if (!std::getline(input, line))
            return false;
        const Words command = words(line);
        if (command.verb.empty()) {
            message = "请输入命令；第一次游玩可输入9或帮助。";
            continue;
        }
        if ((command.verb == "9" || verbIs(command, {"help", "帮助"})) && command.args.empty()) {
            showHelp(ui, input);
            message = "帮助页已关闭。";
            continue;
        }
        if (verbIs(command, {"back", "返回", "返回主菜单"}) && command.args.empty()) {
            std::string error;
            if (!saves.save(game.state(), SaveSlot::Autosave, error)) {
                message = "自动保存失败，仍留在游戏中：" + error +
                          " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            exitMessage = "已返回封面，当前进度保存在自动档。";
            return true;
        }
        if (verbIs(command, {"quit", "exit", "退出"}) && command.args.empty()) {
            std::string error;
            if (!saves.save(game.state(), SaveSlot::Autosave, error)) {
                message = "退出前保存失败，游戏仍保留：" + error +
                          " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            return false;
        }
        if (verbIs(command, {"forcequit", "强制退出"}) && command.args.empty())
            return false;
        if (verbIs(command, {"replay", "重新播放"}) && command.args.empty()) {
            if (game.state().phase != GamePhase::Finished)
                message = "只有结局确定后才能重新播放演出。";
            else {
                playEnding(game, ui, input, output);
                message = "结局演出播放完毕。";
            }
            continue;
        }
        if (verbIs(command, {"characters", "人物", "查看人物"}) && command.args.empty()) {
            ui.showStandalone("人物与小队结算", game.squadText());
            waitForEnter(input);
            message = "已查看人物与小队结算。";
            continue;
        }
        if (verbIs(command, {"save", "保存"})) {
            if (command.args.size() != 1U) {
                message = "用法：save 1（可选1至6）。";
                continue;
            }
            const auto slot = SaveRepository::parseSlot(command.args.front());
            if (!slot || *slot == SaveSlot::Autosave) {
                message = "手动保存只能选择1至6号档位。";
                continue;
            }
            bool occupied = false;
            for (const auto &summary : saves.inspect()) {
                if (summary.slot == *slot)
                    occupied = summary.status != SaveStatus::Empty;
            }
            if (occupied) {
                ui.prompt("覆盖" + SaveRepository::slotName(*slot) + "？输入 y/是 确认，其他输入取消 > ");
                std::string answer;
                if (!std::getline(input, answer))
                    return false;
                const Words confirmation = words(answer);
                if (!confirmation.args.empty() || !verbIs(confirmation, {"y", "yes", "是"})) {
                    message = "已取消覆盖存档。";
                    continue;
                }
            }
            std::string error;
            message = saves.save(game.state(), *slot, error)
                          ? "已保存到" + SaveRepository::slotName(*slot) + "。"
                          : "保存失败，当前游戏不受影响：" + error;
            continue;
        }
        if (verbIs(command, {"load", "读取"})) {
            if (command.args.size() != 1U) {
                message = "用法：load 1（可选1至6或auto）。";
                continue;
            }
            const auto slot = SaveRepository::parseSlot(command.args.front());
            if (!slot) {
                message = "存档位必须是1至6或auto。";
                continue;
            }
            GameState loaded;
            std::string error;
            if (!saves.load(*slot, loaded, error) || !game.replaceState(loaded, error)) {
                message = "读取失败，当前游戏不受影响：" + error;
                continue;
            }
            message = "已读取" + SaveRepository::slotName(*slot) + "。";
            continue;
        }

        const ActionResult result = game.execute(line);
        message = result.recognized ? result.message : "无法识别该命令。输入9或帮助查看完整操作。";
        if (result.success && !result.stateChanged && !result.message.empty()) {
            ui.showStandalone("部落记录", result.message);
            waitForEnter(input);
            message = "已返回当前游戏。";
            continue;
        }
        if (result.endingReached) {
            std::string error;
            if (!saves.save(game.state(), SaveSlot::Autosave, error))
                message += "\n结局自动保存失败：" + error;
            playEnding(game, ui, input, output);
            message += "\n结局演出播放完毕，可重新播放、查看人物或编年史。";
        } else if (result.seasonAdvanced) {
            std::string error;
            if (!saves.save(game.state(), SaveSlot::Autosave, error))
                message += "\n本季自动保存失败：" + error;
            else
                message += "\n本季已自动保存。";
        }
    }
}

std::optional<GameMode> chooseMode(ConsoleUI &ui, std::istream &input, bool &inputClosed) {
    std::string message;
    std::string line;
    for (;;) {
        ui.renderModeMenu(message);
        if (!std::getline(input, line)) {
            inputClosed = true;
            return std::nullopt;
        }
        const Words command = words(line);
        if (command.args.empty() && verbIs(command, {"b", "back", "返回"}))
            return std::nullopt;
        if (command.args.empty()) {
            const auto mode = parseMode(command.verb);
            if (mode)
                return mode;
        }
        message = "无效模式，请输入1、2、3或B。";
    }
}

std::optional<GameState> chooseSave(const SaveRepository &saves, ConsoleUI &ui, std::istream &input,
                                    bool &inputClosed) {
    std::string message;
    std::string line;
    for (;;) {
        ui.renderSaveMenu(saves.inspect(), message);
        if (!std::getline(input, line)) {
            inputClosed = true;
            return std::nullopt;
        }
        const Words command = words(line);
        if (command.args.empty() && verbIs(command, {"b", "back", "返回"}))
            return std::nullopt;
        std::optional<SaveSlot> slot;
        if (command.args.empty()) {
            if (verbIs(command, {"a", "auto", "autosave", "自动", "自动档"}))
                slot = SaveSlot::Autosave;
            else
                slot = SaveRepository::parseSlot(command.verb);
        }
        if (!slot) {
            message = "无效档位，请输入A、1至6或B。";
            continue;
        }
        GameState loaded;
        std::string error;
        if (!saves.load(*slot, loaded, error)) {
            message = error;
            continue;
        }
        return loaded;
    }
}

} // namespace

int runApplication(std::istream &input, std::ostream &output, const std::filesystem::path &saveRoot,
                   const bool interactive, const bool ansiEnabled, const std::size_t terminalWidth) {
    ConsoleUI ui(output, interactive, ansiEnabled, terminalWidth);
    const SaveRepository saves(saveRoot);
    std::string menuMessage;
    std::string line;
    for (;;) {
        ui.renderMainMenu(menuMessage);
        if (!std::getline(input, line))
            break;
        const Words command = words(line);
        if (command.args.empty() && verbIs(command, {"4", "q", "quit", "退出"}))
            break;
        if (command.args.empty() && verbIs(command, {"1", "start", "开始", "开始游戏"})) {
            bool inputClosed = false;
            const auto mode = chooseMode(ui, input, inputClosed);
            if (inputClosed)
                break;
            if (!mode) {
                menuMessage.clear();
                continue;
            }
            GameEngine game({*mode, freshSeed()});
            if (!runGame(game, saves, ui, input, output, true, menuMessage))
                break;
            continue;
        }
        if (command.args.empty() && verbIs(command, {"2", "load", "读取", "读取存档"})) {
            bool inputClosed = false;
            auto loaded = chooseSave(saves, ui, input, inputClosed);
            if (inputClosed)
                break;
            if (!loaded) {
                menuMessage.clear();
                continue;
            }
            GameEngine game(std::move(*loaded));
            if (!runGame(game, saves, ui, input, output, false, menuMessage))
                break;
            continue;
        }
        if (command.args.empty() && verbIs(command, {"3", "h", "help", "帮助", "游戏帮助"})) {
            showHelp(ui, input);
            menuMessage.clear();
            continue;
        }
        if (verbIs(command, {"seed", "种子"}) && command.args.size() == 2U) {
            const auto mode = parseMode(command.args[0]);
            std::uint32_t seed = 0U;
            if (!mode || !parseSeed(command.args[1], seed)) {
                menuMessage = "用法：seed <quick|standard|long> <0至4294967295>。";
                continue;
            }
            GameEngine game({*mode, seed});
            if (!runGame(game, saves, ui, input, output, true, menuMessage))
                break;
            continue;
        }
        menuMessage = "无效选择，请输入1至4。";
    }
    output << "\n燧火未熄，感谢游玩。\n";
    return 0;
}

} // namespace tribe
