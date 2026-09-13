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
bool GameEngine::replaceState(const GameState& candidate, std::string& error) {
    if (!validateState(candidate, error)) return false;
    state_ = candidate;
    error.clear();
    return true;
}

bool GameEngine::validateState(const GameState& candidate, std::string& error) {
    if (!enumInRange(candidate.mode, GameMode::Quick, GameMode::Long) ||
        !enumInRange(candidate.phase, GamePhase::Managing, GamePhase::Sandbox) ||
        !enumInRange(candidate.ending, GameEnding::None, GameEnding::Extinction)) {
        error = "模式、阶段或结局枚举无效。";
        return false;
    }
    if (candidate.season <= 0 || candidate.seasonLimit <= 0 || candidate.season > 10000 ||
        candidate.seasonLimit > 10000 || candidate.actionsLeft < 0 || candidate.actionsLeft > 7 ||
        candidate.populationLimit <= 0 || candidate.populationLimit > 10000) {
        error = "种子、季节或行动点范围无效。";
        return false;
    }
    const std::array<int, 17> nonnegative{{candidate.population, candidate.food, candidate.wood, candidate.stone,
                                           candidate.herbs, candidate.hides, candidate.warriors, candidate.morale,
                                           candidate.campDurability, candidate.stability, candidate.tradeCount,
                                           candidate.warsWon, candidate.warsLost, candidate.missionCount,
                                           candidate.missionDeaths, candidate.highestLevel, candidate.seasonLimit}};
    if (std::any_of(nonnegative.begin(), nonnegative.end(), [](const int value) { return value < 0; }) ||
        candidate.morale > 100 || candidate.stability > 100 || candidate.campDurability > 100) {
        error = "资源或百分比超出范围。";
        return false;
    }
    if (candidate.population > candidate.populationLimit) {
        error = "当前人口不能超过人口上限。";
        return false;
    }
    if (candidate.actionsLeft > population_rules::actionCapacity(candidate)) {
        error = "剩余行动力超过当前可用人口容量。";
        return false;
    }
    if (candidate.warriors > candidate.population) {
        error = "战士人数不能超过部落人口。";
        return false;
    }
    if (candidate.nextItemSerial == 0U) {
        error = "下一件装备编号必须为正数。";
        return false;
    }
    // 所有候选状态（命令提交、replaceState 与存档读取）共用同一文本边界。
    if (!state_safety::validatePersistentText(candidate, error)) return false;
    if (!population_rules::validateWorkforce(candidate.workforce, error)) return false;
    const int overage = population_rules::populationOverage(candidate);
    if (candidate.workforceReassignmentRequired != (overage > 0)) {
        error = "劳力待重分配标记与统一人口池不一致。";
        return false;
    }
    if (candidate.workforce.housing > 0 && !candidate.buildings[indexOf(BuildingId::Longhouse)]) {
        error = "未建长屋不能配置住房岗位。";
        return false;
    }
    for (const PendingEventKind event : candidate.pendingEvents) {
        if (!enumInRange(event, PendingEventKind::Refugees, PendingEventKind::FactionDemand)) {
            error = "季度事件队列包含无效事件。";
            return false;
        }
    }
    if (candidate.pendingEvent.active && candidate.pendingEvents.empty()) {
        // v5/v6 状态只有单事件字段，允许在加载后由新版本逐步消费；v7 新状态始终写入队列。
    } else if (!candidate.pendingEvents.empty() &&
               (!candidate.pendingEvent.active || candidate.pendingEvent.kind != candidate.pendingEvents.front())) {
        error = "季度事件队列与当前事件镜像不一致。";
        return false;
    }
    if (overage > 0 && !candidate.workforceReassignmentRequired) {
        error = "人口占用超过人口-2。";
        return false;
    }
    if (candidate.tribeName.empty() || candidate.leaderName.empty() || candidate.leaderFocus.empty()) {
        error = "部落名、首领名和擅长方向不能为空。";
        return false;
    }
    if (!candidate.discovered[indexOf(WorldLocationId::Camp)]) {
        error = "燧火营地必须已发现。";
        return false;
    }
    if (!candidate.outposts[indexOf(WorldLocationId::Camp)]) {
        error = "燧火营地必须是有效结算点。";
        return false;
    }
    for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
        if (candidate.outposts[index] && !candidate.discovered[index]) {
            error = "前哨不能建立在未发现地点。";
            return false;
        }
    }
    for (std::size_t index = 0; index < kTribeCount; ++index) {
        const TribeProfile& profile = candidate.tribes[index];
        if (profile.id != static_cast<TribeId>(index) || profile.name.empty() || profile.leader.empty() ||
            profile.successor.empty() || profile.factions.size() < 2U || profile.factions.size() > 3U) {
            error = "六部落档案不完整。";
            return false;
        }
        const auto& relation = candidate.relations[index];
        if (relation.relation < -100 || relation.relation > 100 || relation.trust < 0 || relation.trust > 100 ||
            relation.fear < 0 || relation.fear > 100 || relation.tradeDependence < 0 ||
            relation.tradeDependence > 100 ||
            (relation.atWar && (relation.alliance || relation.marriage || relation.tradeRoute ||
                                relation.playerPaysTribute || relation.otherPaysTribute)) ||
            (relation.playerPaysTribute && relation.otherPaysTribute)) {
            error = "外交关系字段越界或矛盾。";
            return false;
        }
    }
    for (const FactionState& faction : candidate.playerFactions) {
        if (faction.name.empty() || faction.candidate.empty() || faction.influence < 0 || faction.influence > 100 ||
            faction.satisfaction < 0 || faction.satisfaction > 100 ||
            !enumInRange(faction.crisis, FactionCrisis::Calm, FactionCrisis::Coup)) {
            error = "玩家派系字段无效。";
            return false;
        }
    }
    const bool extinct = candidate.phase == GamePhase::Finished && candidate.ending == GameEnding::Extinction;
    if ((!extinct && candidate.roster.size() < 2U) || candidate.roster.size() > 64U) {
        error = "角色名单人数无效。";
        return false;
    }
    std::unordered_set<std::string> rosterNames;
    std::unordered_set<std::string> itemOwners;
    for (const Character& character : candidate.roster) {
        if (character.name.empty() || !rosterNames.insert(character.name).second || character.level <= 0 ||
            character.level > 100 || character.experience < 0 || character.growthPoints < 0 || character.life < 0 ||
            character.fatigue < 0 || character.fatigue > 100 || character.loyalty < 0 || character.loyalty > 100) {
            error = "角色名单存在重复或非法属性。";
            return false;
        }
        for (const int attribute : character.attributes.values) {
            if (attribute < kMinimumAttribute || attribute > kMaximumAttribute) {
                error = "角色属性超出范围。";
                return false;
            }
        }
        if (character.life > maximumLife(character)) {
            error = "角色生命超过上限。";
            return false;
        }
        for (std::size_t slot = 0U; slot < character.equipment.size(); ++slot) {
            const auto& item = character.equipment[slot];
            if (!item) continue;
            if (!validStoredItem(*item) || !item->equipmentSlot ||
                *item->equipmentSlot != static_cast<EquipmentSlot>(slot) || !itemOwners.insert(item->id).second ||
                item->condition == ItemCondition::Scrapped) {
                error = "装备物品字段、编号或所有权无效。";
                return false;
            }
        }
    }
    if ((!extinct && candidate.squads.empty()) || candidate.squads.size() > 8U) {
        error = "永久小队数量无效。";
        return false;
    }
    std::unordered_set<std::string> assignedMembers;
    for (const PermanentSquad& squad : candidate.squads) {
        if (squad.name.empty() || squad.captain.empty() || squad.members.size() < 2U || squad.members.size() > 8U ||
            squad.fatigue < 0 || squad.fatigue > 100 || squad.eliteExperience < 0 ||
            !enumInRange(squad.station, WorldLocationId::Camp, WorldLocationId::CliffTradeRoad) ||
            (squad.station != WorldLocationId::Camp && !candidate.outposts[indexOf(squad.station)])) {
            error = "永久小队字段无效。";
            return false;
        }
        std::unordered_set<std::string> members;
        for (const std::string& member : squad.members) {
            const Character* character = findRosterCharacter(candidate.roster, member);
            if (character == nullptr || character->life <= 0 || !members.insert(member).second ||
                !assignedMembers.insert(member).second) {
                error = "小队成员不在角色名单、已阵亡或重复。";
                return false;
            }
        }
        if (members.count(squad.captain) == 0U) {
            error = "小队长必须属于小队。";
            return false;
        }
    }
    const auto validateSupervisor = [&](const std::string& name, const BuildingId building, const Occupation occupation,
                                        const char* label) {
        if (name.empty()) return true;
        const Character* person = findRosterCharacter(candidate.roster, name);
        if (!person || person->occupation != occupation || !candidate.buildings[indexOf(building)] ||
            assignedMembers.count(name) != 0U) {
            error = std::string(label) + "负责人不满足职业、建筑或小队条件。";
            return false;
        }
        return true;
    };
    if (!validateSupervisor(candidate.workshopSupervisor, BuildingId::Workshop, Occupation::Crafter, "武备工坊") ||
        !validateSupervisor(candidate.healerSupervisor, BuildingId::HealerHut, Occupation::Healer, "医者小屋")) {
        return false;
    }
    if (!enumInRange(candidate.pendingEvent.kind, PendingEventKind::Refugees, PendingEventKind::FactionDemand)) {
        error = "待决事件类型无效。";
        return false;
    }
    for (const Item& item : candidate.stockpile) {
        if (!validStoredItem(item) || !itemOwners.insert(item.id).second) {
            error = "共享仓库物品字段、编号或所有权冲突。";
            return false;
        }
    }
    for (const Item& item : candidate.war.lockedEquipment) {
        if (!validStoredItem(item) || item.condition == ItemCondition::Scrapped || !itemOwners.insert(item.id).second) {
            error = "军队锁定装备字段或与其他位置冲突。";
            return false;
        }
    }
    if (candidate.phase == GamePhase::Mission) {
        if (!candidate.activeMission) {
            error = "任务阶段缺少任务状态。";
            return false;
        }
        const OperationResult missionCheck = ExpansionGame::validateState(*candidate.activeMission);
        if (!missionCheck) {
            error = "任务阶段状态无效：" + missionCheck.message;
            return false;
        }
        if (candidate.squads.empty() || candidate.activeMission->squad.members.size() > candidate.squads.front().members.size()) {
            error = "活动地图任务与永久小队不一致。";
            return false;
        }
        for (const Character& member : candidate.activeMission->squad.members) {
            const Character* rosterMember = findRosterCharacter(candidate.roster, member.name);
            if (std::find(candidate.squads.front().members.begin(), candidate.squads.front().members.end(),
                          member.name) == candidate.squads.front().members.end() ||
                rosterMember == nullptr) {
                error = "活动地图任务成员不属于出发小队。";
                return false;
            }
            for (std::size_t slot = 0U; slot < member.equipment.size(); ++slot) {
                const auto& missionItem = member.equipment[slot];
                const auto& rosterItem = rosterMember->equipment[slot];
                if (missionItem.has_value() != rosterItem.has_value() ||
                    (missionItem && !sameItem(*missionItem, *rosterItem))) {
                    error = "活动地图任务的角色装备与长期角色镜像不一致。";
                    return false;
                }
            }
        }
        for (const Item& item : candidate.activeMission->backpack.items()) {
            if (!validStoredItem(item) || item.condition == ItemCondition::Scrapped ||
                !itemOwners.insert(item.id).second) {
                error = "任务背包装备字段或与其他位置冲突。";
                return false;
            }
        }
        for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
            if ((candidate.discovered[index] && !candidate.activeMission->worldDiscovered[index]) ||
                (candidate.outposts[index] && !candidate.activeMission->outposts[index])) {
                error = "活动任务的地图发现或前哨状态落后于主状态。";
                return false;
            }
        }
    } else if (candidate.activeMission) {
        error = "非任务阶段不能保留活动任务。";
        return false;
    }
    const bool preparedArmy = population_rules::hasPreparedArmy(candidate);
    if (preparedArmy &&
        (candidate.war.warriors <= 0 || candidate.war.militia < 0 ||
         candidate.war.warriors + population_rules::garrisonAllocation(candidate) > candidate.warriors ||
         candidate.war.craftsmanshipPower < 0 || candidate.war.craftsmanshipPower > 4)) {
        error = "已组建军队字段或受训战士分配无效。";
        return false;
    }
    if (!preparedArmy && (candidate.war.warriors != 0 || candidate.war.militia != 0 ||
                          !candidate.war.lockedEquipment.empty() || candidate.war.craftsmanshipPower != 0)) {
        error = "未组建军队不能保留兵力或锁定装备。";
        return false;
    }
    if (candidate.phase == GamePhase::War) {
        if (!candidate.war.active || candidate.war.commander.empty() || candidate.war.warriors < 0 ||
            candidate.war.militia < 0 || candidate.war.playerPower < 0 || candidate.war.enemyPower <= 0) {
            error = "战争阶段字段无效。";
            return false;
        }
    } else if (candidate.war.active) {
        error = "非战争阶段不能保留活动战争。";
        return false;
    }
    if (candidate.phase == GamePhase::Finished && candidate.ending == GameEnding::None) {
        error = "结束阶段必须有结局。";
        return false;
    }
    if (candidate.phase != GamePhase::Finished && candidate.phase != GamePhase::Sandbox &&
        candidate.ending != GameEnding::None) {
        error = "未结束战役不能提前写入结局。";
        return false;
    }
    if (candidate.chronicle.empty() || candidate.chronicle.size() > 200U || candidate.leadershipHistory.empty()) {
        error = "编年史或首领历史不完整。";
        return false;
    }
    error.clear();
    return true;
}

} // namespace tribe
