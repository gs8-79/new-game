#include "tribe/console_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace tribe {
namespace {

const char *colorCode(const UiColor color) {
    switch (color) {
    case UiColor::Title:
        return "\x1b[1;38;5;208m";
    case UiColor::Accent:
        return "\x1b[1;38;5;214m";
    case UiColor::Dim:
        return "\x1b[38;5;245m";
    case UiColor::Food:
        return "\x1b[1;93m";
    case UiColor::Wood:
        return "\x1b[1;92m";
    case UiColor::Stone:
        return "\x1b[1;97m";
    case UiColor::Herbs:
        return "\x1b[1;95m";
    case UiColor::Friendly:
        return "\x1b[1;92m";
    case UiColor::Neutral:
        return "\x1b[1;94m";
    case UiColor::Enemy:
        return "\x1b[1;91m";
    case UiColor::Warning:
        return "\x1b[1;93m";
    case UiColor::Normal:
        break;
    }
    return "\x1b[0m";
}

struct Glyph {
    std::size_t bytes;
    std::size_t columns;
};

Glyph glyphAt(const std::string_view text, const std::size_t index) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first < 0x80U)
        return {1U, first < 32U ? 0U : 1U};
    const std::size_t count = (first & 0xE0U) == 0xC0U   ? 2U
                              : (first & 0xF0U) == 0xE0U ? 3U
                              : (first & 0xF8U) == 0xF0U ? 4U
                                                         : 1U;
    if (count == 1U || index + count > text.size())
        return {1U, 1U};
    unsigned int code = first & (0x7FU >> count);
    for (std::size_t offset = 1U; offset < count; ++offset) {
        const auto byte = static_cast<unsigned char>(text[index + offset]);
        if ((byte & 0xC0U) != 0x80U)
            return {1U, 1U};
        code = (code << 6U) | (byte & 0x3FU);
    }
    if ((code >= 0x300U && code <= 0x36FU) || (code >= 0xFE00U && code <= 0xFE0FU))
        return {count, 0U};
    const bool wide = (code >= 0x1100U && code <= 0x115FU) ||
                      (code >= 0x2E80U && code <= 0xA4CFU && code != 0x303FU) ||
                      (code >= 0xAC00U && code <= 0xD7A3U) || (code >= 0xF900U && code <= 0xFAFFU) ||
                      (code >= 0xFE10U && code <= 0xFE6FU) || (code >= 0xFF01U && code <= 0xFF60U) ||
                      (code >= 0xFFE0U && code <= 0xFFE6U) || (code >= 0x1F300U && code <= 0x1FAFFU) ||
                      (code >= 0x20000U && code <= 0x3FFFDU);
    return {count, wide ? 2U : 1U};
}

std::size_t displayWidth(const std::string_view text) {
    std::size_t width = 0U;
    for (std::size_t index = 0U; index < text.size();) {
        const Glyph glyph = glyphAt(text, index);
        width += glyph.columns;
        index += glyph.bytes;
    }
    return width;
}

std::string slotKey(const SaveSlot slot) {
    if (slot == SaveSlot::Autosave)
        return "A";
    return std::to_string(static_cast<int>(slot) + 1);
}

} // namespace

ConsoleUI::ConsoleUI(std::ostream &output, const bool interactive, const bool ansiEnabled,
                     const std::size_t width)
    : destination_(output), interactive_(interactive), ansiEnabled_(ansiEnabled),
      width_(std::max<std::size_t>(width == 0U ? detectTerminalWidth() : width, 2U)),
      automaticWidth_(width == 0U) {}

bool ConsoleUI::standardStreamsAreInteractive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdin)) != 0 && isatty(fileno(stdout)) != 0;
#endif
}

bool ConsoleUI::initializeTerminal() {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE)
        return false;
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode) == 0)
        return false;
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

std::size_t ConsoleUI::detectTerminalWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info) != 0) {
        return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
    }
#else
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0U)
        return size.ws_col;
#endif
    return 80U;
}

void ConsoleUI::write(const UiColor color, const std::string_view text) {
    if (!ansiEnabled_ || color == UiColor::Normal) {
        output_ << text;
        return;
    }
    output_ << colorCode(color) << text << "\x1b[0m";
}

void ConsoleUI::writeCentered(const UiColor color, const std::string_view text) {
    const std::size_t textWidth = displayWidth(text);
    if (textWidth < width_)
        output_ << std::string((width_ - textWidth) / 2U, ' ');
    write(color, text);
    output_ << '\n';
}

void ConsoleUI::writeRule(const char fill) {
    write(UiColor::Dim, std::string(width_, fill));
    output_ << '\n';
}

void ConsoleUI::writeSection(const std::string_view title) {
    write(UiColor::Accent, "[ " + std::string(title) + " ]\n");
}

void ConsoleUI::clear() {
    output_.str({});
    output_.clear();
    if (automaticWidth_)
        width_ = std::max<std::size_t>(detectTerminalWidth(), 2U);
    if (interactive_ && ansiEnabled_)
        output_ << "\x1b[2J\x1b[H";
}

void ConsoleUI::flushPage() {
    // 按显示列数换行，UTF-8 字符不可拆开，ANSI 样式不占列宽。
    const std::string page = output_.str();
    std::size_t column = 0U;
    for (std::size_t index = 0U; index < page.size();) {
        if (page[index] == '\x1b' && index + 1U < page.size() && page[index + 1U] == '[') {
            std::size_t end = index + 2U;
            while (end < page.size() && (page[end] < '@' || page[end] > '~'))
                ++end;
            if (end < page.size())
                ++end;
            destination_ << page.substr(index, end - index);
            index = end;
            continue;
        }
        if (page[index] == '\n') {
            destination_ << '\n';
            column = 0U;
            ++index;
            continue;
        }
        const Glyph glyph = glyphAt(page, index);
        if (column + glyph.columns > width_) {
            destination_ << '\n';
            column = 0U;
        }
        destination_ << page.substr(index, glyph.bytes);
        column += glyph.columns;
        index += glyph.bytes;
    }
    output_.str({});
    output_.clear();
    destination_.flush();
}

void ConsoleUI::renderMainMenu(const std::string_view message) {
    clear();
    writeRule('=');
    writeCentered(UiColor::Dim, ".              .");
    writeCentered(UiColor::Dim, "\\     /\\     /");
    writeCentered(UiColor::Dim, " \\___/  \\___/ ");
    writeCentered(UiColor::Accent, "(  /\\  )");
    writeCentered(UiColor::Title, "( /  \\ )");
    writeCentered(UiColor::Title, "/ /\\ \\");
    writeCentered(UiColor::Accent, "/_/  \\_\\");
    writeCentered(UiColor::Title, "《燧火纪：部落黎明》");
    writeCentered(UiColor::Dim, "在季节更迭中，让部落的火种延续");
    writeRule('=');
    output_ << '\n';
    writeCentered(UiColor::Accent, "[ 1 ]  开始游戏");
    writeCentered(UiColor::Normal, "[ 2 ]  读取存档");
    writeCentered(UiColor::Normal, "[ 3 ]  游戏帮助");
    writeCentered(UiColor::Normal, "[ 4 ]  退出游戏");
    if (!message.empty()) {
        output_ << '\n';
        writeCentered(UiColor::Warning, message);
    }
    output_ << '\n';
    prompt("请选择 > ");
}

void ConsoleUI::renderModeMenu(const std::string_view message) {
    clear();
    writeRule('=');
    writeCentered(UiColor::Title, "选择旅程");
    writeCentered(UiColor::Dim, "不同长度使用相同规则，均可完整存档");
    writeRule('=');
    writeSection("游戏模式");
    output_ << "  [ 1 ] 快速游戏    8季   适合快速体验与课堂演示\n"
            << "  [ 2 ] 正式游戏   16季   完整的标准部落旅程\n"
            << "  [ 3 ] 长期游戏   32季   完整经营与结局后沙盒\n"
            << "  [ B ] 返回封面\n";
    if (!message.empty()) {
        output_ << '\n';
        write(UiColor::Warning, "  " + std::string(message) + "\n");
    }
    writeRule();
    prompt("选择模式 > ");
}

void ConsoleUI::renderSaveMenu(const std::vector<SaveSummary> &saves, const std::string_view message) {
    clear();
    writeRule('=');
    writeCentered(UiColor::Title, "读取存档");
    writeCentered(UiColor::Dim, "从上次的火堆旁，继续你的旅程");
    writeRule('=');
    for (const SaveSummary &save : saves) {
        output_ << "  [" << slotKey(save.slot) << "] " << SaveRepository::slotName(save.slot) << "  ";
        if (save.status == SaveStatus::Empty) {
            write(UiColor::Dim, "[空档]\n");
            continue;
        }
        if (save.status == SaveStatus::Corrupt) {
            write(UiColor::Enemy, "[损坏或不可读取]\n");
            continue;
        }
        write(save.status == SaveStatus::Recoverable ? UiColor::Warning : UiColor::Friendly,
              save.status == SaveStatus::Recoverable ? "[可从恢复文件读取]  " : "[可读取]  ");
        output_ << save.modifiedAt << '\n'
                << "      " << save.mode << "  第" << save.season << '/' << save.seasonLimit << "季  "
                << save.phase << "  " << save.tribeName << " · " << save.leaderName << '\n'
                << "      人口 " << save.population << "  食物 " << save.food << "  木材 " << save.wood
                << "  石料 " << save.stone << "  草药 " << save.herbs << '\n';
    }
    output_ << "\n  [B] 返回封面\n";
    if (!message.empty())
        write(UiColor::Warning, "\n  " + std::string(message) + "\n");
    writeRule();
    prompt("选择存档 > ");
}

void ConsoleUI::renderHelpPage(const int topic) {
    clear();
    writeRule('=');
    writeCentered(UiColor::Title, "游戏帮助");
    writeRule('=');
    if (topic == 0) {
        output_ << "\n  [1] 首次游玩    [2] 经营建设    [3] 探索小队\n"
                << "  [4] 外交贸易    [5] 战争与结局  [6] 存档退出\n\n";
        writeCentered(UiColor::Dim, "输入分类编号查看完整命令、参数与示例");
    } else if (topic == 1) {
        writeSection("首次游玩");
        output_ << "  从封面选择开始游戏，再选择8、16或32季旅程。数字命令最适合新手。\n"
                << "  每季行动点有限；先保证食物，再逐步探索、建设和外交。\n";
        output_ << "  输入命令后按 Enter 执行。先用 1 查看状态、2 查看可去的地点。\n"
                << "  示例：3 → 4 → 8。食物不足会导致人口损失，冬季要提前备粮。\n"
                << "  快速模式从第9季开始，到第16季结束；正式从第1季开始。\n";
    } else if (topic == 2) {
        writeSection("经营建设");
        output_ << "  1 状态  2 地图  3 采集食物  4 采集木材  8 结束季节\n"
                << "  gather/采集  scout/侦察  build/建造  research/研究\n";
        output_ << "  采集 <食物|木材|石料|草药>；侦察 <地图中的相邻地点>\n"
                << "  建造 <粮仓|木墙|工坊|医者小屋|瞭望塔|议事火坛>\n"
                << "  研究 <技术名>；示例：采集 食物、建造 粮仓。\n"
                << "  技术：食物保存、草药知识、引水耕作、燧石长矛、盾墙阵形、\n"
                << "        伏击训练、赠礼习俗、共同语言、部落联盟。高级技术需工坊。\n";
    } else if (topic == 3) {
        writeSection("探索小队");
        output_ << "  5 进入苍林任务；任务中可移动、采集、交谈、贸易、战斗和回营。\n"
                << "  move/移动  gather/采集  order/下令  attack/攻击  return/回营\n";
        output_ << "  小队：7/squads；squadtask <采集|巡逻|侦察|护送|训练|停止>\n"
                << "  squadrest 休整；任务路线：移动 苍林 → 移动 深林 → 采集 草药 → 回营。\n"
                << "  战斗：攻击、防御、下令 <集火|坚守|突进|后撤>；胜利后 搜取。\n"
                << "  look/查看 查询任务与物品详情；使用 <药包|草药|口粮>。\n"
                << "  装备 <主手|副手|头部|身体|手部|腿脚|工具|饰品> <物品编号>。\n";
    } else if (topic == 4) {
        writeSection("外交贸易");
        output_ << "  talk/交谈  gift/送礼  trade/贸易  openroute/开通商路  marry/联姻\n";
        output_ << "  交谈/送礼/开通商路/联姻 <部落>；示例：交谈 河鹿。\n"
                << "  贸易 <部落> <给出资源> <换取资源>；例：贸易 河鹿 木材 食物。\n"
                << "  tribute/demand/ally/declare/truce/raid <部落>：朝贡/索贡/结盟/宣战/停战/劫掠。\n"
                << "  factions 查看派系；appease <1至3> 安抚派系。未知部落需要先探索。\n";
    } else if (topic == 5) {
        writeSection("战争与结局");
        output_ << "  formarmy/组建军队  war/出征；战争中可攻击、防御、下令或撤退。\n"
                << "  季节上限到达后，按已满足条件选择联盟、征服、繁荣或迁徙。\n";
        output_ << "  组建军队 <战士人数> <民兵人数>；先 declare <部落> 宣战，再 出征 <部落>。\n"
                << "  下令 <推进|坚守|集火|包抄|掩护|撤退>；retreat/撤退。\n"
                << "  objectives/目标 查看道路条件；choose <alliance|conquest|prosperity|migration>。\n"
                << "  结局后 replay/重新播放、characters/人物、chronicle/编年史；\n"
                << "  长期非覆灭结局可 sandbox/继续沙盒。\n";
    } else if (topic == 6) {
        writeSection("存档退出");
        output_ << "  save/保存 <1至6>  load/读取 <1至6|auto>\n"
                << "  back/返回主菜单会自动保存；quit/退出会先保存，forcequit/强制退出不保存。\n";
        output_ << "  封面选择读取存档，A为自动档，1至6为手动档；每季和正常离开时自动保存。\n"
                << "  覆盖手动档前会要求确认。保存失败后可重试或选择其他档位。\n"
                << "  固定种子：封面输入 seed <quick|standard|long> <数字>。\n";
    }
    prompt(topic == 0 ? "\n输入1至6，或 B/Enter 返回 > " : "\n输入1至6切换分类，B/Enter 返回 > ");
}

void ConsoleUI::renderGame(const GameEngine &game, const std::string_view message) {
    clear();
    const GameState &state = game.state();
    writeRule('=');
    writeCentered(UiColor::Title, "《燧火纪：部落黎明》");
    writeCentered(UiColor::Dim, GameEngine::modeName(state.mode) + "  |  第" + std::to_string(state.season) +
                                    "/" + std::to_string(state.seasonLimit) + "季  |  " +
                                    GameEngine::phaseName(state.phase) + "  |  行动点 " +
                                    std::to_string(state.actionsLeft));
    writeRule('=');

    writeSection("部落");
    output_ << "  " << state.tribeName << "  首领 " << state.leaderName;
    if (!state.actingLeaderName.empty())
        output_ << "（代理/继任 " << state.actingLeaderName << "）";
    output_ << "  人口 " << state.population << "  战士 " << state.warriors << "  稳定 " << state.stability
            << "  士气 " << state.morale << "  营地 " << state.campDurability << '\n';

    writeSection("资源");
    output_ << "  ";
    write(UiColor::Food, "食物 " + std::to_string(state.food));
    output_ << "   ";
    write(UiColor::Wood, "木材 " + std::to_string(state.wood));
    output_ << "   ";
    write(UiColor::Stone, "石料 " + std::to_string(state.stone));
    output_ << "   ";
    write(UiColor::Herbs, "草药 " + std::to_string(state.herbs));
    output_ << "   贝币 " << state.shells << (state.currencyUnlocked ? "（已流通）" : "（未解锁）") << '\n'
            << "  地点 " << std::count(state.discovered.begin(), state.discovered.end(), true) << "/16"
            << "  建筑 " << std::count(state.buildings.begin(), state.buildings.end(), true) << "/6"
            << "  技术 " << std::count(state.technologies.begin(), state.technologies.end(), true) << "/9"
            << "  贸易 " << state.tradeCount << "  战争胜负 " << state.warsWon << '/' << state.warsLost
            << '\n';

    writeSection("周边局势");
    output_ << "  ";
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        const auto tribe = static_cast<TribeId>(index);
        const DiplomacyRelation &relation = state.relations[index];
        const UiColor color = relation.atWar      ? UiColor::Enemy
                              : relation.alliance ? UiColor::Friendly
                                                  : UiColor::Neutral;
        write(color, GameEngine::tribeName(tribe) + " " + std::to_string(relation.relation));
        if (relation.atWar)
            output_ << "[战]";
        else if (relation.alliance)
            output_ << "[盟]";
        else if (relation.tradeRoute)
            output_ << "[商]";
        output_ << (index + 1U == kTribeCount ? '\n' : ' ');
    }

    writeSection("当前局面");
    if (state.phase == GamePhase::Mission && state.activeMission) {
        // 苍林任务使用共同的小队组件，显示其实时资源与队长状态。
        const ExpansionState &mission = *state.activeMission;
        output_ << "  苍林任务：" << ExpansionGame::phaseName(mission.phase) << " / "
                << ExpansionGame::locationName(mission.location) << "，回合 " << mission.turn;
        if (mission.phase == ExpansionPhase::FrontlineCombat) {
            output_ << "，战线 " << mission.frontline << "/3，敌方生命 " << mission.enemyLife;
        }
        output_ << '\n';
        const Character &captain = mission.squad.members[mission.squad.leaderIndex];
        output_ << "  队长 " << captain.name << "  生命 " << captain.life << "  疲劳 " << captain.fatigue
                << "  口粮 " << mission.supplies << "  药包 " << mission.medicine << '\n';
    } else if (state.phase == GamePhase::War) {
        output_ << "  部落战争：对" << GameEngine::tribeName(state.war.enemy) << "，战线 " << state.war.front
                << "/3，己方战力 " << state.war.playerPower << "，敌方战力 " << state.war.enemyPower << '\n';
    } else if (state.phase == GamePhase::Finished) {
        write(UiColor::Warning, "  结局已确定：" + GameEngine::endingName(state.ending) + "\n");
    } else {
        output_ << "  小队 " << state.squads.size();
        if (!state.squads.empty()) {
            const PermanentSquad &squad = state.squads.front();
            output_ << "  " << squad.name << "  队长 " << squad.captain << "  疲劳 " << squad.fatigue;
            if (squad.refusingOrders)
                output_ << " [抗命]";
        }
        output_ << '\n';
    }

    writeSection("最近消息");
    output_ << "  " << (message.empty() ? "火堆噼啪作响，等待你的决定。" : std::string(message)) << '\n';

    writeSection("可用命令");
    if (state.phase == GamePhase::Mission) {
        output_ << "  移动 采集 交谈 贸易 劫掠 攻击 防御 下令 搜取 回营 放弃任务\n";
    } else if (state.phase == GamePhase::War) {
        output_ << "  攻击 防御 下令 <推进|坚守|集火|包抄|掩护|撤退> 撤退\n";
    } else if (state.phase == GamePhase::EndingChoice) {
        output_ << "  目标  选择 <联盟|征服|繁荣|迁徙>\n";
    } else if (state.phase == GamePhase::Finished) {
        output_ << "  重新播放  人物  编年史  继续沙盒  返回主菜单\n";
    } else {
        output_ << "  1状态  2地图  3食物  4木材  5苍林任务  6外交  7小队  8结束季节  9帮助\n"
                << "  经营：采集 侦察 建造 研究    外交：交谈 送礼 贸易 开通商路 联姻\n";
    }
    output_ << "  存档：save/保存 <1至6>  load/读取 <1至6|auto>  返回：back  退出：quit\n";
    writeRule();
    prompt();
}

void ConsoleUI::showStandalone(const std::string_view title, const std::string_view text) {
    clear();
    writeRule('=');
    writeCentered(UiColor::Title, title);
    writeRule('=');
    output_ << text << "\n\n按 Enter 返回……";
    flushPage();
}

void ConsoleUI::prompt(const std::string_view text) {
    write(UiColor::Accent, text);
    flushPage();
}

} // namespace tribe
