#include "tribe/console_ui.hpp"

#include "tribe/campaign.hpp"
#include "tribe/content.hpp"
#include "tribe/expansion_game.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace tribe {
namespace {

const char* colorCode(const UiColor color) {
    switch (color) {
    case UiColor::Title: return "\x1b[1;96m";
    case UiColor::Food: return "\x1b[1;93m";
    case UiColor::Wood: return "\x1b[1;92m";
    case UiColor::Stone: return "\x1b[1;97m";
    case UiColor::Herbs: return "\x1b[1;95m";
    case UiColor::Friendly: return "\x1b[1;92m";
    case UiColor::Neutral: return "\x1b[1;94m";
    case UiColor::Enemy: return "\x1b[1;91m";
    case UiColor::Warning: return "\x1b[1;93m";
    case UiColor::Normal: break;
    }
    return "\x1b[0m";
}

std::string discoveredLabel(const GameState& state, const LocationId id) {
    return state.discovered[indexOf(id)] ? std::string(location(id).chineseName) : "????";
}

} // namespace

ConsoleUI::ConsoleUI(std::ostream& output, const bool interactive, const bool ansiEnabled)
    : output_(output), interactive_(interactive), ansiEnabled_(ansiEnabled) {}

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
    if (output == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode) == 0) return false;
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

void ConsoleUI::write(const UiColor color, const std::string_view text) {
    if (!ansiEnabled_ || color == UiColor::Normal) {
        output_ << text;
        return;
    }
    output_ << colorCode(color) << text << "\x1b[0m";
}

void ConsoleUI::clear() {
    if (interactive_ && ansiEnabled_) output_ << "\x1b[2J\x1b[H";
}

void ConsoleUI::renderMainMenu(const std::string_view message) {
    clear();
    write(UiColor::Title, "============================================================\n");
    write(UiColor::Title, "                 《燧火纪：部落黎明》\n");
    write(UiColor::Title, "============================================================\n");
    output_ << "你将领导整个燧火部落，在四年中经营、结盟或征服。\n\n"
            << "  1. 开始正式模式（16季，约60分钟）\n"
            << "  2. 开始快速展示模式（从第三年开始，8季）\n"
            << "  e. 大型扩展V1：苍林狩猎与可操控战斗\n"
            << "  7. V2快速战役（8季）\n"
            << "  8. V2课程战役（16季）\n"
            << "  9. V2长期战役（32季）\n"
            << "  3. 读取手动存档1\n"
            << "  4. 读取手动存档2\n"
            << "  5. 读取手动存档3\n"
            << "  6. 读取自动存档\n"
            << "  h. 查看新手帮助\n"
            << "  q. 退出\n"
            << "  扩展小队：expanded <2至8>\n"
            << "  V2命令：campaign quick|course|long / 战役 快速|课程|长期\n"
            << "  V2读档：v2load <1至6|auto> / 读取战役 <1至6|auto>\n"
            << "  高级复现：seed <数字> / quickseed <数字> / expandedseed <种子> <人数>\n"
            << "            campaignseed <quick|course|long> <种子>\n";
    if (!message.empty()) {
        output_ << '\n';
        write(UiColor::Warning, message);
        output_ << '\n';
    }
    output_ << "\n------------------------------------------------------------\n";
    prompt("请选择 > ");
}

void ConsoleUI::renderExpansion(const ExpansionGame& game, const std::string_view message) {
    clear();
    write(UiColor::Title, "=========== 《燧火纪：部落黎明》大型扩展 V1 ===========\n");
    output_ << game.lookText() << '\n'
            << "------------------------------------------------------------\n";
    if (!message.empty()) output_ << "最近消息：" << message << '\n';
    output_ << "------------------------------------------------------------\n"
            << "探索：move/移动 forest|deep|hunting|clearing  gather/采集 食物|草药|兽皮\n"
            << "遭遇：talk/交谈  trade/贸易  raid/劫掠  return/回营\n"
            << "战斗：attack/攻击  defend/防御  order/下令 集火|坚守|突进|后撤\n"
            << "物品：use/使用  loot/搜取  equip/装备 <栏位> <物品编号>\n"
            << "公共：look/查看  help/帮助  back/返回主菜单  quit/退出\n"
            << "说明：V1纵切用于试玩新系统，存档将在V2接入。\n";
    prompt();
}

void ConsoleUI::renderCampaign(const CampaignGame& game, const std::string_view message) {
    clear();
    const CampaignState& state = game.state();
    write(UiColor::Title, "================ 《燧火纪》V2部落战役 ================\n");
    output_ << CampaignGame::modeName(state.mode) << " | 季节 " << state.season << '/' << state.seasonLimit
            << " | " << CampaignGame::phaseName(state.phase) << " | 行动点 " << state.actionsLeft
            << " | 种子 " << state.seed << '\n'
            << "------------------------------------------------------------\n"
            << "部落 " << state.tribeName << "  首领 " << state.leaderName;
    if (!state.actingLeaderName.empty()) output_ << "（代理/继任 " << state.actingLeaderName << "）";
    output_ << "  人口 " << state.population << "  战士 " << state.warriors
            << "  稳定 " << state.stability << "  士气 " << state.morale
            << "  营地 " << state.campDurability << '\n';
    write(UiColor::Food, "食物 " + std::to_string(state.food));
    output_ << "  ";
    write(UiColor::Wood, "木材 " + std::to_string(state.wood));
    output_ << "  ";
    write(UiColor::Stone, "石料 " + std::to_string(state.stone));
    output_ << "  ";
    write(UiColor::Herbs, "草药 " + std::to_string(state.herbs));
    output_ << "  贝币 " << state.shells << (state.currencyUnlocked ? "（已流通）" : "（未解锁）") << '\n'
            << "地图 " << std::count(state.discovered.begin(), state.discovered.end(), true) << "/16"
            << "  建筑 " << std::count(state.buildings.begin(), state.buildings.end(), true) << "/6"
            << "  技术 " << std::count(state.technologies.begin(), state.technologies.end(), true) << "/9"
            << "  贸易 " << state.tradeCount << "  战争胜负 " << state.warsWon << '/' << state.warsLost << '\n'
            << "------------------------------------------------------------\n";

    for (std::size_t index = 1; index < kTribeV2Count; ++index) {
        const auto tribe = static_cast<TribeIdV2>(index);
        const DiplomacyRelation& relation = state.relations[index];
        const UiColor color = relation.atWar ? UiColor::Enemy
            : relation.alliance ? UiColor::Friendly : UiColor::Neutral;
        write(color, CampaignGame::tribeName(tribe) + " " + std::to_string(relation.relation));
        if (relation.atWar) output_ << "[战]";
        else if (relation.alliance) output_ << "[盟]";
        else if (relation.tradeRoute) output_ << "[商]";
        output_ << (index + 1U == kTribeV2Count ? '\n' : ' ');
    }

    if (!state.squads.empty()) {
        const PermanentSquad& squad = state.squads.front();
        output_ << squad.name << "：队长 " << squad.captain << "，" << squad.members.size()
                << "人，疲劳 " << squad.fatigue << "，精锐经验 " << squad.eliteExperience;
        if (squad.refusingOrders) output_ << " [抗命]";
        output_ << '\n';
    }
    if (state.phase == CampaignPhase::Mission && state.activeMission) {
        const ExpansionState& mission = *state.activeMission;
        output_ << "苍林任务：" << ExpansionGame::phaseName(mission.phase) << " / "
                << ExpansionGame::locationName(mission.location) << "，回合 " << mission.turn;
        if (mission.phase == ExpansionPhase::FrontlineCombat) {
            output_ << "，战线 " << mission.frontline << "/3，敌方生命 " << mission.enemyLife;
        }
        output_ << '\n';
    } else if (state.phase == CampaignPhase::War) {
        output_ << "部落战争：对" << CampaignGame::tribeName(state.war.enemy) << "，战线 "
                << state.war.front << "/3，己方战力 " << state.war.playerPower
                << "，敌方战力 " << state.war.enemyPower << '\n';
    } else if (state.phase == CampaignPhase::Finished) {
        write(UiColor::Warning, "结局已确定：" + CampaignGame::endingName(state.ending) + "\n");
    }

    output_ << "------------------------------------------------------------\n";
    if (!message.empty()) output_ << "最近消息：" << message << '\n';
    output_ << "------------------------------------------------------------\n";
    if (state.phase == CampaignPhase::Mission) {
        output_ << "任务：move/移动 gather/采集 talk/交谈 trade/贸易 raid/劫掠 attack/攻击\n"
                << "      defend/防御 order/下令 loot/搜取 return/回营 abort/放弃任务\n";
    } else if (state.phase == CampaignPhase::War) {
        output_ << "战争：attack/攻击 defend/防御 order/下令 <推进|坚守|集火|包抄|掩护|撤退> retreat/撤退\n";
    } else if (state.phase == CampaignPhase::EndingChoice) {
        output_ << "结算：objectives/目标  choose/选择 <alliance|conquest|prosperity|migration>\n";
    } else if (state.phase == CampaignPhase::Finished) {
        output_ << "结局：replay/重新播放  characters/人物  chronicle/编年史\n"
                << "      长期非覆灭结局可 sandbox/继续沙盒；也可 back/返回主菜单\n";
    } else {
        output_ << "1状态 2地图 3食物 4木材 5苍林任务 6外交 7小队 8结束季节 9帮助\n"
                << "经营：gather/采集 scout/侦察 build/建造 research/研究\n"
                << "外交：talk/交谈 gift/送礼 trade/贸易 openroute/开通商路 marry/联姻\n"
                << "内政战争：factions/派系 appease/安抚 formarmy/组建军队 war/出征\n";
    }
    output_ << "存档：save/保存 <1至6|auto>  load/读取 <1至6|auto>\n"
            << "公共：help/帮助  back/返回主菜单  quit/退出\n";
    prompt();
}

void ConsoleUI::renderGame(const GameEngine& engine, const std::string_view message) {
    clear();
    const GameState& state = engine.state();
    write(UiColor::Title, "================ 《燧火纪：部落黎明》 ================\n");
    output_ << modeName(state.mode) << " | 第" << yearForTurn(state.turn) << "年"
            << seasonName(seasonForTurn(state.turn)) << "季 | 季节 " << state.turn << "/16 | 小队 "
            << state.actionsLeft << '/' << teamsForPopulation(state.population) << " | 种子 " << state.seed << '\n';
    output_ << "------------------------------------------------------------\n";
    output_ << "人口 " << state.population << "  战士 " << state.warriors << "  士气 " << state.morale
            << "  营地 " << state.campDurability << "/20  防御 " << engine.permanentDefense()
            << "+" << state.temporaryDefense << '\n';
    write(UiColor::Food, "食物 " + std::to_string(state.food));
    output_ << "  ";
    write(UiColor::Wood, "木材 " + std::to_string(state.wood));
    output_ << "  ";
    write(UiColor::Stone, "石料 " + std::to_string(state.stone));
    output_ << "  ";
    write(UiColor::Herbs, "草药 " + std::to_string(state.herbs));
    output_ << "  建筑 " << engine.buildingCount() << "/6  技术 " << engine.technologyCount() << "/9\n";
    output_ << "------------------------------------------------------------\n";
    write(state.relations[indexOf(FactionId::RiverDeer)] >= 70 ? UiColor::Friendly : UiColor::Neutral,
        "河鹿 " + std::to_string(state.relations[indexOf(FactionId::RiverDeer)]));
    output_ << "  ";
    write(state.relations[indexOf(FactionId::WhiteFeather)] >= 70 ? UiColor::Friendly : UiColor::Neutral,
        "白羽 " + std::to_string(state.relations[indexOf(FactionId::WhiteFeather)]));
    output_ << "  ";
    write(UiColor::Enemy, "岩牙关系 " + std::to_string(state.relations[indexOf(FactionId::Rockfang)])
        + " / 战力 " + std::to_string(state.rockfangStrength));
    if (state.rockfangTruce) output_ << "（已停战）";
    if (state.rockfangFortCaptured) output_ << "（要塞已占领）";
    output_ << "\n------------------------------------------------------------\n";
    output_ << '[' << discoveredLabel(state, LocationId::WhiteFeatherCamp) << "]—["
            << discoveredLabel(state, LocationId::Marsh) << "]—[" << discoveredLabel(state, LocationId::Forest) << "]\n"
            << "       |                       |\n"
            << '[' << discoveredLabel(state, LocationId::RiverFord) << "]—["
            << discoveredLabel(state, LocationId::RedPlain) << "]—[" << discoveredLabel(state, LocationId::Camp) << "]\n"
            << "       |             |\n"
            << '[' << discoveredLabel(state, LocationId::OldPass) << "]—[" << discoveredLabel(state, LocationId::Quarry) << "]\n"
            << "       |\n";
    write(UiColor::Enemy, "[" + discoveredLabel(state, LocationId::RockfangFort) + "]\n");
    output_ << "------------------------------------------------------------\n";
    const auto& currentEvent = event(static_cast<EventId>(state.currentEvent));
    output_ << "当前事件【" << currentEvent.title << "】 " << currentEvent.description << '\n';
    if (!message.empty()) {
        output_ << "最近消息：" << message << '\n';
    }
    output_ << "------------------------------------------------------------\n";
    if (state.phase == Phase::AwaitingRaid) {
        write(UiColor::Enemy, "岩牙来袭：51正面 52伏击 53防守 54撤退\n");
    } else if (state.phase == Phase::FinalChoice) {
        write(UiColor::Warning, "最终议事：71联盟 72征服 73繁荣 74迁徙\n");
    } else {
        output_ << "1状态 2地图 3食物 4木材 5石料 6草药 7训练 8结束 9帮助 10鼓舞\n";
    }
    output_ << "保存：save/保存 1|2|3  读取：load/读取 1|2|3|auto  返回：back  退出：quit\n";
    prompt();
}

void ConsoleUI::showStandalone(const std::string_view title, const std::string_view text) {
    clear();
    write(UiColor::Title, "================ " + std::string(title) + " ================\n");
    output_ << text << "\n\n按 Enter 返回……";
}

void ConsoleUI::prompt(const std::string_view text) {
    output_ << text;
    output_.flush();
}

} // namespace tribe
