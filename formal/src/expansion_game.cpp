#include "tribe/expansion_game.hpp"

#include "command_parser.hpp"
#include "game_command_catalog.hpp"
#include "state_safety.hpp"
#include "world_map_catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tribe {
namespace {

using command_parser::Command;
using command_parser::equalsAny;
using command_parser::parse;

/// 用途：统计任务载货总量。输入：任务状态。输出：五类货物之和；无状态修改。
/// 失败：无。不变量：调用方须先保证各货物字段非负。
int cargoTotal(const ExpansionState& state) {
    return state.cargoFood + state.cargoWood + state.cargoStone + state.cargoHerbs + state.cargoHides;
}

/// 用途：创建带职业加成的初始任务成员。输入：姓名和职业。输出：合法角色；无外部状态修改。
/// 失败：无。不变量：生命初始化为依据属性计算的上限。
Character makeMember(const char* name, Occupation occupation) {
    Character member{name, occupation};
    member.attributes = Attributes{5};
    member.loyalty = 70;
    if (occupation == Occupation::Warrior) {
        member.attributes[Attribute::Strength] = 9;
        member.attributes[Attribute::Endurance] = 8;
    }
    if (occupation == Occupation::Scout) {
        member.attributes[Attribute::Agility] = 9;
        member.attributes[Attribute::Perception] = 8;
    }
    if (occupation == Occupation::Hunter) {
        member.attributes[Attribute::Survival] = 8;
        member.attributes[Attribute::Perception] = 8;
    }
    member.life = maximumLife(member);
    return member;
}

/// 用途：构造失败的任务校验回执。输入：消息。输出：success 为 false 的结果；无状态修改。
/// 失败：无。不变量：不暗示任何状态已提交。
OperationResult invalid(std::string message) { return {false, std::move(message)}; }
/// 用途：构造成功的任务校验回执。输入：消息。输出：success 为 true 的结果；无状态修改。
/// 失败：无。不变量：只表达校验结果，不写入任务状态。
OperationResult valid(std::string message) { return {true, std::move(message)}; }

} // namespace

ExpansionGame::ExpansionGame(std::uint32_t seed, std::size_t squadSize) {
    if (squadSize < kMinimumSquadSize || squadSize > kMaximumSquadSize)
        throw std::invalid_argument("地图小队人数必须为2至8人。");
    state_.seed = seed;
    state_.squad.name = "晨火队";
    state_.squad.cohesion = 72;
    static const std::array<std::pair<const char*, Occupation>, 8> roster{{{"青枝", Occupation::Scout},
                                                                           {"石刃", Occupation::Warrior},
                                                                           {"苍眼", Occupation::Hunter},
                                                                           {"白榆", Occupation::Healer},
                                                                           {"逐鹿", Occupation::Hunter},
                                                                           {"岩槌", Occupation::Crafter},
                                                                           {"芦风", Occupation::Hunter},
                                                                           {"河矛", Occupation::Warrior}}};
    for (std::size_t index = 0; index < squadSize; ++index)
        state_.squad.members.push_back(makeMember(roster[index].first, roster[index].second));
    state_.worldDiscovered[0] = true;
    state_.outposts[0] = true;
}

ExpansionGame::ExpansionGame(ExpansionState state) : state_(std::move(state)) {
    const OperationResult result = validateState(state_);
    if (!result) throw std::invalid_argument("地图任务状态无效：" + result.message);
}

ExpansionCommandResult ExpansionGame::execute(std::string_view input) {
    const Command command = parse(input);
    if (command.verb.empty()) return {};
    const auto commandId = game_command_catalog::classify(command);
    const auto hasArity = [&](const std::size_t count) { return game_command_catalog::hasArity(command, count); };
    if (commandId == game_command_catalog::CommandId::Look || commandId == game_command_catalog::CommandId::Map)
        return hasArity(0U) ? ExpansionCommandResult{true, true, false, false, lookText()}
                            : rejected("用法：look / 查看");
    if (state_.encounterLife > 0) {
        if (commandId == game_command_catalog::CommandId::Attack && hasArity(0U)) return attackEncounter();
        if (commandId == game_command_catalog::CommandId::Defend && hasArity(0U)) return defendEncounter();
        if (commandId == game_command_catalog::CommandId::Retreat && hasArity(0U)) return retreatEncounter();
        return rejected("岩牙巡逻拦住道路；只能攻击、防御、撤退或查看。");
    }
    if (commandId == game_command_catalog::CommandId::Move)
        return hasArity(1U) ? move(command.args.front()) : rejected("用法：move <相邻地点>");
    if (commandId == game_command_catalog::CommandId::Gather)
        return hasArity(1U) ? gather(command.args.front()) : rejected("用法：gather <资源>");
    if (commandId == game_command_catalog::CommandId::Attack)
        return hasArity(0U) ? attackEncounter() : rejected("用法：attack");
    if (commandId == game_command_catalog::CommandId::Use && hasArity(1U) &&
        equalsAny(command.args.front(), {"herb", "herbs", "草药"}))
        return useHerb();
    if (commandId == game_command_catalog::CommandId::BuildOutpost ||
        (commandId == game_command_catalog::CommandId::Build && hasArity(1U) &&
         equalsAny(command.args.front(), {"outpost", "前哨"})))
        return buildOutpost();
    if (commandId == game_command_catalog::CommandId::Settle) return hasArity(0U) ? settle() : rejected("用法：settle");
    return {};
}

ExpansionCommandResult ExpansionGame::move(std::string_view target) {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const auto destination = world_map::parse(target);
    if (!destination) return rejected("未知地点；输入地图可查看1至16号地点。");
    const WorldLocationId current = static_cast<WorldLocationId>(state_.worldLocation);
    if (*destination == current) return rejected("小队已经在这里。");
    if (!world_map::adjacent(current, *destination)) return rejected("两地不相邻，不能跨越道路移动。");
    ExpansionState candidate = state_;
    candidate.worldLocation = static_cast<int>(indexOf(*destination));
    const bool discovered = !candidate.worldDiscovered[indexOf(*destination)];
    candidate.worldDiscovered[indexOf(*destination)] = true;
    recordTurn(candidate, 2, 1);
    std::string message = "小队沿道路抵达" + world_map::locations()[indexOf(*destination)].name + "。";
    if (discovered) message += " 新地点已发现。";
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::gather(std::string_view resource) {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    if (state_.missionKind != MissionKind::Gather) return rejected("前哨建设任务只能运输建材，不能额外采集。");
    if (state_.harvestActions >= 4) return rejected("本次任务的采集时段已用完，请前往营地或前哨结算。");
    const int at = state_.worldLocation;
    const WorldLocationId location = static_cast<WorldLocationId>(at);
    std::optional<ResourceKind> kind;
    int base = 0;
    if (equalsAny(resource, {"food", "食物", "粮食"}) && world_map::supportsResource(location, ResourceKind::Food)) {
        kind = ResourceKind::Food;
        base = 6 + state_.foodGatherBonus;
    } else if (equalsAny(resource, {"wood", "木材"}) && world_map::supportsResource(location, ResourceKind::Wood)) {
        kind = ResourceKind::Wood;
        base = 6;
    } else if (equalsAny(resource, {"stone", "石料", "石头"}) &&
               world_map::supportsResource(location, ResourceKind::Stone)) {
        kind = ResourceKind::Stone;
        base = 5;
    } else if (equalsAny(resource, {"herb", "herbs", "草药"}) &&
               world_map::supportsResource(location, ResourceKind::Herbs)) {
        kind = ResourceKind::Herbs;
        base = 4 + state_.herbGatherBonus;
    } else if (equalsAny(resource, {"hide", "hides", "兽皮"}) &&
               world_map::supportsResource(location, ResourceKind::Hides)) {
        kind = ResourceKind::Hides;
        base = 4;
    } else
        return rejected("当前地点没有这种资源。");
    if (*kind != state_.assignedResource &&
        !(*kind == ResourceKind::Hides && state_.assignedResource == ResourceKind::Food))
        return rejected("本次任务由指定资源队执行，不能混采。");
    const int room = state_.cargoCapacity - cargoTotal(state_);
    if (room <= 0) return rejected("小队载货已满，请结算。");
    ExpansionState candidate = state_;
    const int gain = std::min(room, base + std::max(0, candidate.crewSize - 2));
    std::string_view cargoName;
    switch (*kind) {
        case ResourceKind::Food:
            candidate.cargoFood += gain;
            cargoName = "食物";
            break;
        case ResourceKind::Wood:
            candidate.cargoWood += gain;
            cargoName = "木材";
            break;
        case ResourceKind::Stone:
            candidate.cargoStone += gain;
            cargoName = "石料";
            break;
        case ResourceKind::Herbs:
            candidate.cargoHerbs += gain;
            cargoName = "草药";
            break;
        case ResourceKind::Hides:
            candidate.cargoHides += gain;
            cargoName = "兽皮";
            break;
    }
    ++candidate.harvestActions;
    recordTurn(candidate, 4, 2);
    return commit(std::move(candidate),
                  "小队采集" + std::string(cargoName) + "，装载" + std::to_string(gain) + "单位。", true);
}

ExpansionCommandResult ExpansionGame::buildOutpost() {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    if (at == 0U) return rejected("营地无需建造前哨。");
    if (state_.outposts[at]) return rejected("该地点已经有前哨。");
    if (state_.worldLocation == static_cast<int>(WorldLocationId::RockfangFort) && !state_.encounterDefeated)
        return rejected("岩牙要塞仍有敌对巡逻，不能建造前哨。");
    if (state_.cargoWood < 6 || state_.cargoStone < 4) return rejected("建造前哨需要现场携带木材6、石料4。");
    ExpansionState candidate = state_;
    candidate.cargoWood -= 6;
    candidate.cargoStone -= 4;
    candidate.outposts[at] = true;
    recordTurn(candidate, 5, 3);
    return commit(std::move(candidate), "小队建成前哨；这里现在可作为结算点。", true);
}

ExpansionCommandResult ExpansionGame::settle() {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    if (!state_.outposts[at]) return rejected("这里只能停留；请在营地或已建前哨结算。");
    ExpansionState candidate = state_;
    // 1. 只允许在已建立的结算点结束野外阶段；未结算载货不会写回部落库存。
    candidate.phase = ExpansionPhase::Settled;
    candidate.settled = true;
    // 2. 保留载货和背包给上层 GameEngine 统一入账，避免地图层和部落层重复增加资源。
    recordTurn(candidate, 1, 1);
    for (Character& member : candidate.squad.members) {
        const OperationResult gained = gainExperience(member, 15 + candidate.harvestActions * 10);
        if (!gained) return rejected("经验结算失败：" + gained.message);
    }
    candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 2);
    return commit(std::move(candidate), at == 0U ? "小队在燧火营地结算。" : "小队在前哨结算并驻留。", true);
}

ExpansionCommandResult ExpansionGame::attackEncounter() {
    if (state_.worldLocation != static_cast<int>(WorldLocationId::RockfangFort) || state_.encounterDefeated)
        return rejected("这里没有可攻击的遭遇。");
    ExpansionState candidate = state_;
    if (candidate.encounterLife == 0) candidate.encounterLife = 14;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int damage = std::max(2, attributes[Attribute::Strength] / 2 + attributes[Attribute::Agility] / 4);
    candidate.encounterLife = std::max(0, candidate.encounterLife - damage);
    recordTurn(candidate, 4, 2);
    if (candidate.encounterLife == 0) {
        candidate.encounterDefeated = true;
        Item badge;
        badge.id = "rockfang_badge_" + std::to_string(candidate.seed);
        badge.name = "岩牙巡逻徽记";
        badge.weight = 1;
        badge.equipmentSlot = EquipmentSlot::Accessory;
        badge.bonuses[Attribute::Willpower] = 1;
        const OperationResult stored = candidate.backpack.pickupFree(std::move(badge));
        return commit(std::move(candidate),
                      stored ? "小队击退岩牙巡逻，徽记已放入任务背包；要塞仍需军队占领。"
                             : "小队击退岩牙巡逻；任务背包已满，未带走徽记。",
                      true);
    }
    const int loss = std::max(1, 5 - attributes[Attribute::Endurance] / 4);
    leader.life = std::max(1, leader.life - loss);
    return commit(std::move(candidate),
                  "小队攻击巡逻，敌军生命-" + std::to_string(damage) + "；队长生命-" + std::to_string(loss) + "。",
                  true);
}

ExpansionCommandResult ExpansionGame::defendEncounter() {
    if (state_.encounterLife <= 0 || state_.worldLocation != static_cast<int>(WorldLocationId::RockfangFort))
        return rejected("当前没有遭遇战。");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const int loss = std::max(1, 3 - effectiveAttributes(leader)[Attribute::Endurance] / 5);
    leader.life = std::max(1, leader.life - loss);
    recordTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队结阵防御，队长生命-" + std::to_string(loss) + "。", true);
}

ExpansionCommandResult ExpansionGame::retreatEncounter() {
    if (state_.encounterLife <= 0 || state_.worldLocation != static_cast<int>(WorldLocationId::RockfangFort))
        return rejected("当前没有遭遇战。");
    ExpansionState candidate = state_;
    candidate.encounterLife = 0;
    candidate.worldLocation = static_cast<int>(WorldLocationId::OldPass);
    recordTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队撤回古老山隘。", true);
}

ExpansionCommandResult ExpansionGame::useHerb() {
    if (state_.cargoHerbs <= 0) return rejected("任务载货中没有草药。");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    --candidate.cargoHerbs;
    leader.life = std::min(maximumLife(leader), leader.life + 18);
    leader.fatigue = std::max(0, leader.fatigue - 20);
    // “任务回合前进”必须与状态一致：草药使用也消耗野外时间；队长本回合不额外积累疲劳。
    recordTurn(candidate, 0, 1);
    return commit(std::move(candidate), "队长使用草药，生命恢复、疲劳下降。", true);
}

std::string ExpansionGame::lookText() const {
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    const WorldLocationId current = static_cast<WorldLocationId>(state_.worldLocation);
    std::ostringstream output;
    output << "北↑  十六地点道路图\n当前地点：" << (state_.worldLocation + 1) << '.' << world_map::locations()[at].name
           << "\n相邻道路：";
    for (const WorldLocationId neighbor : world_map::locations()[at].neighbors)
        output << world_map::direction(current, neighbor) << "→" << (indexOf(neighbor) + 1U) << '.'
               << world_map::locations()[indexOf(neighbor)].name << ' ';
    output << "\n载货 " << cargoTotal(state_) << '/' << state_.cargoCapacity << "：食物" << state_.cargoFood << " 木材"
           << state_.cargoWood << " 石料" << state_.cargoStone << " 草药" << state_.cargoHerbs << " 兽皮"
           << state_.cargoHides << "；采集" << state_.harvestActions << "/4\n可采资源：";
    bool printed = false;
    const auto add = [&](std::string_view name) {
        output << (printed ? "、" : "") << name;
        printed = true;
    };
    if (world_map::supportsResource(current, ResourceKind::Food)) add("食物");
    if (world_map::supportsResource(current, ResourceKind::Wood)) add("木材");
    if (world_map::supportsResource(current, ResourceKind::Stone)) add("石料");
    if (world_map::supportsResource(current, ResourceKind::Herbs)) add("草药");
    if (world_map::supportsResource(current, ResourceKind::Hides)) add("兽皮");
    if (!printed) output << "无";
    output << "\n结算：" << (state_.outposts[at] ? "当前地点可结算" : "需前往营地或前哨") << "。";
    if (state_.worldLocation == static_cast<int>(WorldLocationId::RockfangFort) && !state_.encounterDefeated)
        output << "\n遭遇：岩牙巡逻；可用攻击、防御、撤退。";
    if (state_.encounterLife > 0) output << " 敌军生命" << state_.encounterLife << "。";
    return output.str();
}

OperationResult ExpansionGame::validateState(const ExpansionState& state) {
    if (state.phase != ExpansionPhase::Exploring && state.phase != ExpansionPhase::Settled)
        return invalid("任务阶段无效。");
    if (state.worldLocation < 0 || state.worldLocation >= 16 || !state.worldDiscovered[0] || !state.outposts[0] ||
        !state.worldDiscovered[static_cast<std::size_t>(state.worldLocation)])
        return invalid("地图位置或营地状态无效。");
    if ((state.phase == ExpansionPhase::Settled) != state.settled ||
        (state.settled && !state.outposts[static_cast<std::size_t>(state.worldLocation)]))
        return invalid("结算点状态无效。");
    if (state.turn < 0 || state.cargoFood < 0 || state.cargoWood < 0 || state.cargoStone < 0 || state.cargoHerbs < 0 ||
        state.cargoHides < 0 || state.harvestActions < 0 || state.harvestActions > 4 || state.cargoCapacity <= 0 ||
        cargoTotal(state) > state.cargoCapacity || state.foodGatherBonus < 0 || state.herbGatherBonus < 0 ||
        static_cast<int>(state.missionKind) < static_cast<int>(MissionKind::Gather) ||
        static_cast<int>(state.missionKind) > static_cast<int>(MissionKind::OutpostConstruction) ||
        static_cast<int>(state.assignedResource) < static_cast<int>(ResourceKind::Food) ||
        static_cast<int>(state.assignedResource) > static_cast<int>(ResourceKind::Hides) || state.crewSize < 2 ||
        state.crewSize > 6 || state.encounterLife < 0 ||
        (state.encounterLife > 0 &&
         (state.worldLocation != static_cast<int>(WorldLocationId::RockfangFort) || state.encounterDefeated)) ||
        (state.encounterDefeated && state.encounterLife != 0))
        return invalid("载货、劳力或遭遇字段无效。");
    const OperationResult squad = validateSquad(state.squad);
    if (!squad) return invalid("小队无效：" + squad.message);
    if (!state_safety::isSafeDisplayText(state.squad.name)) return invalid("任务小队名称包含非法文本。");
    for (const Character& member : state.squad.members) {
        if (!state_safety::isSafeDisplayText(member.name)) return invalid("任务角色姓名包含非法文本。");
        for (const auto& equipment : member.equipment) {
            if (equipment &&
                (!state_safety::isSafeDisplayText(equipment->id) || !state_safety::isSafeDisplayText(equipment->name)))
                return invalid("任务角色装备包含非法文本。");
        }
    }
    if (state.backpack.items().size() > kMaximumMissionInventoryItems ||
        state.backpack.usedWeight() > state.backpack.weightLimit() ||
        state.backpack.usedSlots() > state.backpack.slotLimit())
        return invalid("任务背包超出容量。");
    std::unordered_set<std::string> ids;
    const auto validateItem = [&ids](const Item& item, const std::size_t expectedSlot) {
        if (item.id.empty() || item.name.empty() || item.weight < 0 || item.slotCount <= 0 ||
            static_cast<int>(item.quality) < static_cast<int>(ItemQuality::Crude) ||
            static_cast<int>(item.quality) > static_cast<int>(ItemQuality::Legendary) ||
            static_cast<int>(item.condition) < static_cast<int>(ItemCondition::Intact) ||
            static_cast<int>(item.condition) > static_cast<int>(ItemCondition::Scrapped) ||
            item.condition == ItemCondition::Scrapped || !state_safety::isSafeDisplayText(item.id) ||
            !state_safety::isSafeDisplayText(item.name) || !ids.insert(item.id).second)
            return false;
        if (!item.equipmentSlot) return expectedSlot == kEquipmentSlotCount;
        const int slot = static_cast<int>(*item.equipmentSlot);
        if (slot < 0 || slot >= static_cast<int>(EquipmentSlot::Count)) return false;
        return expectedSlot == kEquipmentSlotCount || slot == static_cast<int>(expectedSlot);
    };
    for (const Character& member : state.squad.members)
        for (std::size_t slot = 0U; slot < member.equipment.size(); ++slot)
            if (member.equipment[slot] && !validateItem(*member.equipment[slot], slot))
                return invalid("任务角色装备编号、枚举或文本无效。");
    for (const Item& item : state.backpack.items())
        if (!validateItem(item, kEquipmentSlotCount)) return invalid("任务背包物品编号或文本无效。");
    return valid("地图任务状态合法。");
}

ExpansionCommandResult ExpansionGame::commit(ExpansionState candidate, std::string message, bool turnAdvanced) {
    const OperationResult check = validateState(candidate);
    if (!check) return rejected("操作已取消：" + check.message);
    state_ = std::move(candidate);
    return {true, true, true, turnAdvanced, std::move(message)};
}

ExpansionCommandResult ExpansionGame::rejected(std::string message) const {
    return {true, false, false, false, std::move(message)};
}

void ExpansionGame::recordTurn(ExpansionState& candidate, int leaderFatigue, int followerFatigue) const {
    ++candidate.turn;
    for (std::size_t index = 0; index < candidate.squad.members.size(); ++index)
        candidate.squad.members[index].fatigue =
            std::clamp(candidate.squad.members[index].fatigue +
                           (index == candidate.squad.leaderIndex ? leaderFatigue : followerFatigue),
                       0, 100);
    candidate.squad.cohesion = std::clamp(candidate.squad.cohesion, 0, 100);
}

} // namespace tribe
