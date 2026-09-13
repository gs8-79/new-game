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

// 1. 仅操作尚未提交的候选状态；2. 交由规则层将每件锁定装备归仓或标记损坏；3. 清空锁定列表。
// 不变量：同一件装备在任一时刻只能位于仓库或军队锁定列表之一，调用方随后必须通过 commit 校验。
void GameEngine::releaseWarEquipment(GameState& candidate, const bool damaged) const {
    war_rules::releaseLockedEquipment(candidate, damaged);
}

ActionResult GameEngine::formArmy(const int warriors, const int militia, const std::string_view commander) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (population_rules::hasPreparedArmy(state_)) return rejected("已有一支已组建军队；请先解散军队以归还锁定装备。 ");
    const int availableWarriors = std::max(0, state_.warriors - population_rules::garrisonAllocation(state_));
    if (warriors <= 0 || warriors > availableWarriors) return rejected("正式战士数量必须为1至未驻军的受训战士数。 ");
    if (militia < 0) return rejected("民兵数量不能为负数。 ");
    GameState candidate = state_;
    const std::string selectedCommander = commander.empty() ? "石刃" : std::string(commander);
    if (findRosterCharacter(candidate.roster, selectedCommander) == nullptr) return rejected("统帅必须是具名人物。 ");
    // 1. 在候选状态重建编制；2. 从共享仓库移动可用装备；3. 计算兵种与工艺加成；4. 最后用统一人口池校验。
    // 任何前置条件或人口校验失败都不会提交 candidate，因此装备不会从真实库存丢失。
    candidate.war = {};
    candidate.war.commander = selectedCommander;
    candidate.war.warriors = warriors;
    candidate.war.militia = militia;
    candidate.war.playerPower = warriors * 2 + militia + candidate.morale / 10;
    for (auto item = candidate.stockpile.begin();
         item != candidate.stockpile.end() &&
         static_cast<int>(candidate.war.lockedEquipment.size()) < warriors + militia;) {
        if (item->condition == ItemCondition::Scrapped || !item->equipmentSlot) {
            ++item;
            continue;
        }
        // 先复制到锁定列表再从仓库擦除，保证每件装备在候选状态中始终只有一个所有位置。
        candidate.war.lockedEquipment.push_back(*item);
        if (*item->equipmentSlot == EquipmentSlot::MainHand && item->name.find("矛") != std::string::npos)
            ++candidate.war.spearMilitia;
        if (*item->equipmentSlot == EquipmentSlot::OffHand) ++candidate.war.shieldBearers;
        item = candidate.stockpile.erase(item);
    }
    candidate.war.heavySpears = std::min({candidate.war.spearMilitia, candidate.war.shieldBearers,
                                          std::max(0, warriors + militia - candidate.war.spearMilitia)});
    candidate.war.craftsmanshipPower = std::min(
        4, std::accumulate(candidate.war.lockedEquipment.begin(), candidate.war.lockedEquipment.end(), 0,
                           [](const int total, const Item& item) { return total + itemQualityTier(item.quality); }));
    candidate.war.playerPower += candidate.war.spearMilitia * 2 + candidate.war.shieldBearers +
                                 candidate.war.heavySpears * 2 + candidate.war.craftsmanshipPower;
    if (population_rules::committedPopulation(candidate) > population_rules::populationCapacity(candidate)) {
        return rejected("统一人口池不足：已组建军队会与劳力、驻军及出任务小队共同占用人口。 ");
    }
    spendAction(candidate);
    return commit(std::move(candidate),
                  "军队已组建：统帅" + selectedCommander + "，正式战士" + std::to_string(warriors) + "，民兵" +
                      std::to_string(militia) + "；装备已锁定，可用 power 查看兵种。",
                  true);
}

ActionResult GameEngine::disbandArmy() {
    if (state_.war.active) return rejected("战争进行中不能解散军队；请先撤退、获胜或等待溃败结算。 ");
    if (!population_rules::hasPreparedArmy(state_)) return rejected("当前没有已组建的军队。 ");
    GameState candidate = state_;
    releaseWarEquipment(candidate, false);
    candidate.war = {};
    return commit(std::move(candidate), "军队已解散，幸存者回归人口池，锁定装备已归还仓库。", false);
}

ActionResult GameEngine::startWar(const TribeId enemy) {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox) return rejected("当前不能出征。");
    if (state_.war.commander.empty() || state_.war.warriors <= 0) return rejected("请先组建军队。");
    if (!state_.relations[indexOf(enemy)].atWar) return rejected("需要先正式宣战。");
    const WorldLocationId target = enemy == TribeId::Rockfang ? WorldLocationId::RockfangFort : contactLocation(enemy);
    if (!locationDiscovered(state_, target)) return rejected("尚未发现通往战争目标的路线，不能出征。");
    GameState candidate = state_;
    candidate.phase = GamePhase::War;
    candidate.war.active = true;
    candidate.war.enemy = enemy;
    candidate.war.enemyPower = enemyBasePower(enemy);
    candidate.war.riskConfirmed = true;
    const int expectedEnemyPower = candidate.war.enemyPower;
    const int expectedPlayerPower = candidate.war.playerPower;
    return commit(std::move(candidate),
                  "出征开始。预计风险：敌方战力" + std::to_string(expectedEnemyPower) + "，己方" +
                      std::to_string(expectedPlayerPower) + "；民兵伤亡会减少人口与稳定。",
                  false);
}

ActionResult GameEngine::setWarOrder(const WarOrder order) {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    if (state_.war.order == order) return rejected("军队已经执行该军令。");
    GameState candidate = state_;
    candidate.war.order = order;
    if (order == WarOrder::Retreat) {
        return warRetreat();
    }
    return commit(std::move(candidate), "统帅下令“" + warOrderName(order) + "”。", false);
}

ActionResult GameEngine::warAttack() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    GameState candidate = state_;
    int modifier = 0;
    switch (candidate.war.order) {
        case WarOrder::Advance:
            modifier = 4;
            break;
        case WarOrder::Focus:
            modifier = 3;
            break;
        case WarOrder::Flank:
            modifier = candidate.technologies[indexOf(TechnologyId::AmbushTraining)] ? 5 : 1;
            break;
        case WarOrder::Hold:
            modifier = -1;
            break;
        case WarOrder::Cover:
            modifier = -2;
            break;
        case WarOrder::Retreat:
            return warRetreat();
    }
    const int damage = std::max(2, candidate.war.playerPower / 4 + modifier);
    candidate.war.enemyPower = std::max(0, candidate.war.enemyPower - damage);
    std::string message =
        "军队按“" + warOrderName(candidate.war.order) + "”进攻，敌方战力-" + std::to_string(damage) + "。";
    if (candidate.war.enemyPower == 0) {
        concludeWarVictory(candidate, message);
    } else {
        const int casualty =
            std::max(0, candidate.war.enemyPower / 10 -
                            (candidate.war.order == WarOrder::Cover || candidate.war.order == WarOrder::Hold ? 1 : 0));
        int remaining = casualty;
        const int militiaLost = std::min(candidate.war.militia, remaining);
        candidate.war.militia -= militiaLost;
        candidate.population = std::max(0, candidate.population - militiaLost);
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        remaining -= militiaLost;
        const int warriorsLost = std::min(candidate.war.warriors, remaining);
        candidate.war.warriors -= warriorsLost;
        candidate.warriors = std::max(0, candidate.warriors - warriorsLost);
        candidate.population = std::max(0, candidate.population - warriorsLost);
        const int actualLosses = militiaLost + warriorsLost;
        candidate.war.playerPower = std::max(0, candidate.war.playerPower - actualLosses * 2);
        if (actualLosses > 0) {
            candidate.stability = std::max(0, candidate.stability - militiaLost * 2 - warriorsLost);
            message += " 反击造成" + std::to_string(actualLosses) + "人伤亡。";
        }
        if (candidate.war.warriors + candidate.war.militia <= 0 || candidate.war.playerPower <= 0) {
            candidate.phase = GamePhase::Managing;
            releaseWarEquipment(candidate, true);
            candidate.war = {};
            ++candidate.warsLost;
            candidate.morale = std::max(0, candidate.morale - 15);
            candidate.stability = std::max(0, candidate.stability - 10);
            message += " 军队溃败，战争失败。";
            addChronicle(candidate, 4, "战争溃败", "军队失去战斗能力，部落付出人口与稳定代价。");
        }
    }
    finishExtinction(candidate, message);
    return commit(std::move(candidate), std::move(message), false);
}

ActionResult GameEngine::warDefend() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    GameState candidate = state_;
    const int defense = 2 + (candidate.technologies[indexOf(TechnologyId::ShieldWall)] ? 3 : 0) +
                        (candidate.buildings[indexOf(BuildingId::Wall)] ? 2 : 0);
    const int counter = std::max(1, candidate.war.playerPower / 8 + defense);
    const int actualCounter = std::min(counter, std::max(0, candidate.war.enemyPower - 1));
    if (actualCounter == 0) return rejected("敌军阵线只剩最后一点战力，继续防守不会改变战局；需要主动攻击或撤退。");
    candidate.war.enemyPower -= actualCounter;
    if (candidate.food > 0)
        --candidate.food;
    else
        candidate.morale = std::max(0, candidate.morale - 1);
    std::string message = "军队坚守并反击，敌方战力-" + std::to_string(actualCounter) + "。";
    if (candidate.war.enemyPower == 1) {
        message += " 敌军阵线已经动摇，但防守不能占领战线；需要主动攻击才能推进。";
    }
    return commit(std::move(candidate), std::move(message), false);
}

void GameEngine::concludeWarVictory(GameState& candidate, std::string& message) const {
    const TribeId enemy = candidate.war.enemy;
    candidate.phase = GamePhase::Managing;
    ++candidate.warsWon;
    auto& relation = candidate.relations[indexOf(enemy)];
    relation.fear = percentClamp(relation.fear + 25);
    relation.relation = relationClamp(relation.relation - 10);
    candidate.occupations[indexOf(enemy)] = {true, 0, 0};
    releaseWarEquipment(candidate, false);
    candidate.war = {};
    candidate.morale = std::min(100, candidate.morale + 8);
    candidate.stability = std::min(100, candidate.stability + 5);
    addChronicle(candidate, 4, "战争胜利", "石刃率军击败" + tribeName(enemy) + "并占领其据点，等待驻军维持秩序。");
    message += " 敌军溃散，据点已占领；请分配驻军。";
}

ActionResult GameEngine::warRetreat() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    GameState candidate = state_;
    candidate.phase = GamePhase::Managing;
    candidate.food = std::max(0, candidate.food - 4);
    candidate.morale = std::max(0, candidate.morale - 6);
    candidate.stability = std::max(0, candidate.stability - 3);
    releaseWarEquipment(candidate, false);
    candidate.war = {};
    addChronicle(candidate, 2, "军队撤退", "统帅保住主力，但消耗补给并打击士气。");
    return commit(std::move(candidate), "全军撤退：食物-4、士气-6、稳定-3。", false);
}

ActionResult GameEngine::garrison(const TribeId tribe, const int warriors) {
    if (tribe == TribeId::Player || warriors < 0) return rejected("驻军目标或人数无效。 ");
    GameState candidate = state_;
    OccupationState& site = candidate.occupations[indexOf(tribe)];
    if (!site.occupied) return rejected("该据点尚未被占领。 ");
    if (state_.workforceReassignmentRequired && warriors >= site.garrison) {
        return rejected("劳力待重分配时只能降低驻军人数。 ");
    }
    const int totalOther = std::accumulate(candidate.occupations.begin() + 1, candidate.occupations.end(), 0,
                                           [&](int value, const OccupationState& x) { return value + x.garrison; }) -
                           site.garrison;
    const int armyWarriors = population_rules::hasPreparedArmy(candidate) ? candidate.war.warriors : 0;
    if (warriors + totalOther + armyWarriors > candidate.warriors)
        return rejected("驻军不能与已组建军队重复使用同一批受训战士。 ");
    site.garrison = warriors;
    return commit(std::move(candidate), "驻军已调整。", false);
}

} // namespace tribe
