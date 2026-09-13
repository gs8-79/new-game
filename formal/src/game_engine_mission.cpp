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
ActionResult GameEngine::startMission(const ResourceKind resource, const int people) {
    return startMission(MissionKind::Gather, resource, people);
}

ActionResult GameEngine::startOutpostMission() {
    return startMission(MissionKind::OutpostConstruction, ResourceKind::Wood, 0);
}

ActionResult GameEngine::startMission(const MissionKind kind, const ResourceKind resource, const int people) {
    ActionResult result;
    if (state_.squads.empty()) return rejected("当前没有可出发的小队。");
    const bool constructionMission = kind == MissionKind::OutpostConstruction;
    const int legacyCrew = resource == ResourceKind::Food ? state_.workforce.foodCrew
                           : resource == ResourceKind::Wood ? state_.workforce.woodCrew
                           : resource == ResourceKind::Stone ? state_.workforce.stoneCrew
                           : resource == ResourceKind::Herbs ? state_.workforce.herbCrew
                                                             : state_.workforce.foodCrew;
    const int crewSize = people == 0 ? std::max(2, legacyCrew) : people;
    if (crewSize < 2 || crewSize > static_cast<int>(kMaximumSquadSize))
        return rejected("任务派出人数必须为2至8人；用法：mission <资源> <人数>。 ");
    if (crewSize > static_cast<int>(state_.squads.front().members.size()))
        return rejected("派出人数不能超过当前小队人数，请先查看 squads / 小队。 ");
    if (!canSpendAction(result, crewSize)) return result;
    if (crewSize > population_rules::availablePopulation(state_))
        return rejected("可用人口不足，无法派出这么多人。");
    if (constructionMission && (state_.wood < 6 || state_.stone < 4)) {
        return rejected("前哨建设任务需要从仓库带走木材6、石料4。 ");
    }
    if (state_.squads.front().refusingOrders) return rejected("晨火队正在抗命，请先安抚派系。");
    if (state_.squads.front().fatigue >= 85) return rejected("晨火队过于疲劳，需要先休整。");

    GameState candidate = state_;
    const std::uint32_t missionSeed =
        candidate.seed + static_cast<std::uint32_t>(candidate.season * 97 + candidate.missionCount * 17);
    PermanentSquad& permanent = candidate.squads.front();
    ExpansionState missionState;
    missionState.seed = missionSeed;
    missionState.squad.name = permanent.name;
    missionState.squad.cohesion = 70;
    missionState.squad.members.clear();
    missionState.squad.members.reserve(static_cast<std::size_t>(crewSize));
    const auto addMember = [&](const std::string& name) {
        if (static_cast<int>(missionState.squad.members.size()) >= crewSize) return true;
        const Character* character = findRosterCharacter(candidate.roster, name);
        if (character == nullptr || character->life <= 0) return false;
        missionState.squad.members.push_back(*character);
        return true;
    };
    if (!addMember(permanent.captain)) return rejected("小队成员缺失或已阵亡，任务未开始。");
    for (const std::string& name : permanent.members)
        if (name != permanent.captain && !addMember(name)) return rejected("小队成员缺失或已阵亡，任务未开始。");
    const auto captain = std::find_if(missionState.squad.members.begin(), missionState.squad.members.end(),
                                      [&](const Character& character) { return character.name == permanent.captain; });
    if (captain == missionState.squad.members.end()) return rejected("长期小队的队长不在出发名单中。");
    missionState.squad.leaderIndex =
        static_cast<std::size_t>(std::distance(missionState.squad.members.begin(), captain));
    missionState.backpack = Inventory{80, 20};
    missionState.phase = ExpansionPhase::Exploring;
    missionState.settled = false;
    missionState.worldLocation = static_cast<int>(permanent.station);
    missionState.worldDiscovered = candidate.discovered;
    missionState.outposts = candidate.outposts;
    missionState.foodGatherBonus = (candidate.technologies[indexOf(TechnologyId::FoodPreservation)] ? 2 : 0) +
                                   (candidate.technologies[indexOf(TechnologyId::Irrigation)] ? 3 : 0);
    missionState.herbGatherBonus = candidate.technologies[indexOf(TechnologyId::HerbalKnowledge)] ? 2 : 0;
    missionState.missionKind = kind;
    missionState.assignedResource = resource;
    missionState.crewSize = crewSize;
    missionState.cargoCapacity = 16 + crewSize * 4;
    if (constructionMission) {
        missionState.cargoWood = 6;
        missionState.cargoStone = 4;
        candidate.wood -= 6;
        candidate.stone -= 4;
    }
    const OperationResult missionValid = ExpansionGame::validateState(missionState);
    if (!missionValid)
        return rejected("长期小队无法进入任务（凝聚力" + std::to_string(missionState.squad.cohesion) + "）：" +
                        missionValid.message);
    candidate.activeMission = std::move(missionState);
    candidate.phase = GamePhase::Mission;
    if (population_rules::committedPopulation(candidate) > population_rules::populationCapacity(candidate)) {
        return rejected("统一人口池不足：劳力、驻军、已组建军队和出任务小队合计不能超过人口-2。 ");
    }
    permanent.personallyDeployedThisSeason = true;
    spendAction(candidate, crewSize);
    const std::string taskName = constructionMission ? "前哨建设" : resourceName(resource) + "采集";
    return commit(std::move(candidate),
                  "晨火队带领" + std::to_string(crewSize) + "名劳力从驻地进入十六地点地图，执行" + taskName + "任务。",
                  true);
}

ActionResult GameEngine::executeMission(const std::string_view input) {
    if (!state_.activeMission) return rejected("任务状态缺失，无法继续。");
    const Command command = parse(input);
    const bool diplomaticVerb =
        verbIs(command, {"talk",    "交谈", "gift",    "送礼", "trade",  "贸易", "openroute", "开通商路",
                         "marry",   "联姻", "tribute", "朝贡", "demand", "索贡", "ally",      "结盟",
                         "declare", "宣战", "truce",   "停战", "raid",   "劫掠"});
    if (diplomaticVerb) {
        const auto tribe = missionTribeAt(state_.activeMission->worldLocation);
        if (!tribe) return rejected("这里没有可接触的部落；请前往河鹿渡口、白羽营地、古老山隘、潮盐港或玄石工坊。 ");
        const bool isTrade = verbIs(command, {"trade", "贸易"});
        if ((isTrade && command.args.size() != 2U) || (!isTrade && !command.args.empty())) {
            return rejected(isTrade ? "地图贸易用法：trade <给出的资源> <换取的资源>。"
                                    : "地图外交由当前位置确定对象；该指令不需要部落名称。");
        }
        GameState proxyState = state_;
        proxyState.phase = GamePhase::Managing;
        proxyState.activeMission.reset();
        proxyState.actionsLeft = 1;
        proxyState.discovered = state_.activeMission->worldDiscovered;
        GameEngine proxy{std::move(proxyState)};
        std::string proxyInput = command.verb + " " + tribeCommandName(*tribe);
        if (isTrade) proxyInput += " " + command.args[0] + " " + command.args[1];
        ActionResult diplomatic = proxy.execute(proxyInput);
        if (!diplomatic.success) return diplomatic;

        GameState candidate = proxy.state();
        candidate.phase = GamePhase::Mission;
        candidate.actionsLeft = state_.actionsLeft;
        candidate.activeMission = state_.activeMission;
        ExpansionState& missionState = *candidate.activeMission;
        ++missionState.turn;
        for (Character& member : missionState.squad.members) member.fatigue = std::min(100, member.fatigue + 1);
        const std::string locationName = worldLocations()[static_cast<std::size_t>(missionState.worldLocation)].name;
        return commit(std::move(candidate), "在" + locationName + "进行外交：" + diplomatic.message, false);
    }
    ExpansionGame mission{*state_.activeMission};
    const ExpansionCommandResult result = mission.execute(input);
    if (!result.recognized) return {};
    if (!result.success) return rejected(result.message);

    GameState candidate = state_;
    candidate.activeMission = mission.state();
    std::string message = result.message;
    if (mission.state().phase == ExpansionPhase::Settled) {
        // 结算顺序：复制地图发现/前哨→把载货与背包入账→同步角色伤亡→重建永久小队→清除活动任务→一次 commit。
        // 任一步发现名单或装备镜像不一致会返回拒绝，尚未提交的 candidate 被整体丢弃。
        const ExpansionState& settled = mission.state();
        ++candidate.missionCount;
        PermanentSquad& squad = candidate.squads.front();
        candidate.discovered = settled.worldDiscovered;
        candidate.outposts = settled.outposts;
        // 地图层只负责产生载货；这里是资源进入部落长期库存的唯一入口，避免重复结算。
        candidate.food += settled.cargoFood;
        candidate.wood += settled.cargoWood;
        candidate.stone += settled.cargoStone;
        candidate.herbs += settled.cargoHerbs;
        candidate.hides += settled.cargoHides;
        squad.station = static_cast<WorldLocationId>(settled.worldLocation);
        for (const Item& item : settled.backpack.items()) candidate.stockpile.push_back(item);
        const std::vector<std::size_t> originalSquadSizes = [&candidate] {
            std::vector<std::size_t> sizes;
            sizes.reserve(candidate.squads.size());
            for (const PermanentSquad& permanent : candidate.squads) sizes.push_back(permanent.members.size());
            return sizes;
        }();

        std::unordered_set<std::string> deployedNames;
        std::unordered_set<std::string> deadNames;
        for (const Character& member : settled.squad.members) {
            Character* permanent = findRosterCharacter(candidate.roster, member.name);
            if (permanent == nullptr || !deployedNames.insert(member.name).second) {
                return rejected("任务成员与长期角色名单不一致，回营结算已原子取消。");
            }
            if (member.life <= 0)
                deadNames.insert(member.name);
            else
                *permanent = member;
        }
        squad.eliteExperience += 10 + settled.harvestActions * 5;

        bool coreSquadLost = false;
        if (!deadNames.empty()) {
            candidate.roster.erase(
                std::remove_if(candidate.roster.begin(), candidate.roster.end(),
                               [&](const Character& character) { return deadNames.count(character.name) != 0U; }),
                candidate.roster.end());

            const int deaths = static_cast<int>(deadNames.size());
            candidate.missionDeaths += deaths;
            candidate.population = std::max(0, candidate.population - deaths);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.stability = std::max(0, candidate.stability - 8);
            message += " 本次共阵亡" + std::to_string(deaths) + "人，长期名单与小队编制已同步。";
            addChronicle(candidate, 3, "地图任务伤亡", "本次任务阵亡" + std::to_string(deaths) + "人。");

            for (std::size_t index = 0; index < candidate.squads.size(); ++index) {
                PermanentSquad& permanent = candidate.squads[index];
                permanent.members.erase(std::remove_if(permanent.members.begin(), permanent.members.end(),
                                                       [&](const std::string& name) {
                                                           const Character* character =
                                                               findRosterCharacter(candidate.roster, name);
                                                           return !character || character->life <= 0;
                                                       }),
                                        permanent.members.end());

                const std::size_t targetSize = std::min(originalSquadSizes[index], kMaximumSquadSize);
                for (const Character& reserve : candidate.roster) {
                    if (permanent.members.size() >= targetSize) break;
                    if (reserve.life > 0 && std::find(permanent.members.begin(), permanent.members.end(),
                                                      reserve.name) == permanent.members.end()) {
                        permanent.members.push_back(reserve.name);
                    }
                }
                if (permanent.members.size() < kMinimumSquadSize) {
                    coreSquadLost = true;
                    permanent.members.clear();
                    permanent.captain.clear();
                    continue;
                }
                if (std::find(permanent.members.begin(), permanent.members.end(), permanent.captain) ==
                    permanent.members.end()) {
                    permanent.captain = permanent.members.front();
                }
            }
            candidate.squads.erase(
                std::remove_if(candidate.squads.begin(), candidate.squads.end(),
                               [](const PermanentSquad& permanent) { return permanent.members.empty(); }),
                candidate.squads.end());
        }

        for (PermanentSquad& permanent : candidate.squads) {
            permanent.fatigue = permanentSquadFatigue(permanent, candidate.roster);
        }
        candidate.highestLevel = 1;
        for (const Character& member : candidate.roster) {
            candidate.highestLevel = std::max(candidate.highestLevel, member.level);
        }

        message += " 地图任务载货及任务背包已并入部落库存，小队驻地已更新。";
        addChronicle(
            candidate, 2, "地图任务结算",
            "晨火队在" + worldLocations()[static_cast<std::size_t>(settled.worldLocation)].name + "完成结算并驻留。");
        candidate.activeMission.reset();
        if (coreSquadLost && candidate.squads.empty()) {
            candidate.campDurability = 0;
            candidate.actionsLeft = 0;
            candidate.phase = GamePhase::Finished;
            candidate.ending = GameEnding::Extinction;
            message += " 没有足够的存活骨干重建小队，营地在混乱中瓦解，进入部落覆灭结算。";
            addChronicle(candidate, 5, "部落覆灭",
                         "地图任务重创后已无足够存活骨干维持营地。早先发生的伤亡不会被回滚。");
        } else {
            candidate.phase = GamePhase::Managing;
        }
        const bool endingReached = coreSquadLost && candidate.squads.empty();
        return commit(std::move(candidate), std::move(message), false, false, endingReached);
    }
    return commit(std::move(candidate), std::move(message), false);
}

} // namespace tribe
