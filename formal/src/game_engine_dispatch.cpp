#include "tribe/game_engine.hpp"

#include "command_parser.hpp"
#include "game_command_catalog.hpp"
#include "game_engine_internal.hpp"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

namespace tribe {
namespace {

using command_parser::Command;
using game_command_catalog::CommandId;

/// 用途：构造只读命令的统一成功回执。输入：展示文本。输出：不提交、不消耗行动的回执。
/// 状态影响：无。失败：无。不变量：只读查询绝不标记 stateChanged。
ActionResult viewed(std::string text) { return {true, true, false, false, false, false, std::move(text)}; }

} // namespace

/// 用途：解析、分类并按阶段委派玩家命令。输入：原始终端文本。输出：对应行动回执。
/// 状态影响：实际修改仅发生在下游规则的 commit 中。失败：空白命令返回默认回执；不变量：阶段门禁先于规则执行。
ActionResult GameEngine::execute(const std::string_view input) {
    const Command command = command_parser::parse(input);
    if (command.verb.empty()) return {};

    const CommandId commandId = game_command_catalog::classify(command);
    if (state_.phase == GamePhase::Mission) return dispatchMissionCommand(command, commandId, input);
    if (state_.phase == GamePhase::War) return dispatchWarCommand(command, commandId);
    return dispatchManagingCommand(command, commandId);
}

/// 用途：处理活动地图阶段的中止或转交命令。输入：解析命令、分类和原始文本。输出：地图行动回执。
/// 状态影响：中止时经 commit 清空活动任务；其他命令由地图引擎原子处理。失败：地图规则拒绝时不改变部落状态。
ActionResult GameEngine::dispatchMissionCommand(const Command& command, const CommandId commandId,
                                                const std::string_view input) {
    if (commandId == CommandId::Abort && game_command_catalog::hasArity(command, 0U)) {
        GameState candidate = state_;
        if (!candidate.squads.empty()) candidate.squads.front().station = WorldLocationId::Camp;
        candidate.activeMission.reset();
        candidate.phase = GamePhase::Managing;
        candidate.stability = std::max(0, candidate.stability - 2);
        addChronicle(candidate, 1, "任务中止", "小队提前返回，部落稳定略有下降。");
        return commit(std::move(candidate), "小队放弃当前载货并返回营地。", false);
    }
    return executeMission(input);
}

/// 用途：处理战争阶段的军令、攻防和只读状态。输入：解析命令和分类。输出：战争行动回执。
/// 状态影响：攻击、防御、撤退与军令各自通过战争规则提交候选状态。失败：任何其他命令都被阶段门禁拒绝。
ActionResult GameEngine::dispatchWarCommand(const Command& command, const CommandId commandId) {
    if (commandId == CommandId::Order && game_command_catalog::hasArity(command, 1U)) {
        const auto order = game_engine_detail::parseWarOrder(command.args.front());
        return order ? setWarOrder(*order) : rejected("未知军令：推进、坚守、集火、包抄、掩护、撤退。");
    }
    if (commandId == CommandId::Attack && game_command_catalog::hasArity(command, 0U)) return warAttack();
    if (commandId == CommandId::Defend && game_command_catalog::hasArity(command, 0U)) return warDefend();
    if (commandId == CommandId::Retreat && game_command_catalog::hasArity(command, 0U)) return warRetreat();
    if ((commandId == CommandId::Status || commandId == CommandId::Look) &&
        game_command_catalog::hasArity(command, 0U)) {
        std::ostringstream output;
        output << "战争：对" << tribeName(state_.war.enemy) << "，己方战力" << state_.war.playerPower << "，敌方战力"
               << state_.war.enemyPower << "，军令" << game_engine_detail::warOrderName(state_.war.order) << "。";
        return viewed(output.str());
    }
    return rejected("战争中可用：攻击、防御、下令、撤退、状态。");
}

/// 用途：判断人口待重分配时命令能否降低人口占用。输入：解析命令及分类。输出：是否允许进入规则层。
/// 状态影响：无。失败：不符合严格参数形状时返回 false。不变量：不能借由其他命令绕开人口不变量。
bool GameEngine::allowsWorkforceRecovery(const Command& command, const CommandId commandId) const {
    if (commandId == CommandId::Assign && game_command_catalog::hasArity(command, 2U))
        return game_engine_detail::parseWorkforceRole(command.args[0]).has_value();
    if (commandId == CommandId::Assign && game_command_catalog::hasArity(command, 3U))
        return command_parser::equalsAny(command.args[0], {"outpost", "前哨"});
    if (commandId == CommandId::Garrison && game_command_catalog::hasArity(command, 2U)) return true;
    if (commandId == CommandId::DisbandArmy && game_command_catalog::hasArity(command, 0U)) return true;
    return commandId == CommandId::Event && game_command_catalog::hasArity(command, 0U);
}

/// 用途：处理经营、结局与沙盒阶段的命令。输入：解析命令和唯一分类。输出：对应行动回执。
/// 状态影响：每项规则仍只经 commit 修改状态。失败：错误参数、待决事件或人口门禁均返回原有拒绝文本并保持状态不变。
ActionResult GameEngine::dispatchManagingCommand(const Command& command, const CommandId commandId) {
    using namespace game_engine_detail;

    switch (commandId) {
        case CommandId::Status:
            return game_command_catalog::hasArity(command, 0U) ? viewed(statusText()) : rejected("用法：status / 状态");
        case CommandId::Map:
            return game_command_catalog::hasArity(command, 0U) ? viewed(worldText()) : rejected("用法：map / 地图");
        case CommandId::Diplomacy:
            return game_command_catalog::hasArity(command, 0U) ? viewed(diplomacyText())
                                                               : rejected("用法：diplomacy / 外交");
        case CommandId::Factions:
            return game_command_catalog::hasArity(command, 0U) ? viewed(factionText())
                                                               : rejected("用法：factions / 派系");
        case CommandId::Squad:
            if (command.args.size() >= 3U && command_parser::equalsAny(command.args.front(), {"configure", "配置"}))
                return configureSquad(std::vector<std::string>(command.args.begin() + 1, command.args.end()));
            break;
        case CommandId::Squads:
            return game_command_catalog::hasArity(command, 0U) ? viewed(squadText()) : rejected("用法：squads / 小队");
        case CommandId::Objectives:
            return game_command_catalog::hasArity(command, 0U) ? viewed(objectiveText())
                                                               : rejected("用法：objectives / 目标");
        case CommandId::Chronicle:
            return game_command_catalog::hasArity(command, 0U) ? viewed(chronicleText())
                                                               : rejected("用法：chronicle / 编年史");
        case CommandId::Help:
            return game_command_catalog::hasArity(command, 0U) ? viewed(helpText()) : rejected("用法：help / 帮助");
        default:
            break;
    }

    if (state_.phase == GamePhase::Finished) {
        if (commandId == CommandId::Sandbox && game_command_catalog::hasArity(command, 0U)) return continueSandbox();
        return rejected("结局已经确定。长期模式可输入 sandbox / 继续沙盒。");
    }
    if (state_.phase == GamePhase::EndingChoice) {
        if (commandId == CommandId::Choose && game_command_catalog::hasArity(command, 1U)) {
            const auto ending = parseGameEnding(command.args.front());
            return ending ? chooseEnding(*ending) : rejected("未知结局道路。");
        }
        return rejected("当前必须先查看目标并选择结局：choose <alliance|conquest|prosperity|migration>。");
    }
    if (commandId == CommandId::Choose) {
        // 结局只能在时代结算阶段选择；提前说明下一步，避免玩家误以为该命令不存在。
        return rejected("现在还不能选择结局；行动完成后输入 endturn / 结束回合 进入结局议事。");
    }
    if (state_.workforceReassignmentRequired && !allowsWorkforceRecovery(command, commandId)) {
        return rejected("人口已不足以维持现有岗位。请降低劳力或驻军，或用 disbandarmy / 解散军队释放军队后再行动。");
    }
    if (state_.pendingEvent.active && !state_.workforceReassignmentRequired && commandId != CommandId::Event) {
        return rejected("本季有待决事件；请先输入 event 查看并选择 event <1|2>。 ");
    }

    switch (commandId) {
        case CommandId::Build:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto building = parseBuilding(command.args.front());
                return building ? build(*building) : rejected("未知建筑。");
            }
            break;
        case CommandId::Research:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto technology = parseTechnology(command.args.front());
                return technology ? research(*technology) : rejected("未知技术。");
            }
            break;
        case CommandId::Mission:
            // 数字快捷键“5”原本只接受无参数；仅 mission/出任务允许带资源类型。
            if (command.verb != "5" && command.args.size() <= 1U) {
                if (command.args.empty() ||
                    command_parser::equalsAny(command.args.front(), {"world", "map", "地图", "探索"}))
                    return startMission();
                if (command_parser::equalsAny(command.args.front(), {"outpost", "前哨"})) return startOutpostMission();
                const auto resource = parseResource(command.args.front());
                return resource ? startMission(*resource) : rejected("任务类型：食物、木材、石料、草药或兽皮。");
            }
            if (command.verb == "5" && command.args.empty()) return startMission();
            break;
        case CommandId::Workforce:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(workforceText());
            break;
        case CommandId::Assign:
            if (command.args.size() == 3U && command_parser::equalsAny(command.args[0], {"outpost", "前哨"})) {
                int count = 0;
                const auto location = parseLocation(command.args[1]);
                return location && parseNonnegative(command.args[2], count)
                           ? assignOutpostGuards(*location, count)
                           : rejected("用法：assign outpost <地点> <人数>。");
            }
            if (game_command_catalog::hasArity(command, 2U)) {
                int count = 0;
                const auto role = parseWorkforceRole(command.args[0]);
                return role && parseNonnegative(command.args[1], count) ? assignWorkforce(*role, count)
                                                                        : rejected("用法：assign <岗位> <人数>。");
            }
            break;
        case CommandId::Inventory:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(inventoryText());
            break;
        case CommandId::People:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(peopleText());
            break;
        case CommandId::Person:
            // “人物”兼容旧版无参数总览；英文 person 仍必须带姓名，保持旧命令契约。
            if (command.verb == "人物" && game_command_catalog::hasArity(command, 0U)) return viewed(peopleText());
            if (game_command_catalog::hasArity(command, 1U)) return viewed(personText(command.args[0]));
            break;
        case CommandId::Buildings:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(buildingsText());
            break;
        case CommandId::Technologies:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(technologiesText());
            break;
        case CommandId::Craft:
            if (game_command_catalog::hasArity(command, 1U)) return craft(command.args[0]);
            break;
        case CommandId::Repair:
            if (game_command_catalog::hasArity(command, 1U)) return repair(command.args[0]);
            break;
        case CommandId::Scrap:
            if (game_command_catalog::hasArity(command, 1U)) return scrap(command.args[0]);
            break;
        case CommandId::Equip:
            if (game_command_catalog::hasArity(command, 3U))
                return equipPerson(command.args[0], command.args[1], command.args[2]);
            break;
        case CommandId::Unequip:
            if (game_command_catalog::hasArity(command, 2U)) return unequipPerson(command.args[0], command.args[1]);
            break;
        case CommandId::Appoint:
            if (game_command_catalog::hasArity(command, 2U)) return appoint(command.args[0], command.args[1]);
            break;
        case CommandId::Unappoint:
            if (game_command_catalog::hasArity(command, 1U)) return unappoint(command.args[0]);
            break;
        case CommandId::Treat:
            if (game_command_catalog::hasArity(command, 1U)) return treat(command.args[0]);
            break;
        case CommandId::War:
            if (game_command_catalog::hasArity(command, 1U) &&
                command_parser::equalsAny(command.args[0], {"targets", "目标"}))
                return viewed(warTargetsText());
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? startWar(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::WarTargets:
            if (game_command_catalog::hasArity(command, 1U) &&
                command_parser::equalsAny(command.args[0], {"targets", "目标"}))
                return viewed(warTargetsText());
            break;
        case CommandId::Power:
            if (game_command_catalog::hasArity(command, 0U)) return viewed(powerText());
            break;
        case CommandId::Garrison:
            if (game_command_catalog::hasArity(command, 2U)) {
                int count = 0;
                const auto tribe = parseTribe(command.args[0]);
                return tribe && parseNonnegative(command.args[1], count) ? garrison(*tribe, count)
                                                                         : rejected("用法：garrison <部落> <战士数>。");
            }
            break;
        case CommandId::Event:
            if (command.args.empty()) return viewed(eventText(state_));
            if (game_command_catalog::hasArity(command, 1U)) {
                int option = 0;
                return parseNonnegative(command.args[0], option) ? chooseEvent(option)
                                                                 : rejected("用法：event <1|2>。");
            }
            return rejected("用法：event <1|2>。");
        case CommandId::SquadRest:
            if (game_command_catalog::hasArity(command, 0U)) return restSquad();
            break;
        case CommandId::Talk:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? talk(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Gift:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? gift(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Trade:
            if (game_command_catalog::hasArity(command, 3U)) {
                const auto tribe = parseTribe(command.args[0]);
                const auto offered = parseResource(command.args[1]);
                const auto requested = parseResource(command.args[2]);
                return tribe && offered && requested ? trade(*tribe, *offered, *requested)
                                                     : rejected("用法：trade <部落> <给出的资源> <换取的资源>。");
            }
            break;
        case CommandId::OpenRoute:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? openTradeRoute(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Marry:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? marriage(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Tribute:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? offerTribute(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Demand:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? demandTribute(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Ally:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? alliance(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Declare:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? declareWar(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Truce:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? negotiateTruce(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Raid:
            if (game_command_catalog::hasArity(command, 1U)) {
                const auto tribe = parseTribe(command.args.front());
                return tribe ? raid(*tribe) : rejected("未知部落。");
            }
            break;
        case CommandId::Appease:
            if (game_command_catalog::hasArity(command, 1U)) {
                int faction = 0;
                if (!parseNonnegative(command.args.front(), faction) || faction < 1 ||
                    faction > static_cast<int>(kPlayerFactionCount))
                    return rejected("派系编号为1至3。");
                return appeaseFaction(static_cast<std::size_t>(faction - 1));
            }
            break;
        case CommandId::FormArmy:
            if (command.args.size() == 2U || command.args.size() == 3U) {
                int warriors = 0;
                int militia = 0;
                return parseNonnegative(command.args[command.args.size() - 2U], warriors) &&
                               parseNonnegative(command.args.back(), militia)
                           ? formArmy(warriors, militia, command.args.size() == 3U ? command.args[0] : "")
                           : rejected("用法：formarmy <统帅> <战士数> <民兵数>。");
            }
            break;
        case CommandId::DisbandArmy:
            if (game_command_catalog::hasArity(command, 0U)) return disbandArmy();
            break;
        case CommandId::EndTurn:
            return game_command_catalog::hasArity(command, 0U) ? endSeason() : rejected("用法：endturn / 结束回合");
        default:
            break;
    }
    return {};
}

} // namespace tribe
