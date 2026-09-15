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

/// 用途：在道路图上寻找距离起点最近、且满足条件的地点。输入：起点与该地点的判定函数。
/// 输出：最近地点；没有候选时为空值。失败：无。不变量：距离相等时取编号更小的地点，保证提示稳定可复现。
template <typename Accept>
std::optional<WorldLocationId> nearestWhere(const WorldLocationId from, Accept accept) {
    std::optional<WorldLocationId> best;
    int bestSteps = -1;
    for (std::size_t index = 0U; index < kExpeditionWorldLocationCount; ++index) {
        const WorldLocationId candidate = static_cast<WorldLocationId>(index);
        if (!accept(candidate)) continue;
        const int steps = world_map::roadDistance(from, candidate);
        if (steps < 0) continue;
        if (!best || steps < bestSteps) {
            best = candidate;
            bestSteps = steps;
        }
    }
    return best;
}

/// 用途：把最短路压缩成中文方位链，例如“西→北”。输入：起点与终点。
/// 输出：方位链；同点或不可达时为空文本。失败：无。不变量：方位词只来自 world_map::direction。
std::string directionChain(const WorldLocationId from, const WorldLocationId to) {
    const std::vector<WorldLocationId> path = world_map::roadPath(from, to);
    std::string chain;
    for (std::size_t index = 1U; index < path.size(); ++index) {
        if (index > 1U) chain += "→";
        chain += world_map::direction(path[index - 1U], path[index]);
    }
    return chain;
}

/// 用途：生成“第几步、方位链、建议指令”形式的单行路线提示。输入：目标地点与当前地点。
/// 输出：可直接插入提示文本的片段。失败：无。不变量：目标与当前位置相同或不可达时返回失败说明文本，不产生越界访问。
std::string pathHint(const WorldLocationId from, const WorldLocationId to) {
    const int steps = world_map::roadDistance(from, to);
    if (steps < 0) return "（道路不通）";
    const std::size_t target = indexOf(to);
    return std::to_string(steps) + "步（" + directionChain(from, to) + "），输入 move " +
           std::to_string(target + 1U);
}

/// 用途：把资源枚举转换为与命令参数一致的中文名称。输入：资源枚举。输出：中文名；无状态修改。
/// 失败：未知枚举返回“资源”。不变量：名称与 gather 命令接受的参数保持同一套用词。
std::string_view resourceLabel(const ResourceKind resource) {
    switch (resource) {
        case ResourceKind::Food:
            return "食物";
        case ResourceKind::Wood:
            return "木材";
        case ResourceKind::Stone:
            return "石料";
        case ResourceKind::Herbs:
            return "草药";
        case ResourceKind::Hides:
            return "兽皮";
    }
    return "资源";
}

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
    // 防御与撤退在没有遭遇时也必须被“识别后拒绝”：直接返回未识别会让界面提示“无法识别该命令”，
    // 掩盖真实原因（此处没有遭遇战）。5.2 的错误地点操作测试覆盖了这一提示差异。
    if (commandId == game_command_catalog::CommandId::Defend)
        return hasArity(0U) ? defendEncounter() : rejected("用法：defend");
    if (commandId == game_command_catalog::CommandId::Retreat)
        return hasArity(0U) ? retreatEncounter() : rejected("用法：retreat");
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
    if (state_.harvestActions >= kMaximumHarvestActions) return rejected("本次任务的采集时段已用完，请前往营地或前哨结算。");
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
    if (state_.cargoWood < kOutpostWoodCost || state_.cargoStone < kOutpostStoneCost)
        return rejected("建造前哨需要现场携带木材" + std::to_string(kOutpostWoodCost) + "、石料" +
                       std::to_string(kOutpostStoneCost) + "。");
    ExpansionState candidate = state_;
    candidate.cargoWood -= kOutpostWoodCost;
    candidate.cargoStone -= kOutpostStoneCost;
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
    // 结算后地图阶段已经结束：与 move/gather/buildOutpost 保持一致地拒绝，避免结算后仍能推进任务回合。
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    if (state_.worldLocation != static_cast<int>(WorldLocationId::RockfangFort) || state_.encounterDefeated)
        return rejected("这里没有可攻击的遭遇。");
    ExpansionState candidate = state_;
    if (candidate.encounterLife == 0) candidate.encounterLife = kRockfangEncounterLife;
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
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
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
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    if (state_.encounterLife <= 0 || state_.worldLocation != static_cast<int>(WorldLocationId::RockfangFort))
        return rejected("当前没有遭遇战。");
    ExpansionState candidate = state_;
    candidate.encounterLife = 0;
    candidate.worldLocation = static_cast<int>(WorldLocationId::OldPass);
    // 撤退落点必须同时是已发现地点：否则候选状态会被 validateState 判为非法，撤退被整体取消，
    // 玩家反而被困在遭遇里。正常行军一定先经过古老山隘，这里是防御性兜底，保证撤退永远安全。
    candidate.worldDiscovered[indexOf(WorldLocationId::OldPass)] = true;
    recordTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队撤回古老山隘。", true);
}

ExpansionCommandResult ExpansionGame::useHerb() {
    // 地图阶段的统一前置条件：已结算的任务不再消耗草药，也不再推进任务回合。
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
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
    output << '\n' << routeHintText();
    return output.str();
}

std::string ExpansionGame::routeHintText() const { return missionRouteHint(state_); }

std::string missionRouteHint(const ExpansionState& state) {
    const WorldLocationId current = static_cast<WorldLocationId>(state.worldLocation);
    const std::size_t at = indexOf(current);
    const world_map::LocationProfile& profile = world_map::profile(current);
    std::ostringstream output;
    output << "地点档案：风险" << profile.risk << "级（" << profile.riskName << "）  收益：" << profile.reward
           << "\n现场风险：" << profile.hazard;
    // 任务状态提示：把“现在能做什么、做完该回哪里”写成一行，避免玩家在错误地点反复试错。
    output << "\n任务状态：";
    if (state.phase != ExpansionPhase::Exploring) {
        output << "本次任务已结算，地图不再接受移动、采集、建造或遭遇指令。";
    } else if (state.encounterLife > 0) {
        output << "岩牙巡逻拦住道路，只能 attack、defend、retreat 或 look；敌军生命" << state.encounterLife << "。";
    } else {
        output << (state.outposts[at] ? "当前地点是结算点，settle 可立即入库；"
                                     : "当前地点不是结算点，载货必须先带回营地或前哨；");
        if (state.missionKind == MissionKind::Gather)
            output << "采集时段剩余" << std::max(0, kMaximumHarvestActions - state.harvestActions) << '/'
                   << kMaximumHarvestActions << "；";
        output << "载货" << cargoTotal(state) << '/' << state.cargoCapacity;
        if (cargoTotal(state) >= state.cargoCapacity) output << "（已满，结算后才能继续装载）";
        output << "。";
    }
    // 指定资源提示：只有任务层知道“本次任务采什么”，所以本地点的资源错配必须在这里说清楚。
    if (state.phase == ExpansionPhase::Exploring && state.missionKind == MissionKind::Gather) {
        if (!world_map::supportsResource(current, state.assignedResource)) {
            const ResourceKind assigned = state.assignedResource;
            if (const auto source = nearestWhere(current, [assigned](const WorldLocationId location) {
                    return world_map::supportsResource(location, assigned);
                })) {
                const std::size_t index = indexOf(*source);
                output << "\n任务提示：本次指定资源是" << resourceLabel(assigned) << "，此地不产；最近产地 "
                       << (index + 1U) << '.' << world_map::locations()[index].name << ' '
                       << pathHint(current, *source) << "。";
            }
        } else if (state.assignedResource == ResourceKind::Food &&
                   world_map::supportsResource(current, ResourceKind::Hides)) {
            // 兽皮是食物任务的合法副产品（gather 的混采豁免），这里显式提示，避免玩家以为要另开任务。
            output << "\n任务提示：本次指定资源是食物，此地还可顺带装载兽皮（狩猎副产品不算混采）。";
        }
    }
    if (state.phase == ExpansionPhase::Exploring) {
        if (state.outposts[at]) {
            output << "\n路线提示：当前地点即为结算点，可随时 settle 入库。";
        } else if (const auto settlement = nearestWhere(
                       current, [&state](const WorldLocationId location) { return state.outposts[indexOf(location)]; })) {
            const std::size_t index = indexOf(*settlement);
            output << "\n路线提示：最近结算点 " << (index + 1U) << '.' << world_map::locations()[index].name << ' '
                   << pathHint(current, *settlement) << "；具备木材6、石料4时也可就地建造前哨。";
        }
        // 未探索提示按最短路给出，等价于一条自动规划的“补全地图”路线，替代玩家自己数格子。
        if (const auto unknown = nearestWhere(current, [&state](const WorldLocationId location) {
                return !state.worldDiscovered[indexOf(location)];
            })) {
            const std::size_t index = indexOf(*unknown);
            output << "\n探索提示：最近未探索地点 " << (index + 1U) << '.' << world_map::locations()[index].name << ' '
                   << pathHint(current, *unknown) << "。";
        } else {
            output << "\n探索提示：十六地点已全部发现。";
        }
    }
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
        state.cargoHides < 0 || state.harvestActions < 0 || state.harvestActions > kMaximumHarvestActions ||
        state.cargoCapacity <= 0 ||
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
