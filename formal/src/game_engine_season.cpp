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
void GameEngine::settleFoodAndTribute(GameState& candidate, std::string& message) const {
    int consumption = (candidate.population + 1) / 2;
    if ((candidate.season - 1) % 4 == 3) consumption += 3;
    if (candidate.buildings[indexOf(BuildingId::Granary)] && (candidate.season - 1) % 4 == 3)
        consumption = std::max(0, consumption - 2);
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        if (candidate.relations[index].playerPaysTribute) consumption += 2;
        if (candidate.relations[index].otherPaysTribute) candidate.food += 2;
    }
    if (candidate.food >= consumption) {
        candidate.food -= consumption;
        message += " 季节食物消耗" + std::to_string(consumption) + "。";
    } else {
        const int shortage = consumption - candidate.food;
        candidate.food = 0;
        const int loss = std::min(candidate.population, 1 + shortage / 4);
        candidate.population -= loss;
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        candidate.morale = std::max(0, candidate.morale - 12);
        candidate.stability = std::max(0, candidate.stability - 15);
        message += " 食物不足，人口-" + std::to_string(loss) + "、士气-12、稳定-15。";
    }
}

void GameEngine::settleAutonomousTribes(GameState& candidate, std::string& message) const {
    const std::size_t index =
        1U + static_cast<std::size_t>((candidate.seed + static_cast<std::uint32_t>(candidate.season * 13)) % 5U);
    auto& relation = candidate.relations[index];
    TribeProfile& profile = candidate.tribes[index];
    const TribeId tribe = static_cast<TribeId>(index);
    const FactionState& faction = dominantFaction(profile);
    const int choice =
        static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.season * 7 + index)) % 4U);

    const bool tradeDriven = containsAny(profile.personality, {"农业", "航运", "工艺", "精明", "务实"}) ||
                             containsAny(faction.demand, {"粮食", "贸易", "航路", "盐价", "换取", "矿路", "装备"});
    const bool conciliatory = containsAny(profile.personality, {"谨慎", "救助", "温和"}) ||
                              containsAny(faction.demand, {"救助", "伤者", "共享", "情报", "停战", "谈判"});
    const bool aggressive = containsAny(profile.personality, {"强硬", "好战", "武勇"}) ||
                            containsAny(faction.demand, {"战利品", "控制", "征服", "复仇"});

    const bool contacted = locationDiscovered(candidate, contactLocation(tribe));
    const std::string reason = contacted
                                   ? " " + profile.name + "首领" + profile.leader + "秉持“" + profile.personality +
                                         "”，主导派系" + faction.name + "要求“" + faction.demand + "”；"
                                   : " 一个尚未正式接触的部落受其首领取向和内部派系诉求推动；";
    if (candidate.workforce.envoys > 0 && contacted && !relation.atWar) {
        relation.relation = relationClamp(relation.relation + 1);
        relation.trust = percentClamp(relation.trust + 1);
        message += reason + "使者维持往来，关系+1、信任+1。";
    } else if (relation.atWar) {
        relation.fear = percentClamp(relation.fear + 2);
        message += reason + "双方仍处战争，因此优先集结兵力，恐惧+2。";
    } else if (relation.tradeRoute && (tradeDriven || choice <= 1)) {
        candidate.food += 2;
        relation.tradeDependence = percentClamp(relation.tradeDependence + 2);
        message +=
            reason + "双方关系" + std::to_string(relation.relation) + "且固定商路畅通，因此商队送来2食物，贸易依赖+2。";
    } else if (relation.relation < 0 && (aggressive || choice == 2)) {
        relation.fear = percentClamp(relation.fear + 4);
        const bool towerWarning =
            candidate.buildings[indexOf(BuildingId::Watchtower)] && candidate.workforce.scouts > 0;
        const int protection = (candidate.workforce.campGuards > 0 ? 1 : 0) + (candidate.workforce.scouts > 0 ? 1 : 0) +
                               (towerWarning ? 1 : 0);
        const int damage = std::max(0, 2 - protection);
        candidate.campDurability = std::max(0, candidate.campDurability - damage);
        message += reason + "双方关系仅" + std::to_string(relation.relation) +
                   "且尚无固定商路，因此发动边境骚扰，恐惧+4、营地耐久-" + std::to_string(damage) +
                   (towerWarning ? "；瞭望塔提前预警，额外抵消1点损失。" : "。");
    } else if (conciliatory || tradeDriven || relation.relation >= 15 || relation.trust >= 20 || choice == 0) {
        const int relationBefore = relation.relation;
        const int trustBefore = relation.trust;
        relation.relation = relationClamp(relation.relation + 3);
        relation.trust = percentClamp(relation.trust + 2);
        message += reason + "考虑当前关系" + std::to_string(relationBefore) + "、信任" + std::to_string(trustBefore) +
                   "且尚无固定商路，因此派来使者，关系+3、信任+2。";
    } else {
        relation.relation = relationClamp(relation.relation - 2);
        message += reason + "当前信任" + std::to_string(relation.trust) + "且尚无固定商路，因此暂时疏远，关系-2。";
    }

    if (candidate.season % 8 == 0 && index != indexOf(TribeId::Player)) {
        profile.actingLeader = profile.successor;
        profile.leader = profile.successor;
        message += " " + profile.name + "首领更替为" + profile.leader + "。";
        addChronicle(candidate, 3, profile.name + "首领更替", profile.successor + "在派系推举下接掌部落。");
    }
}

void GameEngine::settleFactions(GameState& candidate, std::string& message) const {
    const bool councilRelief = candidate.buildings[indexOf(BuildingId::CouncilFire)] && candidate.workforce.envoys > 0;
    const bool councilReliefApplied = councilRelief && (candidate.food == 0 || candidate.stability < 40);
    for (FactionState& faction : candidate.playerFactions) {
        const int basePressure = candidate.food == 0 ? 12 : candidate.stability < 40 ? 8 : -2;
        const int pressure = councilReliefApplied ? std::max(0, basePressure - 2) : basePressure;
        faction.satisfaction = percentClamp(faction.satisfaction - pressure);
        int stage = static_cast<int>(faction.crisis);
        if (faction.satisfaction < 25 || candidate.stability < 25)
            stage = std::min(5, stage + 1);
        else if (faction.satisfaction >= 55 && stage > 0)
            --stage;
        faction.crisis = static_cast<FactionCrisis>(stage);
        if (faction.crisis == FactionCrisis::Slowdown) {
            candidate.food = std::max(0, candidate.food - 2);
            message += " " + faction.name + "减产，食物-2。";
        } else if (faction.crisis == FactionCrisis::Refusal) {
            for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = true;
            message += " " + faction.name + "拒绝出队。";
        } else if (faction.crisis == FactionCrisis::Deposition) {
            candidate.stability = std::max(0, candidate.stability - 6);
            message += " " + faction.name + "要求罢免首领，稳定-6。";
        } else if (faction.crisis == FactionCrisis::Coup) {
            candidate.actingLeaderName = faction.candidate;
            candidate.leaderName = faction.candidate;
            candidate.tribes[indexOf(TribeId::Player)].leader = faction.candidate;
            candidate.leadershipHistory.push_back(faction.candidate + "（派系政变接任）");
            candidate.stability = 35;
            faction.satisfaction = 50;
            faction.crisis = FactionCrisis::Complaint;
            message += " " + faction.name + "发动政变，" + faction.candidate + "成为代理首领。";
            addChronicle(candidate, 4, "部落政变", faction.candidate + "在危机中接替原首领。");
        }
    }
    if (councilReliefApplied) message += " 议事火坛由使者主持，派系满意流失-2。";
    const bool refusing =
        std::any_of(candidate.playerFactions.begin(), candidate.playerFactions.end(),
                    [](const FactionState& faction) { return faction.crisis == FactionCrisis::Refusal; });
    for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = refusing;
}

void GameEngine::finishExtinction(GameState& candidate, std::string& message) const {
    if (candidate.population > 0 && candidate.campDurability > 0) return;
    candidate.phase = GamePhase::Finished;
    candidate.ending = GameEnding::Extinction;
    candidate.activeMission.reset();
    releaseWarEquipment(candidate, true);
    candidate.war = {};
    message += " 部落人口或营地耐久归零，燧火熄灭。";
    addChronicle(candidate, 5, "部落覆灭", "最后的火坛在风中熄灭。");
}

ActionResult GameEngine::endSeason() {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox)
        return rejected("当前不能结束季节。");
    if (state_.pendingEvent.active || !state_.pendingEvents.empty())
        return rejected("本季事件尚未处理完，请先输入 event 查看并逐个处理。 ");
    GameState candidate = state_;
    std::string message = "第" + std::to_string(candidate.season) + "季结算：";
    settleFoodAndTribute(candidate, message);
    if (candidate.buildings[indexOf(BuildingId::Longhouse)]) {
        if (candidate.workforce.housing > 0 && candidate.population < candidate.populationLimit && candidate.food >= 2) {
            candidate.food -= 2;
            ++candidate.population;
            message += " 长屋在住房岗位维护下接纳1名新成员（食物-2）。";
        } else if (candidate.workforce.housing == 0) {
            candidate.stability = std::max(0, candidate.stability - 2);
            message += " 长屋无人维护，住房失效且稳定-2。";
        }
    }
    settleAutonomousTribes(candidate, message);
    settleFactions(candidate, message);
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        OccupationState& site = candidate.occupations[index];
        if (!site.occupied) continue;
        const int required = index == indexOf(TribeId::Rockfang) || index == indexOf(TribeId::Blackstone) ? 4 : 2;
        if (site.garrison < required) {
            ++site.unrest;
            message += " " + tribeName(static_cast<TribeId>(index)) + "据点驻军不足，动乱+1。";
        } else {
            site.unrest = std::max(0, site.unrest - 1);
            candidate.food += 2;
        }
        if (site.unrest >= 3) {
            site = {};
            candidate.stability = std::max(0, candidate.stability - 6);
            message += " 据点反抗成功，失去占领并稳定-6。";
        }
    }
    for (std::size_t index = 1; index < kWorldLocationCount; ++index)
        if (candidate.outposts[index]) {
            if (candidate.workforce.outpostGuards[index] == 0)
                ++candidate.workforce.outpostIdleSeasons[index];
            else
                candidate.workforce.outpostIdleSeasons[index] = 0;
            const bool occupiedBySquad =
                std::any_of(candidate.squads.begin(), candidate.squads.end(),
                            [&](const PermanentSquad& squad) { return indexOf(squad.station) == index; });
            if (candidate.workforce.outpostIdleSeasons[index] >= 2 && !occupiedBySquad) {
                candidate.outposts[index] = false;
                candidate.workforce.outpostIdleSeasons[index] = 0;
                message += " 一座无人前哨荒废。";
            }
        }
    candidate.pendingEvents.clear();
    const int firstKind = static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.season)) % 4U);
    candidate.pendingEvents.push_back(static_cast<PendingEventKind>(firstKind));
    candidate.pendingEvents.push_back(static_cast<PendingEventKind>((firstKind + 1 + candidate.season) % 4));
    const bool factionCrisis = std::any_of(
        candidate.playerFactions.begin(), candidate.playerFactions.end(), [](const FactionState& faction) {
            return static_cast<int>(faction.crisis) >= static_cast<int>(FactionCrisis::Refusal);
        });
    const bool highRisk = candidate.stability < 45 || candidate.campDurability < 12 || factionCrisis ||
                          ((candidate.seed + static_cast<std::uint32_t>(candidate.season * 31)) % 5U == 0U);
    if (highRisk) candidate.pendingEvents.push_back(static_cast<PendingEventKind>((firstKind + 2) % 4));
    candidate.pendingEvent.active = true;
    candidate.pendingEvent.kind = candidate.pendingEvents.front();
    message += " 新的季度事件已出现，共" + std::to_string(candidate.pendingEvents.size()) +
               "个（输入 event 查看并按顺序处理）。";
    finishExtinction(candidate, message);
    if (candidate.phase == GamePhase::Finished)
        return commit(std::move(candidate), std::move(message), false, true, true);

    if (candidate.season >= candidate.seasonLimit && candidate.phase != GamePhase::Sandbox) {
        candidate.phase = GamePhase::EndingChoice;
        candidate.actionsLeft = 0;
        candidate.longModeFinalShown = candidate.mode == GameMode::Long;
        addChronicle(candidate, 4, "时代结算", "族人围绕火坛讨论已经满足的道路。");
        message += " 已到达模式结算季，请查看目标并选择结局。";
        return commit(std::move(candidate), std::move(message), false, true, false);
    }

    ++candidate.season;
    candidate.actionsLeft = availableTeams(candidate);
    for (PermanentSquad& squad : candidate.squads) squad.personallyDeployedThisSeason = false;
    return commit(std::move(candidate), std::move(message), false, true, false);
}

std::vector<GameEnding> GameEngine::availableEndings() const {
    std::vector<GameEnding> endings;
    if (state_.population <= 0 || state_.campDurability <= 0) return {GameEnding::Extinction};
    int allies = 0;
    for (std::size_t index = 1; index < kTribeCount; ++index) allies += state_.relations[index].alliance ? 1 : 0;
    if (allies >= 2 && state_.relations[indexOf(TribeId::RiverDeer)].relation >= 70 &&
        state_.relations[indexOf(TribeId::WhiteFeather)].relation >= 70 &&
        state_.technologies[indexOf(TechnologyId::Confederation)])
        endings.push_back(GameEnding::Alliance);
    const int occupied = static_cast<int>(std::count_if(state_.occupations.begin() + 1, state_.occupations.end(),
                                                        [](const OccupationState& site) { return site.occupied; }));
    if (occupied >= 2 && state_.warriors >= 5 && state_.morale >= 55) endings.push_back(GameEnding::Conquest);
    if (state_.population >= 20 && state_.food >= 40 && countTrue(state_.buildings) >= 4 &&
        countTrue(state_.technologies) >= 4)
        endings.push_back(GameEnding::Prosperity);
    endings.push_back(GameEnding::Migration);
    return endings;
}

ActionResult GameEngine::chooseEnding(const GameEnding ending) {
    if (state_.phase != GamePhase::EndingChoice) return rejected("现在还不能选择结局。");
    const auto endings = availableEndings();
    if (std::find(endings.begin(), endings.end(), ending) == endings.end())
        return rejected("当前条件尚未满足该结局道路。");
    GameState candidate = state_;
    candidate.ending = ending;
    candidate.phase = GamePhase::Finished;
    addChronicle(candidate, 5, endingName(ending), "族人共同选择了这条道路。");
    return commit(std::move(candidate), "结局已确定：" + endingName(ending) + "。进入独立结算画面。", false, false,
                  true);
}

ActionResult GameEngine::continueSandbox() {
    if (state_.phase != GamePhase::Finished || state_.mode != GameMode::Long || state_.ending == GameEnding::Extinction)
        return rejected("只有长期模式非覆灭结局可以继续沙盒。");
    GameState candidate = state_;
    candidate.phase = GamePhase::Sandbox;
    candidate.actionsLeft = availableTeams(candidate);
    ++candidate.season;
    return commit(std::move(candidate), "进入结局后的自由沙盒，部落可以继续经营。", false);
}

ActionResult GameEngine::chooseEvent(const int option) {
    // 事件规则只修改候选状态；覆灭判定和统一 commit 仍集中在季结算单元，失败不会消耗当前局面。
    GameState candidate = state_;
    const seasonal_event_rules::EventResolution resolution = seasonal_event_rules::resolveChoice(candidate, option);
    if (!resolution.success) return rejected(resolution.message);
    std::string outcome = resolution.message;
    finishExtinction(candidate, outcome);
    return commit(std::move(candidate), "事件抉择已执行：" + outcome, false);
}

} // namespace tribe
