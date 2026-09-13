#include "tribe/game_engine.hpp"

#include "command_parser.hpp"
#include "game_engine_internal.hpp"
#include "population_rules.hpp"
#include "seasonal_event_rules.hpp"
#include "state_safety.hpp"
#include "war_rules.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace tribe {

using namespace game_engine_detail;

using command_parser::Command;
using command_parser::equalsAny;
using command_parser::parse;
using command_parser::verbIs;

GameEngine::GameEngine(GameConfig config) {
    state_.mode = config.mode;
    state_.seed = config.seed;
    state_.tribeName = config.tribeName.empty() ? "燧火" : std::move(config.tribeName);
    state_.leaderName = config.leaderName.empty() ? "炎角" : std::move(config.leaderName);
    state_.leaderFocus = config.leaderFocus.empty() ? "生存" : std::move(config.leaderFocus);
    if (state_.mode == GameMode::Quick) {
        state_.season = 1;
        state_.seasonLimit = 8;
        state_.food = 42;
        state_.wood = 24;
        state_.stone = 12;
        state_.warriors = 5;
    } else if (state_.mode == GameMode::Long) {
        state_.seasonLimit = 32;
        state_.food = 20;
    } else {
        state_.food = 20;
    }
    state_.discovered[indexOf(WorldLocationId::Camp)] = true;
    state_.discovered[indexOf(WorldLocationId::Forest)] = true;
    state_.discovered[indexOf(WorldLocationId::RedPlain)] = true;
    state_.outposts[indexOf(WorldLocationId::Camp)] = true;

    state_.tribes[indexOf(TribeId::Player)] = {TribeId::Player,
                                               state_.tribeName,
                                               state_.leaderName,
                                               "",
                                               "青枝",
                                               state_.leaderFocus,
                                               {{"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
                                                {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
                                                {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::RiverDeer)] = {TribeId::RiverDeer,
                                                  "河鹿",
                                                  "牧河",
                                                  "",
                                                  "禾角",
                                                  "务实农业",
                                                  {{"农耕者", 50, 65, "稳定粮食", "禾角", FactionCrisis::Calm},
                                                   {"渡口商人", 30, 55, "扩大贸易", "舟苇", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::WhiteFeather)] = {TribeId::WhiteFeather,
                                                     "白羽",
                                                     "羽医",
                                                     "",
                                                     "轻翎",
                                                     "谨慎救助",
                                                     {{"医者", 45, 65, "救助伤者", "轻翎", FactionCrisis::Calm},
                                                      {"远望者", 35, 60, "共享情报", "苍羽", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Rockfang)] = {TribeId::Rockfang,
                                                 "岩牙",
                                                 "赤獠",
                                                 "",
                                                 "黑牙",
                                                 "强硬好战",
                                                 {{"战团", 55, 60, "取得战利品", "黑牙", FactionCrisis::Calm},
                                                  {"矿奴监工", 25, 45, "控制矿路", "裂石", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Tidesalt)] = {TribeId::Tidesalt,
                                                 "潮盐",
                                                 "澜母",
                                                 "",
                                                 "潮舟",
                                                 "精明航运",
                                                 {{"船主", 45, 60, "保护航路", "潮舟", FactionCrisis::Calm},
                                                  {"盐工", 35, 55, "提高盐价", "白沫", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Blackstone)] = {TribeId::Blackstone,
                                                   "玄石",
                                                   "玄砧",
                                                   "",
                                                   "黑炉",
                                                   "冷静工艺",
                                                   {{"工匠", 45, 60, "换取粮食", "黑炉", FactionCrisis::Calm},
                                                    {"雇佣战士", 35, 50, "获得装备", "玄盾", FactionCrisis::Calm}}};

    state_.relations[indexOf(TribeId::RiverDeer)] = {20, 15, 0, 0};
    state_.relations[indexOf(TribeId::WhiteFeather)] = {5, 5, 0, 0};
    state_.relations[indexOf(TribeId::Rockfang)] = {-40, 0, 35, 0};
    state_.relations[indexOf(TribeId::Tidesalt)] = {0, 0, 0, 0};
    state_.relations[indexOf(TribeId::Blackstone)] = {0, 0, 5, 0};
    state_.playerFactions = {{
        {"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
        {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
        {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm},
    }};

    state_.roster = {
        makeCampaignCharacter("青枝", Occupation::Envoy),  makeCampaignCharacter("石刃", Occupation::Warrior),
        makeCampaignCharacter("苍眼", Occupation::Scout),  makeCampaignCharacter("白榆", Occupation::Healer),
        makeCampaignCharacter("逐鹿", Occupation::Hunter), makeCampaignCharacter("岩槌", Occupation::Crafter),
        makeCampaignCharacter("芦风", Occupation::Hunter), makeCampaignCharacter("河矛", Occupation::Warrior),
    };
    const OperationResult equipped = equipItem(state_.roster.front(), EquipmentSlot::MainHand, makeCampaignLeaderBow());
    if (!equipped) throw std::logic_error("长期人物初始装备失败：" + equipped.message);
    state_.squads.push_back({"晨火队", "青枝", {"青枝", "石刃", "苍眼", "芦风"}, 0, 0, false, false});
    state_.leadershipHistory.push_back(state_.leaderName + "（初代首领）");
    addChronicle(state_, 3, "燧火新议", state_.leaderName + "召集族人，决定走向更广阔的世界。");

    std::string error;
    if (!validateState(state_, error)) throw std::logic_error("游戏初始状态无效：" + error);
}

GameEngine::GameEngine(GameState state) : state_(std::move(state)) {
    std::string error;
    if (!validateState(state_, error)) throw std::invalid_argument("游戏状态无效：" + error);
}

ActionResult GameEngine::execute(const std::string_view input) {
    const Command command = parse(input);
    if (command.verb.empty()) return {};

    if (state_.phase == GamePhase::Mission) {
        if (verbIs(command, {"abort", "放弃任务"}) && command.args.empty()) {
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
    if (state_.phase == GamePhase::War) {
        if (verbIs(command, {"order", "下令"}) && command.args.size() == 1U) {
            const auto order = parseWarOrder(command.args.front());
            return order ? setWarOrder(*order) : rejected("未知军令：推进、坚守、集火、包抄、掩护、撤退。");
        }
        if (verbIs(command, {"attack", "攻击"}) && command.args.empty()) return warAttack();
        if (verbIs(command, {"defend", "防御"}) && command.args.empty()) return warDefend();
        if (verbIs(command, {"retreat", "撤退"}) && command.args.empty()) return warRetreat();
        if (verbIs(command, {"status", "状态", "look", "查看"}) && command.args.empty()) {
            std::ostringstream output;
            output << "战争：对" << tribeName(state_.war.enemy) << "，己方战力" << state_.war.playerPower
                   << "，敌方战力" << state_.war.enemyPower << "，军令" << warOrderName(state_.war.order) << "。";
            return {true, true, false, false, false, false, output.str()};
        }
        return rejected("战争中可用：攻击、防御、下令、撤退、状态。");
    }

    if (verbIs(command, {"status", "状态"}) || command.verb == "1") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, statusText()}
                                    : rejected("用法：status / 状态");
    }
    if (verbIs(command, {"map", "地图"}) || command.verb == "2") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, worldText()}
                                    : rejected("用法：map / 地图");
    }
    if (verbIs(command, {"diplomacy", "外交"}) || command.verb == "6") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, diplomacyText()}
                                    : rejected("用法：diplomacy / 外交");
    }
    if (verbIs(command, {"factions", "派系", "稳定"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, factionText()}
                                    : rejected("用法：factions / 派系");
    }
    if (verbIs(command, {"squad", "小队配置"}) && command.args.size() >= 3U &&
        equalsAny(command.args.front(), {"configure", "配置"})) {
        return configureSquad(std::vector<std::string>(command.args.begin() + 1, command.args.end()));
    }
    if (verbIs(command, {"squads", "小队"}) || command.verb == "7") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, squadText()}
                                    : rejected("用法：squads / 小队");
    }
    if (verbIs(command, {"objectives", "目标"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, objectiveText()}
                                    : rejected("用法：objectives / 目标");
    }
    if (verbIs(command, {"chronicle", "编年史"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, chronicleText()}
                                    : rejected("用法：chronicle / 编年史");
    }
    if (verbIs(command, {"help", "帮助"}) || command.verb == "9") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, helpText()}
                                    : rejected("用法：help / 帮助");
    }
    if (state_.phase == GamePhase::Finished) {
        if (verbIs(command, {"sandbox", "继续沙盒"}) && command.args.empty()) return continueSandbox();
        return rejected("结局已经确定。长期模式可输入 sandbox / 继续沙盒。");
    }
    if (state_.phase == GamePhase::EndingChoice) {
        if ((state_.pendingEvent.active || !state_.pendingEvents.empty()) && verbIs(command, {"event", "事件"})) {
            if (command.args.empty()) return {true, true, false, false, false, false, eventText(state_)};
            int option = 0;
            return command.args.size() == 1U && parseNonnegative(command.args[0], option) ? chooseEvent(option)
                                                                                          : rejected("用法：event <1|2>。 ");
        }
        if (verbIs(command, {"choose", "选择"}) && command.args.size() == 1U) {
            const auto ending = parseGameEnding(command.args.front());
            return ending ? chooseEnding(*ending) : rejected("未知结局道路。");
        }
        return rejected("当前必须先查看目标并选择结局：choose <alliance|conquest|prosperity|migration>。");
    }
    if (state_.workforceReassignmentRequired) {
        const bool loweringRole = verbIs(command, {"assign", "分配"}) && command.args.size() == 2U &&
                                  parseWorkforceRole(command.args[0]).has_value();
        const bool loweringOutpost = verbIs(command, {"assign", "分配"}) && command.args.size() == 3U &&
                                     equalsAny(command.args[0], {"outpost", "前哨"});
        const bool loweringGarrison = verbIs(command, {"garrison", "驻军"}) && command.args.size() == 2U;
        const bool releasingArmy = verbIs(command, {"disbandarmy", "解散军队"}) && command.args.empty();
        const bool viewingEvent = verbIs(command, {"event", "事件"}) && command.args.empty();
        if (!loweringRole && !loweringOutpost && !loweringGarrison && !releasingArmy && !viewingEvent) {
            return rejected(
                "人口已不足以维持现有岗位。请降低劳力或驻军，或用 disbandarmy / 解散军队释放军队后再行动。");
        }
    }
    if ((state_.pendingEvent.active || !state_.pendingEvents.empty()) && !state_.workforceReassignmentRequired &&
        !verbIs(command, {"event", "事件"})) {
        return rejected("本季有待决事件；请先输入 event 查看并选择 event <1|2>，处理完全部事件后才能继续经营。 ");
    }

    if (verbIs(command, {"build", "建造"}) && (command.args.size() == 1U || command.args.size() == 2U)) {
        const auto building = parseBuilding(command.args.front());
        int workers = 2;
        if (command.args.size() == 2U && !parseNonnegative(command.args.back(), workers))
            return rejected("用法：build/建造 <建筑> [投入人数]。 ");
        return building ? build(*building, workers) : rejected("未知建筑。");
    }
    if (verbIs(command, {"research", "研究"}) && (command.args.size() == 1U || command.args.size() == 2U)) {
        const auto technology = parseTechnology(command.args.front());
        int workers = 0;
        if (command.args.size() == 2U && !parseNonnegative(command.args.back(), workers))
            return rejected("用法：research/研究 <技术> [投入人数]。 ");
        return technology ? research(*technology, workers) : rejected("未知技术。");
    }
    if (verbIs(command, {"mission", "出任务"}) && command.args.size() <= 2U) {
        if (command.args.empty() || equalsAny(command.args.front(), {"world", "map", "地图", "探索"}))
            return startMission();
        if (equalsAny(command.args.front(), {"outpost", "前哨"})) {
            int people = 0;
            if (command.args.size() == 2U && !parseNonnegative(command.args.back(), people))
                return rejected("用法：mission outpost [人数]。 ");
            return startMission(MissionKind::OutpostConstruction, ResourceKind::Wood, people);
        }
        const auto resource = parseResource(command.args.front());
        if (!resource) return rejected("任务用法：mission <食物|木材|石料|草药|兽皮> <人数>。 ");
        int people = 0;
        if (command.args.size() == 2U && !parseNonnegative(command.args.back(), people))
            return rejected("任务人数必须是非负整数。 ");
        return startMission(*resource, people);
    }
    if (command.verb == "5" && command.args.empty()) return startMission();
    if ((verbIs(command, {"workforce", "劳力"}) || command.verb == "3") && command.args.empty())
        return {true, true, false, false, false, false, workforceText()};
    if (verbIs(command, {"assign", "分配"}) && command.args.size() == 3U &&
        equalsAny(command.args[0], {"outpost", "前哨"})) {
        int count = 0;
        const auto location = parseLocation(command.args[1]);
        return location && parseNonnegative(command.args[2], count) ? assignOutpostGuards(*location, count)
                                                                    : rejected("用法：assign outpost <地点> <人数>。");
    }
    if (verbIs(command, {"assign", "分配"}) && command.args.size() == 2U) {
        int count = 0;
        const auto role = parseWorkforceRole(command.args[0]);
        return role && parseNonnegative(command.args[1], count) ? assignWorkforce(*role, count)
                                                                : rejected("用法：assign <岗位> <人数>。");
    }
    if ((verbIs(command, {"inventory", "仓库"}) || command.verb == "4") && command.args.empty())
        return {true, true, false, false, false, false, inventoryText()};
    if (verbIs(command, {"people", "人物"}) && command.args.empty())
        return {true, true, false, false, false, false, peopleText()};
    if (verbIs(command, {"person", "人物"}) && command.args.size() == 1U)
        return {true, true, false, false, false, false, personText(command.args[0])};
    if (verbIs(command, {"buildings", "建筑清单"}) && command.args.empty())
        return {true, true, false, false, false, false, buildingsText()};
    if (verbIs(command, {"technologies", "技术清单"}) && command.args.empty())
        return {true, true, false, false, false, false, technologiesText()};
    if (verbIs(command, {"craft", "制造"}) && command.args.size() == 1U) return craft(command.args[0]);
    if (verbIs(command, {"repair", "维修"}) && command.args.size() == 1U) return repair(command.args[0]);
    if (verbIs(command, {"scrap", "报废"}) && command.args.size() == 1U) return scrap(command.args[0]);
    if (verbIs(command, {"equip", "装备"}) && command.args.size() == 3U)
        return equipPerson(command.args[0], command.args[1], command.args[2]);
    if (verbIs(command, {"unequip", "卸下"}) && command.args.size() == 2U)
        return unequipPerson(command.args[0], command.args[1]);
    if (verbIs(command, {"appoint", "任命"}) && command.args.size() == 2U)
        return appoint(command.args[0], command.args[1]);
    if (verbIs(command, {"unappoint", "卸任"}) && command.args.size() == 1U) return unappoint(command.args[0]);
    if (verbIs(command, {"treat", "医治"}) && command.args.size() == 1U) return treat(command.args[0]);
    if (verbIs(command, {"war", "战争"}) && command.args.size() == 1U &&
        equalsAny(command.args[0], {"targets", "目标"}))
        return {true, true, false, false, false, false, warTargetsText()};
    if (verbIs(command, {"power", "战力"}) && command.args.empty())
        return {true, true, false, false, false, false, powerText()};
    if (verbIs(command, {"garrison", "驻军"}) && command.args.size() == 2U) {
        int count = 0;
        const auto tribe = parseTribe(command.args[0]);
        return tribe && parseNonnegative(command.args[1], count) ? garrison(*tribe, count)
                                                                 : rejected("用法：garrison <部落> <战士数>。");
    }
    if (verbIs(command, {"event", "事件"})) {
        if (command.args.empty()) return {true, true, false, false, false, false, eventText(state_)};
        int option = 0;
        return command.args.size() == 1U && parseNonnegative(command.args[0], option) ? chooseEvent(option)
                                                                                      : rejected("用法：event <1|2>。");
    }
    if (verbIs(command, {"squadrest", "小队休整"}) && command.args.empty()) return restSquad();

    if (verbIs(command, {"talk", "交谈"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? talk(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"gift", "送礼"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? gift(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"trade", "贸易"}) && command.args.size() == 3U) {
        const auto tribe = parseTribe(command.args[0]);
        const auto offered = parseResource(command.args[1]);
        const auto requested = parseResource(command.args[2]);
        return tribe && offered && requested ? trade(*tribe, *offered, *requested)
                                             : rejected("用法：trade <部落> <给出的资源> <换取的资源>。");
    }
    if (verbIs(command, {"openroute", "开通商路"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? openTradeRoute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"marry", "联姻"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? marriage(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"tribute", "朝贡"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? offerTribute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"demand", "索贡"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? demandTribute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"ally", "结盟"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? alliance(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"declare", "宣战"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? declareWar(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"truce", "停战"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? negotiateTruce(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"raid", "劫掠"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? raid(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"appease", "安抚"}) && command.args.size() == 1U) {
        int faction = 0;
        if (!parseNonnegative(command.args.front(), faction) || faction < 1 ||
            faction > static_cast<int>(kPlayerFactionCount))
            return rejected("派系编号为1至3。");
        return appeaseFaction(static_cast<std::size_t>(faction - 1));
    }
    if (verbIs(command, {"formarmy", "组建军队", "组军"}) && (command.args.size() == 2U || command.args.size() == 3U)) {
        int warriors = 0;
        int militia = 0;
        return parseNonnegative(command.args[command.args.size() - 2U], warriors) &&
                       parseNonnegative(command.args.back(), militia)
                   ? formArmy(warriors, militia, command.args.size() == 3U ? command.args[0] : "")
                   : rejected("用法：formarmy <统帅> <战士数> <民兵数>。");
    }
    if (verbIs(command, {"formarmy", "组建军队", "组军"}))
        return rejected("用法：组建军队 <统帅> <战士数> <民兵数>；也可省略统帅：组建军队 <战士数> <民兵数>。 ");
    if (verbIs(command, {"disbandarmy", "解散军队"}) && command.args.empty()) return disbandArmy();
    if (verbIs(command, {"war", "出征"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? startWar(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"endturn", "end", "结束回合"}) || command.verb == "8") {
        return command.args.empty() ? endSeason() : rejected("用法：endturn / 结束回合");
    }
    return {};
}

bool GameEngine::canSpendAction(ActionResult& result, const int cost) const {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox) {
        result = rejected("当前阶段不能执行部落行动。");
        return false;
    }
    if (cost <= 0 || cost > 7) {
        result = rejected("行动力投入必须为1至7点。");
        return false;
    }
    if (state_.actionsLeft < cost) {
        result = rejected("本季剩余行动力不足（需要" + std::to_string(cost) + "，剩余" +
                          std::to_string(state_.actionsLeft) + "）。请结束季节或减少投入人数。");
        return false;
    }
    if (cost > population_rules::availablePopulation(state_)) {
        result = rejected("可用人口不足，无法投入" + std::to_string(cost) + "人。");
        return false;
    }
    return true;
}

ActionResult GameEngine::commit(GameState candidate, std::string message, const bool consumesAction,
                                const bool seasonAdvanced, const bool endingReached) {
    // 1. 根据候选状态重新计算派生的人口待重分配标志，避免调用方遗漏这一派生字段。
    candidate.actionsLeft = std::min(candidate.actionsLeft, population_rules::actionCapacity(candidate));
    population_rules::refreshWorkforceReassignment(candidate);
    // 2. 在移动任何数据前统一验证文本、人物、任务、装备和人口不变量；失败直接丢弃 candidate。
    std::string error;
    if (!validateState(candidate, error)) return rejected("行动后的状态未通过校验，已原子取消：" + error);
    // 3. 校验成功后才执行唯一一次状态替换，因此失败路径绝不会污染已提交的 state_。
    state_ = std::move(candidate);
    return {true, true, true, consumesAction, seasonAdvanced, endingReached, std::move(message)};
}

ActionResult GameEngine::rejected(std::string message) const {
    return {true, false, false, false, false, false, std::move(message)};
}

void GameEngine::addChronicle(GameState& candidate, const int importance, std::string title, std::string detail) const {
    candidate.chronicle.push_back(
        {candidate.season, std::clamp(importance, 1, 5), std::move(title), std::move(detail)});
    if (candidate.chronicle.size() > 200U) candidate.chronicle.erase(candidate.chronicle.begin());
}

int GameEngine::availableTeams(const GameState& state) const {
    return population_rules::actionCapacity(state);
}

WorldLocationId GameEngine::contactLocation(const TribeId tribe) const {
    switch (tribe) {
        case TribeId::RiverDeer:
            return WorldLocationId::RiverFord;
        case TribeId::WhiteFeather:
            return WorldLocationId::WhiteFeatherCamp;
        case TribeId::Rockfang:
            return WorldLocationId::OldPass;
        case TribeId::Tidesalt:
            return WorldLocationId::TidesaltHarbor;
        case TribeId::Blackstone:
            return WorldLocationId::BlackstoneWorkshop;
        case TribeId::Player:
        case TribeId::Count:
            return WorldLocationId::MountainMarket;
    }
    return WorldLocationId::MountainMarket;
}

bool GameEngine::locationDiscovered(const GameState& state, const WorldLocationId location) const {
    return state.discovered[indexOf(location)];
}

} // namespace tribe
