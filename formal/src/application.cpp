#include "tribe/application.hpp"

#include "command_parser.hpp"
#include "tribe/console_ui.hpp"
#include "tribe/ending_presentation.hpp"
#include "tribe/save_repository.hpp"

#include <chrono>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tribe {
namespace {

// 本匿名命名空间的流程辅助函数只处理输入流、UI 和局部变量：输出为解析或流程结果，不直接写 GameEngine 状态。
// 解析失败返回空或 false，流程失败由调用方显示消息；命令兼容性始终经 command_parser 保持。
using command_parser::Command;
using command_parser::parse;
using command_parser::verbIs;

/// 用途：从交互控制台或脚本流读取一行 UTF-8 命令。输入：输入流和输出字符串。输出：是否读到一行。
/// 状态影响：仅推进输入流；交互控制台使用宽字符 API，脚本继续使用 getline。不变量：提交前去掉首尾空白。
bool readInputLine(std::istream& input, std::string& line) {
#ifdef _WIN32
    if (&input == &std::cin) {
        const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
            std::vector<wchar_t> wideLine(4096U);
            DWORD read = 0;
            if (!ReadConsoleW(handle, wideLine.data(), static_cast<DWORD>(wideLine.size() - 1U), &read, nullptr))
                return false;
            while (read > 0U && (wideLine[read - 1U] == L'\r' || wideLine[read - 1U] == L'\n')) --read;
            wideLine[read] = L'\0';
            const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideLine.data(), static_cast<int>(read),
                                                   nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return false;
            line.resize(static_cast<std::size_t>(bytes));
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideLine.data(), static_cast<int>(read), line.data(),
                                    bytes, nullptr, nullptr) != bytes)
                return false;
        } else if (!std::getline(input, line)) {
            return false;
        }
    } else if (!std::getline(input, line)) {
        return false;
    }
#else
    if (!std::getline(input, line)) return false;
#endif
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        line.clear();
        return true;
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    line = line.substr(first, last - first + 1U);
    return true;
}

/// 用途：生成本次新局的时间种子。输入：无。输出：32 位种子；无游戏状态修改。
/// 失败：无。不变量：只用于初始化新局，不能替代存档中的持久化种子。
std::uint32_t freshSeed() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint32_t>(static_cast<unsigned long long>(now) & 0xFFFFFFFFULL);
}

/// 用途：解析无符号种子文本。输入：文本和输出种子。输出：是否成功。
/// 状态影响：仅成功时写 seed。失败：空白、溢出或残余字符返回 false；不变量：不接受部分数字。
bool parseSeed(const std::string& text, std::uint32_t& seed) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seed);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

/// 用途：解析模式中英文别名。输入：已统一规范化的词元。输出：模式或空值；无状态修改。
/// 失败：未知别名返回空。不变量：玩家可见中英文模式命令保持兼容。
std::optional<GameMode> parseMode(const std::string_view text) {
    if (text == "1" || text == "quick" || text == "fast" || text == "快速") return GameMode::Quick;
    if (text == "2" || text == "standard" || text == "formal" || text == "正式") return GameMode::Standard;
    if (text == "3" || text == "long" || text == "长期") return GameMode::Long;
    return std::nullopt;
}

/// 用途：消费一次确认输入。输入：标准输入流。输出：无。
/// 状态影响：仅推进输入流。失败：流结束时直接返回；不变量：不触发任何游戏状态提交。
void waitForEnter(std::istream& input) {
    std::string ignored;
    readInputLine(input, ignored);
}

/// 用途：循环展示帮助主题。输入：UI 和输入流。输出：无。
/// 状态影响：仅输出和消费输入。失败：输入结束时返回；不变量：使用共享解析器且不修改 GameEngine。
void showHelp(ConsoleUI& ui, std::istream& input) {
    int topic = 0;
    std::string line;
    for (;;) {
        ui.renderHelpPage(topic);
        if (!readInputLine(input, line)) return;
        const Command command = parse(line);
        if (command.args.empty() && (command.verb.empty() || verbIs(command, {"b", "back", "返回"}))) {
            if (topic == 0) return;
            topic = 0;
        } else if (command.args.empty() && command.verb.size() == 1U && command.verb[0] >= '1' &&
                   command.verb[0] <= '6') {
            topic = command.verb[0] - '0';
        }
    }
}

/// 用途：按终端能力播放或静态展示结局。输入：只读游戏、UI、流。输出：无。
/// 状态影响：仅输出和消费确认输入。失败：动画回退不改变游戏状态；不变量：ANSI 开关由 UI 统一控制。
void playEnding(const GameEngine& game, ConsoleUI& ui, std::istream& input, std::ostream& output) {
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

/// 用途：运行一局游戏的命令循环。输入：引擎、存档仓库、UI、流及起始消息。输出：是否返回封面。
/// 状态影响：仅经 GameEngine/SaveRepository
/// 的原子接口提交。失败：读写失败保留当前局；不变量：空白和中英文命令共用解析器。
bool runGame(GameEngine& game, const SaveRepository& saves, ConsoleUI& ui, std::istream& input, std::ostream& output,
             const bool saveInitial, std::string& exitMessage, std::string initialMessage = {}) {
    exitMessage.clear();
    std::string message = std::move(initialMessage);
    if (message.empty())
        message = GameEngine::modeName(game.state().mode) + "已经开始。输入1查看状态，输入9或帮助查看完整命令。";
    if (saveInitial) {
        std::string error;
        if (!saves.save(game.state(), SaveSlot::Autosave, error)) message += "\n自动保存失败：" + error;
    }

    std::string line;
    for (;;) {
        ui.renderGame(game, message);
        if (!readInputLine(input, line)) return false;
        const Command command = parse(line);
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
                message =
                    "自动保存失败，仍留在游戏中：" + error + " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            exitMessage = "已返回封面，当前进度保存在自动档。";
            return true;
        }
        if (verbIs(command, {"quit", "exit", "退出"}) && command.args.empty()) {
            std::string error;
            if (!saves.save(game.state(), SaveSlot::Autosave, error)) {
                message =
                    "退出前保存失败，游戏仍保留：" + error + " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            return false;
        }
        if (verbIs(command, {"forcequit", "强制退出"}) && command.args.empty()) return false;
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
            for (const auto& summary : saves.inspect()) {
                if (summary.slot == *slot) occupied = summary.status != SaveStatus::Empty;
            }
            if (occupied) {
                ui.prompt("覆盖" + SaveRepository::slotName(*slot) + "？输入 y/是 确认，其他输入取消 > ");
                std::string answer;
                if (!readInputLine(input, answer)) return false;
                const Command confirmation = parse(answer);
                if (!confirmation.args.empty() || !verbIs(confirmation, {"y", "yes", "是"})) {
                    message = "已取消覆盖存档。";
                    continue;
                }
            }
            std::string error;
            message = saves.save(game.state(), *slot, error) ? "已保存到" + SaveRepository::slotName(*slot) + "。"
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
            SaveLoadInfo loadInfo;
            if (!saves.load(*slot, loaded, error, &loadInfo) || !game.replaceState(loaded, error)) {
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
            if (!saves.save(game.state(), SaveSlot::Autosave, error)) message += "\n结局自动保存失败：" + error;
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

/// 用途：在封面读取模式选择。输入：UI、输入流和结束标志。输出：模式或空值。
/// 状态影响：仅更新 inputClosed。失败：流结束置标志；不变量：返回空值不创建或修改游戏状态。
std::optional<GameMode> chooseMode(ConsoleUI& ui, std::istream& input, bool& inputClosed) {
    std::string message;
    std::string line;
    for (;;) {
        ui.renderModeMenu(message);
        if (!readInputLine(input, line)) {
            inputClosed = true;
            return std::nullopt;
        }
        const Command command = parse(line);
        if (command.args.empty() && verbIs(command, {"b", "back", "返回"})) return std::nullopt;
        if (command.args.empty()) {
            const auto mode = parseMode(command.verb);
            if (mode) return mode;
        }
        message = "无效模式，请输入1、2、3或B。";
    }
}

/// 用途：在存档菜单加载一份完整合法状态。输入：仓库、UI、流及输出标志/消息。输出：状态或空值。
/// 状态影响：成功时返回已解析存档，绝不写入现有游戏。失败：保留调用方消息并不产生候选状态污染。
std::optional<GameState> chooseSave(const SaveRepository& saves, ConsoleUI& ui, std::istream& input, bool& inputClosed,
                                    std::string& loadedMessage) {
    loadedMessage.clear();
    std::string message;
    std::string line;
    for (;;) {
        ui.renderSaveMenu(saves.inspect(), message);
        if (!readInputLine(input, line)) {
            inputClosed = true;
            return std::nullopt;
        }
        const Command command = parse(line);
        if (command.args.empty() && verbIs(command, {"b", "back", "返回"})) return std::nullopt;
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
        SaveLoadInfo loadInfo;
        if (!saves.load(*slot, loaded, error, &loadInfo)) {
            message = error;
            continue;
        }
        loadedMessage = "已读取" + SaveRepository::slotName(*slot) + "。";
        return loaded;
    }
}

} // namespace

int runApplication(std::istream& input, std::ostream& output, const std::filesystem::path& saveRoot,
                   const bool interactive, const bool ansiEnabled, const std::size_t terminalWidth) {
    ConsoleUI ui(output, interactive, ansiEnabled, terminalWidth);
    const SaveRepository saves(saveRoot);
    std::string menuMessage;
    std::string line;
    for (;;) {
        ui.renderMainMenu(menuMessage);
        if (!readInputLine(input, line)) break;
        const Command command = parse(line);
        if (command.args.empty() && verbIs(command, {"4", "q", "quit", "退出"})) break;
        if (command.args.empty() && verbIs(command, {"1", "start", "开始", "开始游戏"})) {
            bool inputClosed = false;
            const auto mode = chooseMode(ui, input, inputClosed);
            if (inputClosed) break;
            if (!mode) {
                menuMessage.clear();
                continue;
            }
            GameEngine game({*mode, freshSeed()});
            if (!runGame(game, saves, ui, input, output, true, menuMessage)) break;
            continue;
        }
        if (command.args.empty() && verbIs(command, {"2", "load", "读取", "读取存档"})) {
            bool inputClosed = false;
            auto loaded = chooseSave(saves, ui, input, inputClosed, menuMessage);
            if (inputClosed) break;
            if (!loaded) {
                menuMessage.clear();
                continue;
            }
            GameEngine game(std::move(*loaded));
            const std::string loadedMessage = menuMessage;
            if (!runGame(game, saves, ui, input, output, false, menuMessage, loadedMessage)) break;
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
            if (!runGame(game, saves, ui, input, output, true, menuMessage)) break;
            continue;
        }
        menuMessage = "无效选择，请输入1至4。";
    }
    output << "\n燧火未熄，感谢游玩。\n";
    return 0;
}

} // namespace tribe
