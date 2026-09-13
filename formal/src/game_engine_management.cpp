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
ActionResult GameEngine::build(const BuildingId building, const int workers) {
    ActionResult result;
    if (workers < 2 || workers > 7) return rejected("建造投入人数必须为2至7人。用法：建造 <建筑> [投入人数]。 ");
    if (!canSpendAction(result, workers)) return result;
    if (workers > population_rules::availablePopulation(state_)) return rejected("可用人口不足，无法投入这么多人建造。 ");
    if (state_.buildings[indexOf(building)]) return rejected("该唯一建筑已经建成。");
    static const std::array<int, kBuildingCount> woodCosts{{8, 10, 8, 6, 8, 6, 12}};
    static const std::array<int, kBuildingCount> stoneCosts{{2, 2, 6, 2, 4, 4, 4}};
    const int woodCost = woodCosts[indexOf(building)];
    const int stoneCost = stoneCosts[indexOf(building)];
    if (state_.wood < woodCost || state_.stone < stoneCost) {
        return rejected("建造需要木材" + std::to_string(woodCost) + "、石料" + std::to_string(stoneCost) +
                        "，资源不足。");
    }
    GameState candidate = state_;
    candidate.wood -= woodCost;
    candidate.stone -= stoneCost;
    candidate.buildings[indexOf(building)] = true;
    candidate.stability = std::min(100, candidate.stability + 2);
    spendAction(candidate, workers);
    return commit(std::move(candidate), "建筑完成，投入" + std::to_string(workers) + "人口，部落稳定提高2。", true);
}

ActionResult GameEngine::research(const TechnologyId technology, const int workers) {
    ActionResult result;
    if (state_.technologies[indexOf(technology)]) return rejected("该技术已经研究完成。");
    const int raw = static_cast<int>(technology);
    const int tier = raw % 3;
    if (tier > 0 && !state_.technologies[static_cast<std::size_t>(raw - 1)])
        return rejected("必须先研究同路线的前一级技术。");
    if (tier == 2 && !state_.buildings[indexOf(BuildingId::Workshop)]) return rejected("高级技术需要先建武备工坊。");
    const int foodCost = 3 + tier * 2;
    const int woodCost = 2 + tier;
    const int requiredWorkers = 2 + tier;
    const int actualWorkers = workers == 0 ? requiredWorkers : workers;
    if (actualWorkers < requiredWorkers || actualWorkers > 7)
        return rejected("研究投入人数必须为" + std::to_string(requiredWorkers) + "至7人。 ");
    if (!canSpendAction(result, actualWorkers)) return result;
    if (actualWorkers > population_rules::availablePopulation(state_)) return rejected("可用人口不足，无法投入这么多人研究。 ");
    if (state_.food < foodCost || state_.wood < woodCost) return rejected("研究所需食物或木材不足。");
    GameState candidate = state_;
    candidate.food -= foodCost;
    candidate.wood -= woodCost;
    candidate.technologies[indexOf(technology)] = true;
    spendAction(candidate, actualWorkers);
    return commit(std::move(candidate), "研究完成，投入" + std::to_string(actualWorkers) + "人口。", true);
}

ActionResult GameEngine::restSquad() {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.squads.empty() || state_.squads.front().fatigue == 0) return rejected("小队当前无需休整。");
    GameState candidate = state_;
    const bool staffedHealerHut =
        candidate.buildings[indexOf(BuildingId::HealerHut)] && candidate.workforce.healers > 0;
    const int recovery = (staffedHealerHut ? 40 : 30) + 5 * medicineRank(candidate);
    PermanentSquad& squad = candidate.squads.front();
    for (const std::string& name : squad.members) {
        Character* member = findRosterCharacter(candidate.roster, name);
        if (member != nullptr) member->fatigue = std::max(0, member->fatigue - recovery);
    }
    squad.fatigue = permanentSquadFatigue(squad, candidate.roster);
    spendAction(candidate);
    return commit(std::move(candidate), "小队在营地休整，疲劳恢复" + std::to_string(recovery) + "。", true);
}

ActionResult GameEngine::assignWorkforce(const WorkforceRole role, const int count) {
    if (count < 0 || count > 6) return rejected("岗位人数为0至6。 ");
    if ((role == WorkforceRole::Crafters || role == WorkforceRole::Healers || role == WorkforceRole::Scouts ||
         role == WorkforceRole::Envoys || role == WorkforceRole::CampGuards) &&
        count > 1) {
        return rejected("工匠、医者、侦察、使者和营地守卫当前只区分未配置或已配置，请输入0或1。 ");
    }
    GameState candidate = state_;
    int* target = nullptr;
    switch (role) {
        case WorkforceRole::FoodCrew:
            target = &candidate.workforce.foodCrew;
            break;
        case WorkforceRole::WoodCrew:
            target = &candidate.workforce.woodCrew;
            break;
        case WorkforceRole::StoneCrew:
            target = &candidate.workforce.stoneCrew;
            break;
        case WorkforceRole::HerbCrew:
            target = &candidate.workforce.herbCrew;
            break;
        case WorkforceRole::Crafters:
            target = &candidate.workforce.crafters;
            break;
        case WorkforceRole::Healers:
            target = &candidate.workforce.healers;
            break;
        case WorkforceRole::Scouts:
            target = &candidate.workforce.scouts;
            break;
        case WorkforceRole::Envoys:
            target = &candidate.workforce.envoys;
            break;
        case WorkforceRole::CampGuards:
            target = &candidate.workforce.campGuards;
            break;
        case WorkforceRole::Housing:
            target = &candidate.workforce.housing;
            break;
    }
    const int previous = *target;
    if (state_.workforceReassignmentRequired && count >= previous) {
        return rejected("劳力待重分配时只能降低岗位人数。 ");
    }
    *target = count;
    const bool legacyResourceRole = role == WorkforceRole::FoodCrew || role == WorkforceRole::WoodCrew ||
                                    role == WorkforceRole::StoneCrew || role == WorkforceRole::HerbCrew;
    return commit(std::move(candidate), legacyResourceRole ? "旧资源队配置已记录但不再限制采集；任务请直接指定资源和人数。"
                                                           : "人口岗位分配已调整。",
                  false);
}

ActionResult GameEngine::assignOutpostGuards(const WorldLocationId location, const int count) {
    if (location == WorldLocationId::Camp || count < 0 || count > 1) {
        return rejected("前哨守卫当前只区分无人或有人维护，请输入0或1，且营地不使用此前哨命令。 ");
    }
    if (!state_.outposts[indexOf(location)]) return rejected("只能为已建前哨配置守卫。 ");
    if (state_.workforceReassignmentRequired && count >= state_.workforce.outpostGuards[indexOf(location)]) {
        return rejected("劳力待重分配时只能降低前哨守卫人数。 ");
    }
    GameState candidate = state_;
    candidate.workforce.outpostGuards[indexOf(location)] = count;
    return commit(std::move(candidate), "前哨守卫已调整；无人前哨连续两季会荒废。", false);
}

ActionResult GameEngine::craft(const std::string_view recipe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (!state_.buildings[indexOf(BuildingId::Workshop)] || state_.workforce.crafters < 1)
        return rejected("制造需要已建武备工坊并至少安排1名工匠维护。 ");
    struct Recipe {
        const char* key;
        const char* name;
        int wood;
        int stone;
        int hides;
        int herbs;
        EquipmentSlot slot;
        Attribute attribute;
        int bonus;
        TechnologyId prerequisite;
    };
    const std::array<Recipe, 9> recipes{{
        {"knife", "石刀", 1, 2, 0, 0, EquipmentSlot::MainHand, Attribute::Strength, 1, TechnologyId::FoodPreservation},
        {"spear", "石矛", 2, 3, 0, 0, EquipmentSlot::MainHand, Attribute::Strength, 2, TechnologyId::FoodPreservation},
        {"shield", "木盾", 4, 0, 0, 0, EquipmentSlot::OffHand, Attribute::Endurance, 2, TechnologyId::FoodPreservation},
        {"armor", "皮甲", 0, 0, 4, 0, EquipmentSlot::Body, Attribute::Endurance, 2, TechnologyId::FoodPreservation},
        {"shoes", "草鞋", 1, 0, 0, 1, EquipmentSlot::LegsFeet, Attribute::Agility, 1, TechnologyId::FoodPreservation},
        {"flintspear", "燧石长矛", 2, 5, 0, 0, EquipmentSlot::MainHand, Attribute::Strength, 3,
         TechnologyId::FlintSpear},
        {"reinforcedshield", "加固木盾", 6, 2, 0, 0, EquipmentSlot::OffHand, Attribute::Endurance, 3,
         TechnologyId::ShieldWall},
        {"cloak", "猎人披风", 1, 0, 5, 1, EquipmentSlot::Body, Attribute::Perception, 3, TechnologyId::HerbalKnowledge},
        {"charm", "护符", 0, 1, 0, 3, EquipmentSlot::Accessory, Attribute::Willpower, 3, TechnologyId::HerbalKnowledge},
    }};
    const auto found = std::find_if(recipes.begin(), recipes.end(),
                                    [&](const Recipe& value) { return equalsAny(recipe, {value.key, value.name}); });
    if (found == recipes.end())
        return rejected("未知配方；可制造石刀、石矛、木盾、皮甲、草鞋、燧石长矛、加固木盾、猎人披风、护符。 ");
    if (found->prerequisite != TechnologyId::FoodPreservation && !state_.technologies[indexOf(found->prerequisite)])
        return rejected("该进阶配方尚未由技术解锁。 ");
    if (state_.wood < found->wood || state_.stone < found->stone || state_.hides < found->hides ||
        state_.herbs < found->herbs)
        return rejected("制造材料不足。 ");
    GameState candidate = state_;
    candidate.wood -= found->wood;
    candidate.stone -= found->stone;
    candidate.hides -= found->hides;
    candidate.herbs -= found->herbs;
    // 物品可能已装备、携带或从仓库报废；因此不能以当前仓库大小作为编号来源。
    // 在扣除材料后的候选状态中预留一个持久化序号，提交失败不会污染真实状态。
    if (candidate.nextItemSerial == 0U || candidate.nextItemSerial == std::numeric_limits<std::uint32_t>::max()) {
        return rejected("装备编号已耗尽，无法继续制造。 ");
    }
    Item item;
    item.id = std::string(found->key) + "_" + std::to_string(candidate.season) + "_" +
              std::to_string(candidate.nextItemSerial++);
    item.name = found->name;
    item.weight = 2;
    item.equipmentSlot = found->slot;
    item.bonuses[found->attribute] = found->bonus;
    switch (craftRank(candidate)) {
        case 1:
            item.quality = ItemQuality::Fine;
            break;
        case 2:
            item.quality = ItemQuality::Rare;
            break;
        case 3:
            item.quality = ItemQuality::Legendary;
            break;
        default:
            item.quality = ItemQuality::Common;
            break;
    }
    const std::string qualityName = itemQualityName(item.quality);
    candidate.stockpile.push_back(std::move(item));
    spendAction(candidate);
    return commit(std::move(candidate), "武备工坊完成制造，物品已进入共享仓库，品质为" + qualityName + "。", true);
}

ActionResult GameEngine::repair(const std::string_view itemId) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (!state_.buildings[indexOf(BuildingId::Workshop)] || state_.workforce.crafters < 1)
        return rejected("维修需要武备工坊与工匠。 ");
    GameState candidate = state_;
    auto item = std::find_if(candidate.stockpile.begin(), candidate.stockpile.end(),
                             [&](const Item& value) { return value.id == itemId; });
    if (item == candidate.stockpile.end()) return rejected("仓库中没有这个物品编号。 ");
    if (item->condition == ItemCondition::Scrapped) return rejected("报废物品不能维修，只能报废清理。 ");
    if (item->condition == ItemCondition::Intact) return rejected("该物品仍然完好。 ");
    if (candidate.wood < 1 || candidate.stone < 1) return rejected("维修需要木材1、石料1。 ");
    --candidate.wood;
    --candidate.stone;
    item->condition = ItemCondition::Intact;
    spendAction(candidate);
    return commit(std::move(candidate), "装备已维修至完好状态。", true);
}

ActionResult GameEngine::scrap(const std::string_view itemId) {
    GameState candidate = state_;
    auto item = std::find_if(candidate.stockpile.begin(), candidate.stockpile.end(),
                             [&](const Item& value) { return value.id == itemId; });
    if (item == candidate.stockpile.end()) return rejected("只能报废仓库中的物品。 ");
    if (item->condition != ItemCondition::Scrapped) return rejected("只有报废状态的装备可以清理。 ");
    candidate.stockpile.erase(item);
    return commit(std::move(candidate), "已清理报废装备。", false);
}

ActionResult GameEngine::equipPerson(const std::string_view person, const std::string_view slotName,
                                     const std::string_view itemId) {
    const auto slot = parseEquipmentSlot(slotName);
    if (!slot) return rejected("未知装备栏。 ");
    GameState candidate = state_;
    Character* character = findRosterCharacter(candidate.roster, person);
    if (character == nullptr) return rejected("没有这个人物。 ");
    auto item = std::find_if(candidate.stockpile.begin(), candidate.stockpile.end(),
                             [&](const Item& value) { return value.id == itemId; });
    if (item == candidate.stockpile.end() || item->condition == ItemCondition::Scrapped)
        return rejected("仓库中没有可装备的该物品。 ");
    if (!item->equipmentSlot || *item->equipmentSlot != *slot) return rejected("物品与指定装备栏不匹配。 ");
    Item moved = *item;
    candidate.stockpile.erase(item);
    if (character->equipment[indexOf(*slot)]) candidate.stockpile.push_back(*character->equipment[indexOf(*slot)]);
    character->equipment[indexOf(*slot)] = std::move(moved);
    return commit(std::move(candidate), "人物装备已从共享仓库转移。", false);
}

ActionResult GameEngine::unequipPerson(const std::string_view person, const std::string_view slotName) {
    const auto slot = parseEquipmentSlot(slotName);
    if (!slot) return rejected("未知装备栏。 ");
    GameState candidate = state_;
    Character* character = findRosterCharacter(candidate.roster, person);
    if (character == nullptr || !character->equipment[indexOf(*slot)]) return rejected("该人物此栏没有装备。 ");
    candidate.stockpile.push_back(*character->equipment[indexOf(*slot)]);
    character->equipment[indexOf(*slot)].reset();
    return commit(std::move(candidate), "装备已卸回共享仓库。", false);
}

ActionResult GameEngine::appoint(const std::string_view person, const std::string_view role) {
    GameState candidate = state_;
    Character* character = findRosterCharacter(candidate.roster, person);
    if (character == nullptr) return rejected("没有这个人物。 ");
    if (isPermanentSquadMember(candidate, person)) return rejected("小队成员不能同时担任管理岗位。 ");
    if (equalsAny(role, {"workshop", "武备工坊"})) {
        if (!candidate.buildings[indexOf(BuildingId::Workshop)] || candidate.workforce.crafters == 0)
            return rejected("任命工坊负责人需要已建武备工坊并配置工匠。 ");
        if (character->occupation != Occupation::Crafter) return rejected("武备工坊负责人必须是工匠。 ");
        candidate.workshopSupervisor = character->name;
    } else if (equalsAny(role, {"healer", "医者小屋"})) {
        if (!candidate.buildings[indexOf(BuildingId::HealerHut)] || candidate.workforce.healers == 0)
            return rejected("任命医者负责人需要已建医者小屋并配置医者。 ");
        if (character->occupation != Occupation::Healer) return rejected("医者小屋负责人必须是医者。 ");
        candidate.healerSupervisor = character->name;
    } else
        return rejected("可任命岗位：武备工坊、医者小屋。 ");
    return commit(std::move(candidate), "具名人物已任命为建筑负责人。", false);
}

ActionResult GameEngine::unappoint(const std::string_view role) {
    GameState candidate = state_;
    if (equalsAny(role, {"workshop", "武备工坊"})) {
        if (candidate.workshopSupervisor.empty()) return rejected("武备工坊当前没有负责人。 ");
        candidate.workshopSupervisor.clear();
    } else if (equalsAny(role, {"healer", "医者小屋"})) {
        if (candidate.healerSupervisor.empty()) return rejected("医者小屋当前没有负责人。 ");
        candidate.healerSupervisor.clear();
    } else {
        return rejected("可卸任岗位：武备工坊、医者小屋。 ");
    }
    return commit(std::move(candidate), "负责人已卸任。", false);
}

ActionResult GameEngine::configureSquad(const std::vector<std::string>& args) {
    if (args.size() < 3U || args.size() > 9U) return rejected("用法：squad configure <队长> <成员2至8人>。 ");
    GameState candidate = state_;
    PermanentSquad& squad = candidate.squads.front();
    squad.captain = args[0];
    squad.members.assign(args.begin() + 1, args.end());
    if (std::find(squad.members.begin(), squad.members.end(), squad.captain) == squad.members.end())
        squad.members.insert(squad.members.begin(), squad.captain);
    if ((!candidate.workshopSupervisor.empty() &&
         std::find(squad.members.begin(), squad.members.end(), candidate.workshopSupervisor) != squad.members.end()) ||
        (!candidate.healerSupervisor.empty() &&
         std::find(squad.members.begin(), squad.members.end(), candidate.healerSupervisor) != squad.members.end())) {
        return rejected("现任建筑负责人不能编入永久小队；请先卸任。 ");
    }
    std::string error;
    if (!validateState(candidate, error)) return rejected("小队编制不合法：" + error);
    return commit(std::move(candidate), "小队编制已更新。", false);
}

ActionResult GameEngine::treat(const std::string_view squadName) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (!state_.buildings[indexOf(BuildingId::HealerHut)] || state_.workforce.healers < 1)
        return rejected("医治需要医者小屋与至少1名医者维护。 ");
    GameState candidate = state_;
    auto squad = std::find_if(candidate.squads.begin(), candidate.squads.end(),
                              [&](const PermanentSquad& value) { return value.name == squadName; });
    if (squad == candidate.squads.end() || candidate.herbs < 1) return rejected("找不到小队或草药不足。 ");
    --candidate.herbs;
    for (const std::string& member : squad->members)
        if (Character* character = findRosterCharacter(candidate.roster, member)) {
            character->life = std::min(maximumLife(*character), character->life + 20 + 5 * medicineRank(candidate));
            character->fatigue = std::max(0, character->fatigue - 25 - 5 * medicineRank(candidate));
        }
    squad->fatigue = permanentSquadFatigue(*squad, candidate.roster);
    spendAction(candidate);
    return commit(std::move(candidate), "医者消耗1草药，为全队治疗并恢复精力。", true);
}

} // namespace tribe
