#include "tribe/console_ui.hpp"
#include "tribe/campaign.hpp"
#include "tribe/campaign_save_repository.hpp"
#include "tribe/expansion_game.hpp"
#include "tribe/ending_presentation.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"

#include <chrono>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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

bool parseSquadSize(const std::string& text, std::size_t& size) {
    unsigned int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || parsed < tribe::kMinimumSquadSize || parsed > tribe::kMaximumSquadSize) {
        return false;
    }
    size = parsed;
    return true;
}

std::optional<tribe::CampaignMode> parseCampaignMode(const std::string_view text) {
    if (text == "quick" || text == "fast" || text == "8" || text == "快速" || text == "快速8季") {
        return tribe::CampaignMode::Quick;
    }
    if (text == "course" || text == "standard" || text == "16" || text == "课程" || text == "课程16季") {
        return tribe::CampaignMode::Course;
    }
    if (text == "long" || text == "32" || text == "长期" || text == "长期32季") {
        return tribe::CampaignMode::Long;
    }
    return std::nullopt;
}

void waitForEnter(tribe::ConsoleUI& ui) {
    if (!ui.interactive()) return;
    std::string ignored;
    std::getline(std::cin, ignored);
}

void playCampaignEnding(const tribe::CampaignGame& game, tribe::ConsoleUI& ui) {
    tribe::EndingPresentationOptions options;
    options.animated = ui.interactive();
    options.ansiEnabled = ui.ansiEnabled();
    options.clearBetweenFrames = ui.interactive() && ui.ansiEnabled();
    options.frameDelay = std::chrono::milliseconds{260};
    ui.clear();
    tribe::EndingPresentation::play(game.endingSummary(), std::cout, options);
    if (ui.interactive()) {
        std::cout << "\n\n按 Enter 返回结算菜单。";
        waitForEnter(ui);
    }
}

enum class SaveExitStatus { Autosaved, Manual1, Manual2, Manual3, Cancelled, InputClosed };

SaveExitStatus manualSaveStatus(const tribe::SaveSlot slot) {
    switch (slot) {
    case tribe::SaveSlot::Slot1: return SaveExitStatus::Manual1;
    case tribe::SaveSlot::Slot2: return SaveExitStatus::Manual2;
    case tribe::SaveSlot::Slot3: return SaveExitStatus::Manual3;
    case tribe::SaveSlot::Autosave: break;
    }
    return SaveExitStatus::Cancelled;
}

std::string saveExitMessage(const SaveExitStatus status, const bool finished) {
    if (status == SaveExitStatus::Autosaved) {
        return finished ? "本局结束，结局已经写入自动存档。" : "已经返回主菜单；最近游戏保存在自动档。";
    }
    const char* slot = status == SaveExitStatus::Manual1 ? "手动存档1"
        : status == SaveExitStatus::Manual2 ? "手动存档2" : "手动存档3";
    return finished ? std::string("本局结束，结局已经另存到") + slot + "。"
                    : std::string("已经返回主菜单；最近游戏另存到") + slot + "。";
}

SaveExitStatus recoverAutosaveFailure(tribe::GameEngine& engine, const tribe::SaveRepository& saves,
    tribe::ConsoleUI& ui, std::string saveError) {
    std::string line;
    for (;;) {
        const std::string message = "自动保存失败：" + saveError
            + "\n当前游戏仍保留。请选择：1重试自动保存；2/3/4另存到手动档1/2/3；5取消离开。"
              "也可输入 retry、save 1|2|3、cancel。";
        ui.renderGame(engine, message);
        if (!std::getline(std::cin, line)) return SaveExitStatus::InputClosed;
        const Words command = words(line);

        if ((command.verb == "1" || verbIs(command, {"retry", "重试"})) && command.args.empty()) {
            if (saves.save(engine.state(), tribe::SaveSlot::Autosave, saveError)) {
                return SaveExitStatus::Autosaved;
            }
            continue;
        }
        if ((command.verb == "5" || verbIs(command, {"cancel", "取消", "继续"})) && command.args.empty()) {
            return SaveExitStatus::Cancelled;
        }

        std::optional<tribe::SaveSlot> slot;
        if (command.args.empty() && command.verb == "2") slot = tribe::SaveSlot::Slot1;
        else if (command.args.empty() && command.verb == "3") slot = tribe::SaveSlot::Slot2;
        else if (command.args.empty() && command.verb == "4") slot = tribe::SaveSlot::Slot3;
        else if (verbIs(command, {"save", "保存"}) && command.args.size() == 1U) {
            slot = tribe::SaveRepository::parseSlot(command.args.front());
        }
        if (!slot || *slot == tribe::SaveSlot::Autosave) {
            saveError = "恢复选项无效，请输入1至5，或 retry / save 1|2|3 / cancel。";
            continue;
        }
        if (saves.save(engine.state(), *slot, saveError)) return manualSaveStatus(*slot);
    }
}

SaveExitStatus saveBeforeLeaving(tribe::GameEngine& engine, const tribe::SaveRepository& saves,
    tribe::ConsoleUI& ui) {
    std::string error;
    if (saves.save(engine.state(), tribe::SaveSlot::Autosave, error)) return SaveExitStatus::Autosaved;
    return recoverAutosaveFailure(engine, saves, ui, std::move(error));
}

bool runGame(tribe::GameEngine& engine, const tribe::SaveRepository& saves, tribe::ConsoleUI& ui,
    const bool saveInitial, std::string& exitMessage) {
    exitMessage.clear();
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
            const SaveExitStatus status = saveBeforeLeaving(engine, saves, ui);
            if (status == SaveExitStatus::Cancelled) {
                message = "已取消返回主菜单，当前游戏仍保留。";
                continue;
            }
            if (status == SaveExitStatus::InputClosed) return false;
            exitMessage = saveExitMessage(status, false);
            return true;
        }
        if (verbIs(command, {"quit", "退出"}) && command.args.empty()) {
            const SaveExitStatus status = saveBeforeLeaving(engine, saves, ui);
            if (status == SaveExitStatus::Cancelled) {
                message = "已取消退出，当前游戏仍保留。";
                continue;
            }
            return false;
        }

        const tribe::ActionResult result = engine.execute(line);
        message = result.recognized ? result.message : "无法识别该命令。输入9、help或帮助查看完整操作。";
        bool autosaveSucceeded = true;
        std::string autosaveError;
        if (result.seasonAdvanced || (result.stateChanged && engine.state().phase == tribe::Phase::Finished)) {
            autosaveSucceeded = saves.save(engine.state(), tribe::SaveSlot::Autosave, autosaveError);
            if (!autosaveSucceeded) {
                message += "\n自动保存失败：" + autosaveError;
            }
        }
        if (result.stateChanged && engine.state().phase == tribe::Phase::Finished) {
            SaveExitStatus status = SaveExitStatus::Autosaved;
            if (!autosaveSucceeded) status = recoverAutosaveFailure(engine, saves, ui, autosaveError);
            if (status == SaveExitStatus::Cancelled) {
                message += "\n已取消离开；本局结局仍保留在当前进程中。可再次保存、返回或退出。";
                continue;
            }
            if (status == SaveExitStatus::InputClosed) return false;
            exitMessage = saveExitMessage(status, true);
            message += "\n" + exitMessage;
            ui.renderGame(engine, message);
            std::cout << "\n本局结束，按 Enter 返回主菜单……";
            waitForEnter(ui);
            return true;
        }
    }
}

bool runExpansionGame(tribe::ExpansionGame& game, tribe::ConsoleUI& ui, std::string& exitMessage) {
    std::string message = "苍林狩猎纵切已经开始。先输入 move forest / 移动 苍林。";
    std::string line;
    for (;;) {
        ui.renderExpansion(game, message);
        if (!std::getline(std::cin, line)) return false;
        const Words command = words(line);
        if (command.verb.empty()) {
            message = "请输入命令；第一次试玩可输入 help 或帮助。";
            continue;
        }
        if (verbIs(command, {"help", "帮助"}) && command.args.empty()) {
            ui.showStandalone("大型扩展V1帮助",
                "目标：编成具名小队进入苍林，采集物资，处理外族遭遇并安全回营。\n"
                "和平路线：移动 苍林 → 移动 深林 → 采集 草药 → 移动 空地 → 交谈 → 贸易 → 回营。\n"
                "战斗路线：移动 苍林 → 移动 深林 → 移动 空地 → 劫掠 → 下令 集火 → 攻击。\n"
                "战斗共有三段战线；队长每回合行动，队员会自动跟随。胜利后输入搜取，再回营。\n"
                "拾取不耗回合，战斗中禁止换装；疲劳会降低先手和战斗表现。");
            waitForEnter(ui);
            message = "帮助页已关闭。";
            continue;
        }
        if (verbIs(command, {"back", "返回", "返回主菜单"}) && command.args.empty()) {
            exitMessage = "已结束大型扩展V1试玩并返回主菜单。";
            return true;
        }
        if (verbIs(command, {"quit", "退出"}) && command.args.empty()) return false;

        const tribe::ExpansionCommandResult result = game.execute(line);
        message = result.recognized ? result.message
                                    : "无法识别该命令。输入 help 或帮助查看示例路线。";
    }
}

bool runCampaignGame(tribe::CampaignGame& game, const tribe::CampaignSaveRepository& saves,
    tribe::ConsoleUI& ui, std::string& exitMessage) {
    exitMessage.clear();
    std::string message = tribe::CampaignGame::modeName(game.state().mode)
        + "已经开始。输入1查看状态，输入9或帮助查看完整命令。";
    std::string line;
    for (;;) {
        ui.renderCampaign(game, message);
        if (!std::getline(std::cin, line)) return false;
        const Words command = words(line);
        if (command.verb.empty()) {
            message = "请输入命令；第一次游玩可输入9、help或帮助。";
            continue;
        }
        if ((command.verb == "9" || verbIs(command, {"help", "帮助"})) && command.args.empty()) {
            ui.showStandalone("V2长期战役帮助", game.helpText()
                + "\n存档：save/保存 <1至6|auto>，load/读取 <1至6|auto>。"
                  "\n公共：back/返回主菜单；quit/退出。每季和返回时自动保存。");
            waitForEnter(ui);
            message = "帮助页已关闭。";
            continue;
        }
        if (verbIs(command, {"back", "返回", "返回主菜单"}) && command.args.empty()) {
            std::string error;
            if (!saves.save(game.state(), tribe::CampaignSaveSlot::Autosave, error)) {
                message = "自动保存失败，仍留在游戏中：" + error
                    + " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            exitMessage = "已返回主菜单；V2战役保存在自动档。";
            return true;
        }
        if (verbIs(command, {"quit", "exit", "退出"}) && command.args.empty()) {
            std::string error;
            if (!saves.save(game.state(), tribe::CampaignSaveSlot::Autosave, error)) {
                message = "退出前保存失败，游戏仍保留：" + error
                    + " 可重试、输入 save 1 另存，或输入 forcequit 强制退出。";
                continue;
            }
            return false;
        }
        if (verbIs(command, {"forcequit", "强制退出"}) && command.args.empty()) return false;
        if (verbIs(command, {"replay", "重新播放"}) && command.args.empty()) {
            if (game.state().phase != tribe::CampaignPhase::Finished) {
                message = "只有结局确定后才能重新播放演出。";
            } else {
                playCampaignEnding(game, ui);
                message = "结局演出播放完毕。";
            }
            continue;
        }
        if (verbIs(command, {"characters", "人物", "查看人物"}) && command.args.empty()) {
            ui.showStandalone("人物与小队结算", game.squadText());
            waitForEnter(ui);
            message = "已查看人物与小队结算。";
            continue;
        }
        if (verbIs(command, {"save", "保存"})) {
            if (command.args.size() != 1U) {
                message = "用法：save 1（可选1至6或auto）。";
                continue;
            }
            const auto slot = tribe::CampaignSaveRepository::parseSlot(command.args.front());
            if (!slot) {
                message = "V2存档位必须是1至6或auto。";
                continue;
            }
            std::string error;
            message = saves.save(game.state(), *slot, error)
                ? "已保存到" + tribe::CampaignSaveRepository::slotName(*slot) + "。"
                : "保存失败，当前游戏不受影响：" + error;
            continue;
        }
        if (verbIs(command, {"load", "读取"})) {
            if (command.args.size() != 1U) {
                message = "用法：load 1（可选1至6或auto）。";
                continue;
            }
            const auto slot = tribe::CampaignSaveRepository::parseSlot(command.args.front());
            if (!slot) {
                message = "V2存档位必须是1至6或auto。";
                continue;
            }
            tribe::CampaignState loaded;
            std::string error;
            if (!saves.load(*slot, loaded, error) || !game.replaceState(loaded, error)) {
                message = "读取失败，当前游戏不受影响：" + error;
                continue;
            }
            message = "已读取" + tribe::CampaignSaveRepository::slotName(*slot) + "。";
            continue;
        }

        const tribe::CampaignActionResult result = game.execute(line);
        message = result.recognized ? result.message
                                    : "无法识别该命令。输入9、help或帮助查看完整操作。";
        if (result.endingReached) {
            std::string error;
            if (!saves.save(game.state(), tribe::CampaignSaveSlot::Autosave, error)) {
                message += "\n结局自动保存失败：" + error + "。";
            }
            playCampaignEnding(game, ui);
            message += "\n结局演出播放完毕，可重新播放、查看人物或编年史。";
        }
        if (result.seasonAdvanced) {
            std::string error;
            if (!saves.save(game.state(), tribe::CampaignSaveSlot::Autosave, error)) {
                message += "\n本季自动保存失败：" + error + "（游戏仍可继续）。";
            } else {
                message += "\n本季已自动保存。";
            }
        }
    }
}

} // namespace

int main() {
    const bool interactive = tribe::ConsoleUI::standardStreamsAreInteractive();
    const bool ansi = interactive && tribe::ConsoleUI::initializeTerminal();
    tribe::ConsoleUI ui(std::cout, interactive, ansi);
    const tribe::SaveRepository saves(std::filesystem::current_path() / "saves" / "formal");
    const tribe::CampaignSaveRepository campaignSaves(
        std::filesystem::current_path() / "saves" / "formal-v2");

    std::string menuMessage;
    std::string line;
    for (;;) {
        ui.renderMainMenu(menuMessage);
        if (!std::getline(std::cin, line)) break;
        const Words command = words(line);
        if (verbIs(command, {"q", "quit", "退出"})) break;
        if (verbIs(command, {"h", "help", "帮助"})) {
            tribe::GameEngine example({tribe::GameMode::Standard, 1U});
            ui.showStandalone("主菜单与新手帮助",
                "V2战役：7快速8季、8课程16季、9长期32季；也可输入 campaign quick|course|long。\n"
                "大型扩展V1：输入e；旧正式模式：输入1或2。\n\n旧正式模式命令：\n" + example.helpText());
            waitForEnter(ui);
            menuMessage.clear();
            continue;
        }

        if (command.args.empty() && (command.verb == "e" || command.verb == "expansion"
                || command.verb == "扩展")) {
            tribe::ExpansionGame game(freshSeed(), 4U);
            if (!runExpansionGame(game, ui, menuMessage)) break;
            continue;
        }
        if ((command.verb == "expanded" || command.verb == "扩展小队") && command.args.size() == 1U) {
            std::size_t squadSize = 0;
            if (!parseSquadSize(command.args.front(), squadSize)) {
                menuMessage = "扩展小队人数必须是2至8。";
                continue;
            }
            tribe::ExpansionGame game(freshSeed(), squadSize);
            if (!runExpansionGame(game, ui, menuMessage)) break;
            continue;
        }
        if (command.verb == "expandedseed" && command.args.size() == 2U) {
            std::uint32_t seed = 0;
            std::size_t squadSize = 0;
            if (!parseSeed(command.args[0], seed) || !parseSquadSize(command.args[1], squadSize)) {
                menuMessage = "用法：expandedseed <0至4294967295> <2至8人>。";
                continue;
            }
            tribe::ExpansionGame game(seed, squadSize);
            if (!runExpansionGame(game, ui, menuMessage)) break;
            continue;
        }

        std::optional<tribe::CampaignMode> campaignMode;
        std::uint32_t campaignSeed = freshSeed();
        bool campaignRequested = false;
        if (command.args.empty() && (command.verb == "7" || command.verb == "v2quick"
                || command.verb == "快速战役")) {
            campaignMode = tribe::CampaignMode::Quick;
            campaignRequested = true;
        } else if (command.args.empty() && (command.verb == "8" || command.verb == "v2course"
                || command.verb == "课程战役")) {
            campaignMode = tribe::CampaignMode::Course;
            campaignRequested = true;
        } else if (command.args.empty() && (command.verb == "9" || command.verb == "v2long"
                || command.verb == "长期战役")) {
            campaignMode = tribe::CampaignMode::Long;
            campaignRequested = true;
        } else if (verbIs(command, {"campaign", "v2", "战役"}) && command.args.size() == 1U) {
            campaignMode = parseCampaignMode(command.args.front());
            campaignRequested = true;
        } else if (verbIs(command, {"campaignseed", "v2seed", "战役种子"}) && command.args.size() == 2U) {
            campaignMode = parseCampaignMode(command.args[0]);
            campaignRequested = true;
            if (!parseSeed(command.args[1], campaignSeed)) {
                menuMessage = "战役种子必须是0到4294967295之间的整数。";
                continue;
            }
        }
        if (campaignRequested) {
            if (!campaignMode) {
                menuMessage = "V2模式必须是 quick/course/long，或 快速/课程/长期。";
                continue;
            }
            tribe::CampaignGame game({*campaignMode, campaignSeed});
            if (!runCampaignGame(game, campaignSaves, ui, menuMessage)) break;
            continue;
        }

        if (verbIs(command, {"v2load", "campaignload", "读取战役"}) && command.args.size() == 1U) {
            const auto slot = tribe::CampaignSaveRepository::parseSlot(command.args.front());
            if (!slot) {
                menuMessage = "V2存档位必须是1至6或auto。";
                continue;
            }
            tribe::CampaignState loaded;
            std::string error;
            if (!campaignSaves.load(*slot, loaded, error)) {
                menuMessage = "V2读取失败：" + error;
                continue;
            }
            tribe::CampaignGame game(std::move(loaded));
            if (!runCampaignGame(game, campaignSaves, ui, menuMessage)) break;
            continue;
        }

        if (command.args.empty() && (command.verb == "1" || command.verb == "2")) {
            const tribe::GameMode mode = command.verb == "1" ? tribe::GameMode::Standard : tribe::GameMode::Quick;
            tribe::GameEngine engine({mode, freshSeed()});
            if (!runGame(engine, saves, ui, true, menuMessage)) break;
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
            if (!runGame(engine, saves, ui, true, menuMessage)) break;
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
            if (!runGame(engine, saves, ui, false, menuMessage)) break;
            continue;
        }
        menuMessage = "无效选择，请输入1至9、e、h或q。";
    }

    std::cout << "\n燧火未熄，感谢游玩。\n";
    return 0;
}
