#include "tribe/expansion_game.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tribe {
namespace {

struct ParsedCommand {
    std::string verb;
    std::vector<std::string> args;
};

std::string asciiLower(std::string text) {
    for (char& character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 128U) character = static_cast<char>(std::tolower(byte));
    }
    return text;
}

ParsedCommand parseCommand(const std::string_view input) {
    std::istringstream stream{std::string(input)};
    ParsedCommand command;
    stream >> command.verb;
    command.verb = asciiLower(command.verb);
    std::string argument;
    while (stream >> argument) command.args.push_back(asciiLower(std::move(argument)));
    return command;
}

bool equalsAny(const std::string_view value, const std::initializer_list<std::string_view> aliases) {
    return std::find(aliases.begin(), aliases.end(), value) != aliases.end();
}

bool verbIs(const ParsedCommand& command, const std::initializer_list<std::string_view> aliases) {
    return equalsAny(command.verb, aliases);
}

bool validPhase(const ExpansionPhase phase) {
    const int value = static_cast<int>(phase);
    return value >= static_cast<int>(ExpansionPhase::CampPreparation)
        && value <= static_cast<int>(ExpansionPhase::ReturnSettlement);
}

bool validLocation(const ExpansionLocation location) {
    const int value = static_cast<int>(location);
    return value >= static_cast<int>(ExpansionLocation::Camp)
        && value <= static_cast<int>(ExpansionLocation::StrangerClearing);
}

bool validStance(const ForeignStance stance) {
    const int value = static_cast<int>(stance);
    return value >= static_cast<int>(ForeignStance::Unknown)
        && value <= static_cast<int>(ForeignStance::Defeated);
}

bool validOrder(const SquadOrder order) {
    const int value = static_cast<int>(order);
    return value >= static_cast<int>(SquadOrder::Follow)
        && value <= static_cast<int>(SquadOrder::Withdraw);
}

OperationResult accepted(std::string message) {
    return {true, std::move(message)};
}

OperationResult rejectedOperation(std::string message) {
    return {false, std::move(message)};
}

Character namedCharacter(const std::string& name, const Occupation occupation) {
    Character character{name, occupation};
    character.attributes = Attributes{5};
    character.loyalty = 70;
    switch (occupation) {
    case Occupation::Hunter:
        character.attributes[Attribute::Perception] = 8;
        character.attributes[Attribute::Survival] = 8;
        character.attributes[Attribute::Agility] = 7;
        break;
    case Occupation::Warrior:
        character.attributes[Attribute::Strength] = 9;
        character.attributes[Attribute::Endurance] = 8;
        character.attributes[Attribute::Willpower] = 7;
        break;
    case Occupation::Scout:
        character.attributes[Attribute::Agility] = 9;
        character.attributes[Attribute::Perception] = 8;
        character.attributes[Attribute::Survival] = 7;
        break;
    case Occupation::Healer:
        character.attributes[Attribute::Survival] = 8;
        character.attributes[Attribute::Perception] = 7;
        character.attributes[Attribute::Willpower] = 7;
        break;
    case Occupation::Crafter:
        character.attributes[Attribute::Survival] = 7;
        character.attributes[Attribute::Perception] = 7;
        character.attributes[Attribute::Endurance] = 7;
        break;
    case Occupation::Envoy:
        character.attributes[Attribute::Diplomacy] = 9;
        character.attributes[Attribute::Leadership] = 8;
        character.attributes[Attribute::Agility] = 7;
        character.attributes[Attribute::Perception] = 6;
        character.attributes[Attribute::Endurance] = 6;
        break;
    }
    character.life = maximumLife(character);
    return character;
}

std::optional<EquipmentSlot> parseSlot(const std::string_view value) {
    if (equalsAny(value, {"mainhand", "main", "主手"})) return EquipmentSlot::MainHand;
    if (equalsAny(value, {"offhand", "off", "副手"})) return EquipmentSlot::OffHand;
    if (equalsAny(value, {"head", "头部"})) return EquipmentSlot::Head;
    if (equalsAny(value, {"body", "身体"})) return EquipmentSlot::Body;
    if (equalsAny(value, {"hands", "手部"})) return EquipmentSlot::Hands;
    if (equalsAny(value, {"legsfeet", "legs", "腿脚"})) return EquipmentSlot::LegsFeet;
    if (equalsAny(value, {"tool", "工具"})) return EquipmentSlot::Tool;
    if (equalsAny(value, {"accessory", "饰品"})) return EquipmentSlot::Accessory;
    return std::nullopt;
}

const std::array<std::string_view, kExpeditionWorldLocationCount> kWorldNames{{
    "燧火营地", "苍林", "红土原", "芦苇沼泽", "河鹿渡口", "白羽营地", "燧石矿场", "古老山隘",
    "岩牙要塞", "盐风海岸", "潮盐港", "贝壳滩", "玄石谷", "玄石工坊", "山前集市", "断崖商道"}};

const std::array<std::vector<int>, kExpeditionWorldLocationCount> kWorldRoads{{
    {1, 2}, {0, 3}, {0, 4, 6}, {1, 5, 9}, {2, 14}, {3}, {2, 12}, {15, 8},
    {7}, {3, 11}, {11, 14}, {9, 10}, {6, 13}, {12, 15}, {4, 10, 15}, {13, 14, 7}}};

std::optional<int> parseWorldLocation(const std::string_view value) {
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
        && number >= 1 && number <= static_cast<int>(kExpeditionWorldLocationCount)) return number - 1;
    static const std::array<std::vector<std::string_view>, kExpeditionWorldLocationCount> aliases{{
        {"camp", "营地", "燧火营地"}, {"forest", "苍林"}, {"plain", "redplain", "红土原"},
        {"marsh", "沼泽", "芦苇沼泽"}, {"ford", "riverford", "河鹿渡口"}, {"whitecamp", "白羽营地"},
        {"quarry", "矿场", "燧石矿场"}, {"pass", "oldpass", "古老山隘"}, {"fort", "rockfort", "岩牙要塞"},
        {"coast", "saltwind", "盐风海岸"}, {"harbor", "tidesaltharbor", "潮盐港"},
        {"beach", "shellbeach", "贝壳滩"}, {"valley", "blackstonevalley", "玄石谷"},
        {"workshop", "blackstoneworkshop", "玄石工坊"}, {"market", "mountainmarket", "山前集市"},
        {"road", "cliffroad", "断崖商道"}}};
    for (std::size_t index = 0; index < aliases.size(); ++index) {
        if (std::find(aliases[index].begin(), aliases[index].end(), value) != aliases[index].end()) {
            return static_cast<int>(index);
        }
    }
    return std::nullopt;
}

int cargoTotal(const ExpansionState& state) {
    return state.cargoFood + state.cargoWood + state.cargoStone + state.cargoHerbs;
}

bool worldRoadExists(const int from, const int to) {
    if (from < 0 || from >= static_cast<int>(kWorldRoads.size())) return false;
    const auto& roads = kWorldRoads[static_cast<std::size_t>(from)];
    return std::find(roads.begin(), roads.end(), to) != roads.end();
}

} // namespace

ExpansionGame::ExpansionGame(const std::uint32_t seed, const std::size_t squadSize) {
    if (squadSize < kMinimumSquadSize || squadSize > kMaximumSquadSize) {
        throw std::invalid_argument("苍林狩猎小队人数必须为2至8人。");
    }

    state_.seed = seed;
    state_.squad.name = "晨火狩猎队";
    state_.squad.leaderIndex = 0;
    state_.squad.cohesion = 72;
    static const std::array<std::pair<const char*, Occupation>, kMaximumSquadSize> roster{{
        {"青枝", Occupation::Envoy},
        {"石刃", Occupation::Warrior},
        {"苍眼", Occupation::Scout},
        {"白榆", Occupation::Healer},
        {"逐鹿", Occupation::Hunter},
        {"岩槌", Occupation::Crafter},
        {"芦风", Occupation::Hunter},
        {"河矛", Occupation::Warrior},
    }};
    for (std::size_t index = 0; index < squadSize; ++index) {
        state_.squad.members.push_back(namedCharacter(roster[index].first, roster[index].second));
    }

    Item leaderBow;
    leaderBow.id = "leader_bow";
    leaderBow.name = "苍林短弓";
    leaderBow.weight = 3;
    leaderBow.equipmentSlot = EquipmentSlot::MainHand;
    leaderBow.bonuses[Attribute::Perception] = 2;
    const OperationResult equipped = equipItem(state_.squad.members.front(), EquipmentSlot::MainHand, leaderBow);
    if (!equipped) throw std::logic_error(equipped.message);

    Item spareKnife;
    spareKnife.id = "spare_knife";
    spareKnife.name = "备用石刀";
    spareKnife.weight = 2;
    spareKnife.equipmentSlot = EquipmentSlot::MainHand;
    spareKnife.bonuses[Attribute::Strength] = 1;
    const OperationResult stored = state_.inventory.pickupFree(std::move(spareKnife));
    if (!stored) throw std::logic_error(stored.message);

    const OperationResult valid = validateState(state_);
    if (!valid) throw std::logic_error("苍林狩猎初始状态无效：" + valid.message);
}

ExpansionGame::ExpansionGame(ExpansionState state) : state_(std::move(state)) {
    const OperationResult valid = validateState(state_);
    if (!valid) throw std::invalid_argument("苍林狩猎状态无效：" + valid.message);
}

ExpansionCommandResult ExpansionGame::execute(const std::string_view input) {
    const ParsedCommand command = parseCommand(input);
    if (command.verb.empty()) return {};
    if (state_.worldMode) return executeWorld(command.verb, command.args);

    if (verbIs(command, {"look", "查看"})) {
        return command.args.empty()
            ? ExpansionCommandResult{true, true, false, false, lookText()}
            : rejected("用法：look / 查看");
    }
    if (verbIs(command, {"move", "移动"})) {
        return command.args.size() == 1U ? move(command.args.front()) : rejected("用法：move <地点> / 移动 <地点>");
    }
    if (verbIs(command, {"gather", "采集"})) {
        return command.args.size() == 1U ? gather(command.args.front()) : rejected("用法：gather <资源> / 采集 <资源>");
    }
    if (verbIs(command, {"talk", "交谈"})) {
        return command.args.empty() ? talk() : rejected("用法：talk / 交谈");
    }
    if (verbIs(command, {"trade", "贸易"})) {
        return command.args.empty() ? trade() : rejected("用法：trade / 贸易");
    }
    if (verbIs(command, {"raid", "劫掠"})) {
        return command.args.empty() ? raid() : rejected("用法：raid / 劫掠");
    }
    if (verbIs(command, {"attack", "攻击"})) {
        return command.args.empty() ? attack() : rejected("用法：attack / 攻击");
    }
    if (verbIs(command, {"defend", "防御"})) {
        return command.args.empty() ? defend() : rejected("用法：defend / 防御");
    }
    if (verbIs(command, {"order", "下令"})) {
        return command.args.size() == 1U ? order(command.args.front()) : rejected("用法：order <军令> / 下令 <军令>");
    }
    if (verbIs(command, {"use", "使用"})) {
        return command.args.size() == 1U ? use(command.args.front()) : rejected("用法：use <物品> / 使用 <物品>");
    }
    if (verbIs(command, {"loot", "搜取"})) {
        return command.args.empty() ? loot() : rejected("用法：loot / 搜取");
    }
    if (verbIs(command, {"retreat", "撤退"})) {
        return command.args.empty() ? retreat() : rejected("用法：retreat / 撤退");
    }
    if (verbIs(command, {"return", "回营"})) {
        return command.args.empty() ? returnToCamp() : rejected("用法：return / 回营");
    }
    if (verbIs(command, {"equip", "装备"})) {
        return command.args.size() == 2U ? equip(command.args[0], command.args[1])
                                         : rejected("用法：equip <装备栏> <物品编号> / 装备 <装备栏> <物品编号>");
    }
    return {};
}

ExpansionCommandResult ExpansionGame::executeWorld(const std::string& verb,
    const std::vector<std::string>& args) {
    if (equalsAny(verb, {"look", "查看", "map", "地图"})) {
        return args.empty() ? ExpansionCommandResult{true, true, false, false, worldLookText()}
                            : rejected("用法：look / 查看");
    }
    if (state_.enemyLife > 0) {
        if (equalsAny(verb, {"attack", "攻击"})) {
            return args.empty() ? attackWorldEncounter() : rejected("用法：attack / 攻击");
        }
        if (equalsAny(verb, {"defend", "防御"})) {
            return args.empty() ? defendWorldEncounter() : rejected("用法：defend / 防御");
        }
        if (equalsAny(verb, {"retreat", "撤退"})) {
            return args.empty() ? retreatWorldEncounter() : rejected("用法：retreat / 撤退");
        }
        if (equalsAny(verb, {"equip", "装备"})) {
            return args.size() == 2U ? equip(args[0], args[1])
                                     : rejected("用法：equip <装备栏> <物品编号> / 装备 <装备栏> <物品编号>");
        }
        return rejected("岩牙巡逻正在逼近；可用：攻击、防御、撤退、装备、查看。");
    }
    if (equalsAny(verb, {"attack", "攻击"})) {
        return args.empty() ? attackWorldEncounter() : rejected("用法：attack / 攻击");
    }
    if (equalsAny(verb, {"move", "移动"})) {
        return args.size() == 1U ? moveWorld(args.front()) : rejected("用法：move <相邻地点> / 移动 <相邻地点>");
    }
    if (equalsAny(verb, {"gather", "采集"})) {
        return args.size() == 1U ? gatherWorld(args.front()) : rejected("用法：gather <资源> / 采集 <资源>");
    }
    if (equalsAny(verb, {"buildoutpost", "outpost", "建造前哨"})
        || (equalsAny(verb, {"build", "建造"}) && args.size() == 1U
            && equalsAny(args.front(), {"outpost", "前哨"}))) {
        return (args.empty() || (args.size() == 1U && equalsAny(args.front(), {"outpost", "前哨"})))
            ? buildOutpost() : rejected("用法：build outpost / 建造 前哨");
    }
    if (equalsAny(verb, {"settle", "结算", "return", "回营"})) {
        return args.empty() ? settleWorld() : rejected("用法：settle / 结算");
    }
    if (equalsAny(verb, {"equip", "装备"})) {
        return args.size() == 2U ? equip(args[0], args[1])
                                 : rejected("用法：equip <装备栏> <物品编号> / 装备 <装备栏> <物品编号>");
    }
    return {};
}

ExpansionCommandResult ExpansionGame::moveWorld(const std::string_view target) {
    if (state_.phase != ExpansionPhase::ForestExploration) return rejected("本次地图任务已经结算。");
    if (state_.enemyLife > 0) return rejected("遭遇战尚未结束，不能离开；请攻击、防御或撤退。");
    const auto destination = parseWorldLocation(target);
    if (!destination) return rejected("未知地点，可输入地图查看1至16号地点。");
    if (*destination == state_.worldLocation) return rejected("小队已经在该地点。");
    if (!worldRoadExists(state_.worldLocation, *destination)) return rejected("两地不相邻，不能跨越道路移动。");
    ExpansionState candidate = state_;
    candidate.worldLocation = *destination;
    const bool discoveredNow = !candidate.worldDiscovered[static_cast<std::size_t>(*destination)];
    candidate.worldDiscovered[static_cast<std::size_t>(*destination)] = true;
    recordSquadTurn(candidate, 2, 1);
    std::string message = "小队沿道路抵达" + std::string(kWorldNames[static_cast<std::size_t>(*destination)]) + "。";
    if (discoveredNow) message += " 新地点已记录到部落地图。";
    if (*destination == 8 && !candidate.rockfangFortCleared) {
        message += " 岩牙巡逻正在要塞外戒备；输入攻击可发起遭遇战，装备会影响战斗。";
    }
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::gatherWorld(const std::string_view resource) {
    if (state_.phase != ExpansionPhase::ForestExploration) return rejected("本次地图任务已经结算。");
    if (state_.harvestActions >= 4) return rejected("本次任务的采集时段已经结束，请前往营地或前哨结算。");
    const int location = state_.worldLocation;
    enum class CargoKind { Food, Wood, Stone, Herbs };
    std::optional<CargoKind> kind;
    int baseGain = 0;
    if (equalsAny(resource, {"food", "食物", "粮食"})
        && (location == 1 || location == 2 || location == 4 || location == 9)) {
        kind = CargoKind::Food; baseGain = 6 + state_.foodGatherBonus;
    } else if (equalsAny(resource, {"wood", "木材"}) && (location == 1 || location == 3)) {
        kind = CargoKind::Wood; baseGain = 6;
    } else if (equalsAny(resource, {"stone", "石料", "石头"})
        && (location == 6 || location == 7 || location == 12)) {
        kind = CargoKind::Stone; baseGain = 5;
    } else if (equalsAny(resource, {"herbs", "herb", "草药"})
        && (location == 1 || location == 3 || location == 5)) {
        kind = CargoKind::Herbs; baseGain = 4 + state_.herbGatherBonus;
    } else {
        return rejected("当前地点没有这种资源，或该资源不能由小队直接采集。");
    }
    const int room = state_.cargoCapacity - cargoTotal(state_);
    if (room <= 0) return rejected("小队载货已经达到上限，请先到营地或前哨结算。");
    ExpansionState candidate = state_;
    const int gain = std::min(baseGain, room);
    switch (*kind) {
    case CargoKind::Food: candidate.cargoFood += gain; break;
    case CargoKind::Wood: candidate.cargoWood += gain; break;
    case CargoKind::Stone: candidate.cargoStone += gain; break;
    case CargoKind::Herbs: candidate.cargoHerbs += gain; break;
    }
    ++candidate.harvestActions;
    recordSquadTurn(candidate, 4, 2);
    return commit(std::move(candidate), "小队采集" + std::string(resource) + "，装载" + std::to_string(gain) + "单位。", true);
}

ExpansionCommandResult ExpansionGame::buildOutpost() {
    if (state_.phase != ExpansionPhase::ForestExploration) return rejected("本次地图任务已经结算。");
    if (state_.worldLocation == 0) return rejected("燧火营地无需重复建造前哨。");
    const std::size_t location = static_cast<std::size_t>(state_.worldLocation);
    if (state_.outposts[location]) return rejected("该地点已经建有前哨。");
    if (state_.worldLocation == 8 && !state_.rockfangFortCleared) return rejected("岩牙要塞仍由敌军控制，不能建造前哨。");
    if (state_.cargoWood < 6 || state_.cargoStone < 4) return rejected("现场建造前哨需要携带木材6、石料4。");
    ExpansionState candidate = state_;
    candidate.cargoWood -= 6;
    candidate.cargoStone -= 4;
    candidate.outposts[location] = true;
    recordSquadTurn(candidate, 5, 3);
    return commit(std::move(candidate), "小队现场建成前哨，现在可以在此结算并驻留。", true);
}

ExpansionCommandResult ExpansionGame::settleWorld() {
    if (state_.phase != ExpansionPhase::ForestExploration) return rejected("本次地图任务已经结算。");
    const std::size_t location = static_cast<std::size_t>(state_.worldLocation);
    if (location != 0U && !state_.outposts[location]) return rejected("这里只能临时停留；必须返回营地或已建前哨才能结算。");
    ExpansionState candidate = state_;
    candidate.phase = ExpansionPhase::ReturnSettlement;
    candidate.settled = true;
    recordSquadTurn(candidate, 1, 1);
    const int experience = 15 + candidate.harvestActions * 10;
    for (Character& member : candidate.squad.members) {
        const OperationResult gained = gainExperience(member, experience);
        if (!gained) return rejected("地图任务经验结算失败，状态未改变：" + gained.message);
    }
    candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 2);
    return commit(std::move(candidate), location == 0U ? "小队回到燧火营地并完成资源结算。"
                                                       : "小队在前哨卸下资源并驻留。", true);
}

ExpansionCommandResult ExpansionGame::attackWorldEncounter() {
    if (state_.worldLocation != 8 || state_.rockfangFortCleared || state_.battleWon) {
        return rejected("当前没有可攻击的地图遭遇。 ");
    }
    ExpansionState candidate = state_;
    if (candidate.enemyLife <= 0) candidate.enemyLife = 14;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int damage = std::max(2, attributes[Attribute::Strength] / 2 + attributes[Attribute::Agility] / 4);
    candidate.enemyLife = std::max(0, candidate.enemyLife - damage);
    recordSquadTurn(candidate, 4, 2);
    if (candidate.enemyLife == 0) {
        candidate.battleWon = true;
        Item trophy;
        trophy.id = "rockfang_badge";
        trophy.name = "岩牙巡逻徽记";
        trophy.weight = 1;
        trophy.equipmentSlot = EquipmentSlot::Accessory;
        trophy.bonuses[Attribute::Willpower] = 1;
        const OperationResult stored = candidate.inventory.pickupFree(std::move(trophy));
        std::string message = "小队击退岩牙巡逻，尚未占领要塞。";
        if (stored) message += " 获得可装备的岩牙巡逻徽记。";
        else message += " 背包已满，战利品未能带走。";
        return commit(std::move(candidate), std::move(message), true);
    }
    const int retaliation = std::max(1, 5 - attributes[Attribute::Endurance] / 4);
    leader.life = std::max(1, leader.life - retaliation);
    return commit(std::move(candidate), "小队攻击岩牙巡逻，敌军生命-" + std::to_string(damage)
        + "；反击使队长生命-" + std::to_string(retaliation) + "。", true);
}

ExpansionCommandResult ExpansionGame::defendWorldEncounter() {
    if (state_.enemyLife <= 0 || state_.worldLocation != 8) return rejected("当前没有可防御的地图遭遇。 ");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int retaliation = std::max(1, 3 - attributes[Attribute::Endurance] / 5);
    leader.life = std::max(1, leader.life - retaliation);
    recordSquadTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队结阵防御，岩牙巡逻的攻势被化解；队长生命-"
        + std::to_string(retaliation) + "。", true);
}

ExpansionCommandResult ExpansionGame::retreatWorldEncounter() {
    if (state_.enemyLife <= 0 || state_.worldLocation != 8) return rejected("当前没有可撤离的地图遭遇。 ");
    ExpansionState candidate = state_;
    candidate.enemyLife = 0;
    candidate.battleWon = false;
    candidate.retreated = true;
    candidate.worldLocation = 7;
    recordSquadTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队撤回古老山隘；岩牙要塞仍未被占领。", true);
}

std::string ExpansionGame::worldLookText() const {
    std::ostringstream output;
    const std::size_t location = static_cast<std::size_t>(state_.worldLocation);
    output << "地点：" << kWorldNames[location] << "  相邻：";
    for (const int neighbor : kWorldRoads[static_cast<std::size_t>(state_.worldLocation)]) {
        output << (neighbor + 1) << '.' << kWorldNames[static_cast<std::size_t>(neighbor)] << ' ';
    }
    output << "\n载货 " << cargoTotal(state_) << '/' << state_.cargoCapacity << "：食物" << state_.cargoFood
           << " 木材" << state_.cargoWood << " 石料" << state_.cargoStone << " 草药" << state_.cargoHerbs
           << "  采集次数" << state_.harvestActions << "/4";
    output << "\n可采资源：";
    bool hasResource = false;
    const auto addResource = [&](const std::string_view name) {
        if (hasResource) output << "、";
        output << name;
        hasResource = true;
    };
    if (location == 1U || location == 2U || location == 4U || location == 9U) addResource("食物");
    if (location == 1U || location == 3U) addResource("木材");
    if (location == 6U || location == 7U || location == 12U) addResource("石料");
    if (location == 1U || location == 3U || location == 5U) addResource("草药");
    if (!hasResource) output << "无";

    std::array<int, kExpeditionWorldLocationCount> distance{};
    distance.fill(-1);
    std::vector<int> frontier{state_.worldLocation};
    distance[location] = 0;
    int nearest = -1;
    for (std::size_t cursor = 0; cursor < frontier.size() && nearest < 0; ++cursor) {
        const int current = frontier[cursor];
        if (state_.outposts[static_cast<std::size_t>(current)]) {
            nearest = current;
            break;
        }
        for (const int neighbor : kWorldRoads[static_cast<std::size_t>(current)]) {
            if (distance[static_cast<std::size_t>(neighbor)] >= 0) continue;
            distance[static_cast<std::size_t>(neighbor)] = distance[static_cast<std::size_t>(current)] + 1;
            frontier.push_back(neighbor);
        }
    }
    if (nearest >= 0) {
        output << "\n最近结算点：" << kWorldNames[static_cast<std::size_t>(nearest)]
               << "（" << distance[static_cast<std::size_t>(nearest)] << "段道路）";
    }
    if (state_.outposts[location]) output << "  [当前可结算]";
    const Character& leader = state_.squad.members[state_.squad.leaderIndex];
    output << "\n队长装备：";
    bool hasEquipment = false;
    for (const auto& item : leader.equipment) {
        if (!item) continue;
        if (hasEquipment) output << "、";
        output << item->name;
        hasEquipment = true;
    }
    if (!hasEquipment) output << "无";
    if (state_.enemyLife > 0) {
        output << "\n遭遇：岩牙巡逻，敌军生命" << state_.enemyLife
               << "。可用 攻击、防御、撤退、装备。";
    } else if (state_.battleWon && location == 8U) {
        output << "\n遭遇：岩牙巡逻已被击退；要塞仍需由部落军队占领。";
    }
    return output.str();
}

ExpansionCommandResult ExpansionGame::move(const std::string_view target) {
    ExpansionState candidate = state_;
    if (candidate.phase == ExpansionPhase::CampPreparation
        && equalsAny(target, {"forest", "forestedge", "edge", "苍林", "林缘"})) {
        candidate.phase = ExpansionPhase::ForestExploration;
        candidate.location = ExpansionLocation::ForestEdge;
        recordSquadTurn(candidate, 2, 1);
        return commit(std::move(candidate), "小队离开营地，抵达苍林边缘。", true);
    }
    if (candidate.phase != ExpansionPhase::ForestExploration) {
        return rejected("当前阶段不能移动；战斗中请先撤退，遭遇后可交谈、贸易、劫掠或回营。");
    }

    if (equalsAny(target, {"deep", "deepforest", "深林", "密林"})
        && (candidate.location == ExpansionLocation::ForestEdge
            || candidate.location == ExpansionLocation::HuntingGround)) {
        candidate.location = ExpansionLocation::DeepForest;
    } else if (equalsAny(target, {"hunt", "hunting", "huntingground", "猎场", "狩猎地"})
        && candidate.location == ExpansionLocation::DeepForest) {
        candidate.location = ExpansionLocation::HuntingGround;
    } else if (equalsAny(target, {"clearing", "meeting", "空地", "林间空地", "外族"})
        && (candidate.location == ExpansionLocation::DeepForest
            || candidate.location == ExpansionLocation::HuntingGround)) {
        candidate.location = ExpansionLocation::StrangerClearing;
        candidate.phase = ExpansionPhase::ForeignEncounter;
        candidate.foreignStance = ForeignStance::Neutral;
    } else {
        return rejected("没有可用道路，状态未改变。林缘→深林→猎场或外族空地。");
    }
    recordSquadTurn(candidate, 2, 1);
    const std::string message = "队长带路，全队按军令移动到" + locationName(candidate.location) + "。";
    return commit(std::move(candidate), message, true);
}

ExpansionCommandResult ExpansionGame::gather(const std::string_view resource) {
    if (state_.phase != ExpansionPhase::ForestExploration) return rejected("只有探索苍林时可以采集。");
    if (state_.turn >= 6) {
        return rejected("本次苍林作业的采集时段已经结束，请与外族接触或回营结算。");
    }
    ExpansionState candidate = state_;
    const Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int seedBonus = static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.turn)) % 3U);
    std::string message;

    if (equalsAny(resource, {"food", "supplies", "食物", "口粮"})) {
        if (candidate.location != ExpansionLocation::HuntingGround) return rejected("需要先到狩猎地采集食物。");
        const int gain = 4 + attributes[Attribute::Survival] / 4 + seedBonus;
        candidate.supplies += gain;
        message = "猎队获得" + std::to_string(gain) + "份口粮。";
    } else if (equalsAny(resource, {"hides", "hide", "兽皮"})) {
        if (candidate.location != ExpansionLocation::HuntingGround) return rejected("需要先到狩猎地搜集兽皮。");
        const int gain = 1 + attributes[Attribute::Perception] / 5;
        candidate.hides += gain;
        message = "猎队获得" + std::to_string(gain) + "张兽皮。";
    } else if (equalsAny(resource, {"herbs", "herb", "草药"})) {
        if (candidate.location != ExpansionLocation::DeepForest
            && candidate.location != ExpansionLocation::HuntingGround) {
            return rejected("深林或狩猎地才有可辨认的草药。");
        }
        const int gain = 2 + attributes[Attribute::Perception] / 6;
        candidate.herbs += gain;
        message = "小队采得" + std::to_string(gain) + "束草药。";
    } else {
        return rejected("可采集：food/食物、hides/兽皮、herbs/草药。");
    }
    recordSquadTurn(candidate, 4, 2);
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::talk() {
    if (state_.phase != ExpansionPhase::ForeignEncounter || state_.foreignStance != ForeignStance::Neutral) {
        return rejected("只有首次遇见外族且双方中立时可以交谈。");
    }
    const Character& leader = state_.squad.members[state_.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int diplomacy = attributes[Attribute::Diplomacy] + attributes[Attribute::Leadership]
        + state_.squad.cohesion / 10;
    if (diplomacy < 14) return rejected("队长的外交与领导能力不足，交谈没有开始，状态未改变。");

    ExpansionState candidate = state_;
    candidate.foreignStance = ForeignStance::Peaceful;
    candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 3);
    recordSquadTurn(candidate, 1, 1);
    return commit(std::move(candidate), "队长青枝表明来意，外族放下武器并允许和平离开。", true);
}

ExpansionCommandResult ExpansionGame::trade() {
    if (state_.phase != ExpansionPhase::ForeignEncounter
        || (state_.foreignStance != ForeignStance::Peaceful
            && state_.foreignStance != ForeignStance::Trading)) {
        return rejected("必须先与外族和平交谈才能贸易。");
    }
    if (state_.supplies < 2) return rejected("贸易需要2份口粮，资源不足，状态未改变。");

    ExpansionState candidate = state_;
    candidate.supplies -= 2;
    ++candidate.medicine;
    ++candidate.tradeGoods;
    candidate.traded = true;
    candidate.foreignStance = ForeignStance::Trading;
    recordSquadTurn(candidate, 1, 1);
    return commit(std::move(candidate), "用2份口粮换得1份药包和1件贸易货物。", true);
}

ExpansionCommandResult ExpansionGame::raid() {
    if (state_.phase != ExpansionPhase::ForeignEncounter
        || state_.foreignStance == ForeignStance::Hostile
        || state_.foreignStance == ForeignStance::Defeated) {
        return rejected("当前没有可以发动劫掠的外族队伍。");
    }

    ExpansionState candidate = state_;
    candidate.phase = ExpansionPhase::FrontlineCombat;
    candidate.foreignStance = ForeignStance::Hostile;
    candidate.frontline = 1;
    candidate.enemyLife = frontlineLife(1);
    candidate.enemySpeed = 7 + static_cast<int>(candidate.seed % 3U) + candidate.frontline;
    candidate.battleWon = false;
    candidate.lootAvailable = false;
    recordSquadTurn(candidate, 3, 2);
    return commit(std::move(candidate), "劫掠命令打破对峙，敌方展开三段战线。", true);
}

ExpansionCommandResult ExpansionGame::attack() {
    if (state_.phase != ExpansionPhase::FrontlineCombat) return rejected("当前不在战斗中，不能攻击。");
    if (state_.squad.members[state_.squad.leaderIndex].life <= 0) {
        return rejected("队长已经阵亡，不能继续攻击。");
    }
    ExpansionState candidate = state_;
    recordSquadTurn(candidate, 4, 3);
    candidate.lastPlayerInitiative = squadSpeed(candidate)
        + static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.frontline * 7)) % 5U);
    candidate.lastEnemyInitiative = candidate.enemySpeed
        + static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.frontline * 5)) % 5U);
    const bool playerFirst = candidate.lastPlayerInitiative >= candidate.lastEnemyInitiative;
    std::string message;
    if (!playerFirst) {
        enemyResponse(candidate, false);
        if (finishIfLeaderFallen(candidate, message)) {
            return commit(std::move(candidate), std::move(message), true);
        }
    }

    const int damage = attackPower(candidate);
    candidate.enemyLife = std::max(0, candidate.enemyLife - damage);
    message = "队长攻击，队员按“" + orderName(candidate.order) + "”自动跟随，造成"
        + std::to_string(damage) + "点伤害。";
    if (candidate.enemyLife == 0) {
        advanceFrontline(candidate, message);
    } else if (playerFirst) {
        enemyResponse(candidate, false);
        if (finishIfLeaderFallen(candidate, message)) {
            return commit(std::move(candidate), std::move(message), true);
        }
    }
    message += " 先手" + std::to_string(candidate.lastPlayerInitiative) + ":"
        + std::to_string(candidate.lastEnemyInitiative) + "。";
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::defend() {
    if (state_.phase != ExpansionPhase::FrontlineCombat) return rejected("当前不在战斗中，不能防御。");
    if (state_.squad.members[state_.squad.leaderIndex].life <= 0) {
        return rejected("队长已经阵亡，不能继续防御。");
    }
    ExpansionState candidate = state_;
    recordSquadTurn(candidate, 2, 2);
    candidate.lastPlayerInitiative = squadSpeed(candidate)
        + static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.frontline * 7)) % 5U);
    candidate.lastEnemyInitiative = candidate.enemySpeed
        + static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.frontline * 5)) % 5U);
    enemyResponse(candidate, true);
    std::string message;
    if (finishIfLeaderFallen(candidate, message)) {
        return commit(std::move(candidate), std::move(message), true);
    }

    int endurance = 0;
    for (const Character& member : candidate.squad.members) {
        if (member.life > 0) endurance += effectiveAttributes(member)[Attribute::Endurance];
    }
    const int counterDamage = std::max(1, endurance / static_cast<int>(candidate.squad.members.size() * 2U));
    candidate.enemyLife = std::max(0, candidate.enemyLife - counterDamage);
    message = "全队结阵防御，并反击造成" + std::to_string(counterDamage) + "点伤害。";
    if (candidate.enemyLife == 0) advanceFrontline(candidate, message);
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::order(const std::string_view value) {
    if (state_.phase == ExpansionPhase::ReturnSettlement) return rejected("本次行动已经结算，不能再下达军令。");
    SquadOrder selected = SquadOrder::Follow;
    if (equalsAny(value, {"follow", "跟随"})) selected = SquadOrder::Follow;
    else if (equalsAny(value, {"advance", "突进"})) selected = SquadOrder::Advance;
    else if (equalsAny(value, {"hold", "坚守"})) selected = SquadOrder::Hold;
    else if (equalsAny(value, {"focus", "集火"})) selected = SquadOrder::Focus;
    else if (equalsAny(value, {"withdraw", "后撤"})) selected = SquadOrder::Withdraw;
    else return rejected("未知军令。可选：跟随、突进、坚守、集火、后撤。");
    if (selected == state_.order) return rejected("小队已经执行该军令，状态未改变。");

    ExpansionState candidate = state_;
    candidate.order = selected;
    recordSquadTurn(candidate, 1, 1);
    std::string message = "队长下令“" + orderName(selected) + "”，"
        + std::to_string(candidate.squad.members.size() - 1U) + "名队员自动跟随。";
    if (candidate.phase == ExpansionPhase::FrontlineCombat) {
        enemyResponse(candidate, selected == SquadOrder::Hold);
        finishIfLeaderFallen(candidate, message);
    }
    return commit(std::move(candidate), message, true);
}

ExpansionCommandResult ExpansionGame::use(const std::string_view item) {
    if (state_.phase == ExpansionPhase::ReturnSettlement) return rejected("本次行动已经结算，无需使用物品。");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    std::string message;

    if (equalsAny(item, {"medicine", "药包", "药"})) {
        if (candidate.medicine <= 0) return rejected("没有可用药包，状态未改变。");
        if (leader.life >= maximumLife(leader) && leader.fatigue == 0) return rejected("队长状态良好，无需使用药包。");
        --candidate.medicine;
        leader.life = std::min(maximumLife(leader), leader.life + 20);
        leader.fatigue = std::max(0, leader.fatigue - 15);
        message = "队长使用药包，恢复生命并减轻疲劳。";
    } else if (equalsAny(item, {"herb", "herbs", "草药"})) {
        if (candidate.herbs <= 0) return rejected("没有采集到草药，状态未改变。");
        if (leader.life >= maximumLife(leader)) return rejected("队长生命已满，无需使用草药。");
        --candidate.herbs;
        leader.life = std::min(maximumLife(leader), leader.life + 10);
        message = "队长使用草药恢复10点生命。";
    } else if (equalsAny(item, {"ration", "口粮"})) {
        if (candidate.supplies <= 0) return rejected("没有口粮，状态未改变。");
        bool tired = false;
        for (const Character& member : candidate.squad.members) tired = tired || member.fatigue > 0;
        if (!tired) return rejected("全队尚未疲劳，无需消耗口粮。");
        --candidate.supplies;
        for (Character& member : candidate.squad.members) member.fatigue = std::max(0, member.fatigue - 8);
        message = "全队分食口粮，疲劳下降。";
    } else {
        return rejected("可使用：medicine/药包、herbs/草药、ration/口粮。");
    }

    recordSquadTurn(candidate, 1, 1);
    if (candidate.phase == ExpansionPhase::FrontlineCombat) {
        enemyResponse(candidate, false);
        finishIfLeaderFallen(candidate, message);
    }
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::loot() {
    if (!state_.battleWon || !state_.lootAvailable
        || state_.phase != ExpansionPhase::ForeignEncounter) {
        return rejected("当前没有可搜取的战利品。");
    }
    ExpansionState candidate = state_;
    const OperationResult picked = candidate.inventory.pickupFree(victoryLoot());
    if (!picked) return rejected("搜取失败，状态未改变：" + picked.message);
    candidate.lootAvailable = false;
    return commit(std::move(candidate), "免费搜取战利品成功；不消耗回合，也不增加疲劳。", false);
}

ExpansionCommandResult ExpansionGame::retreat() {
    if (state_.phase != ExpansionPhase::FrontlineCombat) return rejected("只有战斗中可以撤退。");
    ExpansionState candidate = state_;
    recordSquadTurn(candidate, 5, 3);
    candidate.lastPlayerInitiative = squadSpeed(candidate)
        + static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.turn)) % 5U);
    candidate.lastEnemyInitiative = candidate.enemySpeed
        + static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.turn)) % 5U);
    enemyResponse(candidate, candidate.order == SquadOrder::Withdraw);
    std::string message;
    if (finishIfLeaderFallen(candidate, message)) {
        return commit(std::move(candidate), std::move(message), true);
    }
    candidate.phase = ExpansionPhase::ForestExploration;
    candidate.location = ExpansionLocation::ForestEdge;
    candidate.frontline = 0;
    candidate.enemyLife = 0;
    candidate.enemySpeed = 0;
    candidate.retreated = true;
    candidate.lootAvailable = false;
    return commit(std::move(candidate), "小队按速度次序脱离战线，退回苍林边缘。", true);
}

ExpansionCommandResult ExpansionGame::returnToCamp() {
    if (state_.phase == ExpansionPhase::CampPreparation) return rejected("小队尚未离开营地。");
    if (state_.phase == ExpansionPhase::FrontlineCombat) return rejected("战斗中不能直接回营，请先撤退或击穿三段战线。");
    if (state_.phase == ExpansionPhase::ReturnSettlement) return rejected("本次回营已经完成结算。");
    if (state_.phase == ExpansionPhase::ForeignEncounter && state_.foreignStance == ForeignStance::Hostile) {
        return rejected("敌对外族仍在阻拦，不能直接回营。");
    }

    ExpansionState candidate = state_;
    recordSquadTurn(candidate, 1, 1);
    candidate.phase = ExpansionPhase::ReturnSettlement;
    candidate.location = ExpansionLocation::Camp;
    candidate.frontline = 0;
    candidate.enemyLife = 0;
    candidate.enemySpeed = 0;
    candidate.lootAvailable = false;
    candidate.settled = true;
    const int experience = candidate.battleWon ? 120 : candidate.traded ? 55
        : candidate.foreignStance == ForeignStance::Peaceful ? 35 : 15;
    for (Character& member : candidate.squad.members) {
        member.fatigue = std::max(0, member.fatigue - 10);
        const OperationResult gained = gainExperience(member, experience);
        if (!gained) return rejected("回营经验结算失败，状态未改变：" + gained.message);
    }
    if (candidate.battleWon) candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 8);
    else if (candidate.traded) candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 5);
    else if (candidate.retreated) candidate.squad.cohesion = std::max(0, candidate.squad.cohesion - 5);
    else candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 3);
    candidate.supplies += candidate.hides / 2;
    return commit(std::move(candidate), "小队回营完成结算：经验、疲劳、凝聚力和物资均已处理。", true);
}

ExpansionCommandResult ExpansionGame::equip(const std::string_view slotText, const std::string_view itemId) {
    if (state_.phase == ExpansionPhase::FrontlineCombat) return rejected("战斗中禁止更换装备，状态未改变。");
    const auto slot = parseSlot(slotText);
    if (!slot) return rejected("未知装备栏，状态未改变。");
    const auto found = std::find_if(state_.inventory.items().begin(), state_.inventory.items().end(),
        [&](const Item& item) { return item.id == itemId || item.name == itemId; });
    if (found == state_.inventory.items().end()) return rejected("背包中没有该物品，状态未改变。");

    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    Item selected;
    const OperationResult taken = candidate.inventory.take(found->id, selected);
    if (!taken) return rejected("装备失败，状态未改变：" + taken.message);
    const auto& previous = leader.equipment[static_cast<std::size_t>(*slot)];
    if (previous) {
        const OperationResult stored = candidate.inventory.pickupFree(*previous);
        if (!stored) return rejected("装备失败，无法收回原装备，状态未改变：" + stored.message);
    }
    const OperationResult equipped = equipItem(leader, *slot, selected);
    if (!equipped) return rejected("装备失败，状态未改变：" + equipped.message);
    return commit(std::move(candidate), "队长完成装备更换。", false);
}

ExpansionCommandResult ExpansionGame::commit(ExpansionState candidate, std::string message, const bool turnAdvanced) {
    const OperationResult valid = validateState(candidate);
    if (!valid) return rejected("行动后的状态未通过校验，行动已取消：" + valid.message);
    state_ = std::move(candidate);
    return {true, true, true, turnAdvanced, std::move(message)};
}

ExpansionCommandResult ExpansionGame::rejected(std::string message) const {
    return {true, false, false, false, std::move(message)};
}

void ExpansionGame::recordSquadTurn(ExpansionState& candidate, const int leaderFatigue,
    const int followerFatigue) const {
    ++candidate.turn;
    ++candidate.leaderActions;
    candidate.followerActions += static_cast<int>(candidate.squad.members.size() - 1U);
    for (std::size_t index = 0; index < candidate.squad.members.size(); ++index) {
        Character& member = candidate.squad.members[index];
        const int gain = index == candidate.squad.leaderIndex ? leaderFatigue : followerFatigue;
        member.fatigue = std::clamp(member.fatigue + gain, 0, 100);
    }
}

void ExpansionGame::enemyResponse(ExpansionState& candidate, const bool defending) const {
    if (candidate.phase != ExpansionPhase::FrontlineCombat || candidate.squad.members.empty()) return;
    std::vector<std::size_t> livingIndices;
    for (std::size_t index = 0; index < candidate.squad.members.size(); ++index) {
        if (candidate.squad.members[index].life > 0) livingIndices.push_back(index);
    }
    if (livingIndices.empty()) return;
    const std::size_t livingIndex = static_cast<std::size_t>(
        (candidate.seed + static_cast<std::uint32_t>(candidate.turn * 3 + candidate.frontline))
        % static_cast<std::uint32_t>(livingIndices.size()));
    const std::size_t targetIndex = livingIndices[livingIndex];
    Character& target = candidate.squad.members[targetIndex];
    const Attributes attributes = effectiveAttributes(target);
    const int targetSpeed = std::max(0, attributes[Attribute::Agility] * 2
        + attributes[Attribute::Perception] - target.fatigue / 4);
    const int dodgeScore = targetSpeed
        + static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.turn * 11)) % 5U);
    const int accuracy = candidate.enemySpeed * 2
        + static_cast<int>((candidate.seed * 5U + static_cast<std::uint32_t>(candidate.frontline * 7)) % 5U);
    if (dodgeScore >= accuracy) {
        ++candidate.dodges;
        return;
    }
    int damage = 5 + candidate.frontline * 2;
    if (defending || candidate.order == SquadOrder::Hold) damage = std::max(1, damage / 2);
    target.life = std::max(0, target.life - damage);
}

bool ExpansionGame::finishIfLeaderFallen(ExpansionState& candidate, std::string& message) const {
    if (candidate.squad.members[candidate.squad.leaderIndex].life > 0) return false;
    candidate.phase = ExpansionPhase::ReturnSettlement;
    candidate.location = ExpansionLocation::Camp;
    candidate.frontline = 0;
    candidate.enemyLife = 0;
    candidate.enemySpeed = 0;
    candidate.lootAvailable = false;
    candidate.missionFailed = true;
    candidate.settled = true;
    message += message.empty() ? "队长在战斗中阵亡，幸存队员撤回营地，本次任务失败。"
                               : " 队长在战斗中阵亡，幸存队员撤回营地，本次任务失败。";
    return true;
}

void ExpansionGame::advanceFrontline(ExpansionState& candidate, std::string& message) const {
    if (candidate.frontline < 3) {
        ++candidate.frontline;
        candidate.enemyLife = frontlineLife(candidate.frontline);
        candidate.enemySpeed = 7 + static_cast<int>(candidate.seed % 3U) + candidate.frontline;
        message += " 小队突破战线，进入第" + std::to_string(candidate.frontline) + "段。";
        return;
    }
    candidate.phase = ExpansionPhase::ForeignEncounter;
    candidate.foreignStance = ForeignStance::Defeated;
    candidate.frontline = 0;
    candidate.enemyLife = 0;
    candidate.enemySpeed = 0;
    candidate.battleWon = true;
    candidate.lootAvailable = true;
    message += " 三段战线全部突破，外族放下武器，可以免费搜取一次战利品后回营。";
}

int ExpansionGame::squadSpeed(const ExpansionState& state) const {
    int total = 0;
    int living = 0;
    for (const Character& member : state.squad.members) {
        if (member.life <= 0) continue;
        const Attributes attributes = effectiveAttributes(member);
        total += std::max(0, attributes[Attribute::Agility] * 2
            + attributes[Attribute::Perception] - member.fatigue / 5);
        ++living;
    }
    return living == 0 ? 0 : total / living;
}

int ExpansionGame::attackPower(const ExpansionState& state) const {
    const Character& leader = state.squad.members[state.squad.leaderIndex];
    int power = effectiveAttributes(leader)[Attribute::Strength];
    int fatigue = leader.fatigue;
    for (std::size_t index = 0; index < state.squad.members.size(); ++index) {
        if (index == state.squad.leaderIndex || state.squad.members[index].life <= 0) continue;
        power += effectiveAttributes(state.squad.members[index])[Attribute::Strength] / 2;
        fatigue += state.squad.members[index].fatigue;
    }
    power += state.squad.cohesion / 20;
    switch (state.order) {
    case SquadOrder::Advance: power += 4; break;
    case SquadOrder::Hold: power -= 2; break;
    case SquadOrder::Focus: power += 2; break;
    case SquadOrder::Withdraw: power -= 4; break;
    case SquadOrder::Follow: break;
    }
    power -= fatigue / static_cast<int>(state.squad.members.size() * 20U);
    return std::max(1, power);
}

int ExpansionGame::frontlineLife(const int frontline) const {
    return 13 + frontline * 4 + static_cast<int>(state_.seed % 3U);
}

Item ExpansionGame::victoryLoot() const {
    Item item;
    item.id = "forest_trophy_" + std::to_string(state_.seed % 997U);
    item.name = "苍林骨饰";
    item.quality = state_.seed % 5U == 0U ? ItemQuality::Rare : ItemQuality::Fine;
    item.weight = 1;
    item.slotCount = 1;
    item.equipmentSlot = EquipmentSlot::Accessory;
    item.bonuses[Attribute::Perception] = 2;
    item.bonuses[Attribute::Leadership] = 1;
    return item;
}

std::string ExpansionGame::lookText() const {
    if (state_.worldMode) return worldLookText();
    const Character& leader = state_.squad.members[state_.squad.leaderIndex];
    std::ostringstream output;
    output << "阶段：" << phaseName(state_.phase) << "  地点：" << locationName(state_.location)
           << "  回合：" << state_.turn << "\n"
           << "小队：" << state_.squad.name << "  队长：" << leader.name
           << "  人数：" << state_.squad.members.size() << "  军令：" << orderName(state_.order) << "\n"
           << "口粮：" << state_.supplies << "  草药：" << state_.herbs << "  兽皮：" << state_.hides
           << "  药包：" << state_.medicine << "  贸易货物：" << state_.tradeGoods << "\n"
           << "队长行动：" << state_.leaderActions << "  队员跟随：" << state_.followerActions
           << "  闪避：" << state_.dodges;
    if (state_.missionFailed) output << "\n任务结果：失败（队长阵亡）";
    if (state_.phase == ExpansionPhase::FrontlineCombat) {
        output << "\n战线：" << state_.frontline << "/3  敌方生命：" << state_.enemyLife
               << "  双方先手：" << state_.lastPlayerInitiative << ":" << state_.lastEnemyInitiative;
    }
    return output.str();
}

std::string ExpansionGame::stateFingerprint() const {
    std::ostringstream output;
    output << state_.seed << '|' << state_.turn << '|' << static_cast<int>(state_.phase) << '|'
           << static_cast<int>(state_.location) << '|' << static_cast<int>(state_.foreignStance) << '|'
           << static_cast<int>(state_.order) << '|' << state_.supplies << '|' << state_.herbs << '|'
           << state_.hides << '|' << state_.medicine << '|' << state_.tradeGoods << '|'
           << state_.frontline << '|' << state_.enemyLife << '|' << state_.enemySpeed << '|'
           << state_.leaderActions << '|' << state_.followerActions << '|' << state_.dodges << '|'
           << state_.lastPlayerInitiative << '|' << state_.lastEnemyInitiative << '|'
           << state_.traded << state_.battleWon << state_.retreated << state_.missionFailed
           << state_.lootAvailable << state_.settled << '|' << state_.squad.name << '|'
           << state_.squad.leaderIndex << '|' << static_cast<int>(state_.squad.residentMission) << '|'
           << state_.squad.cohesion << '|' << state_.inventory.weightLimit() << '|'
           << state_.inventory.slotLimit();
    for (const Character& member : state_.squad.members) {
        output << "|M:" << member.name << ':' << static_cast<int>(member.occupation) << ':' << member.level
               << ':' << member.experience << ':' << member.growthPoints << ':' << member.life << ':'
               << member.fatigue << ':' << member.loyalty;
        for (const int value : member.attributes.values) output << ':' << value;
        for (const auto& equipped : member.equipment) {
            output << ':' << (equipped ? equipped->id : "-")
                   << ':' << (equipped ? static_cast<int>(equipped->condition) : -1);
        }
    }
    for (const Item& item : state_.inventory.items()) {
        output << "|I:" << item.id << ':' << item.name << ':' << static_cast<int>(item.quality) << ':'
               << static_cast<int>(item.condition) << ':' << item.weight << ':' << item.slotCount << ':'
               << (item.equipmentSlot ? static_cast<int>(*item.equipmentSlot) : -1);
        for (const int value : item.bonuses.values) output << ':' << value;
    }
    output << "|W:" << state_.worldMode << ':' << state_.worldLocation << ':' << state_.cargoFood << ':'
           << state_.cargoWood << ':' << state_.cargoStone << ':' << state_.cargoHerbs << ':'
           << state_.harvestActions << ':' << state_.cargoCapacity << ':' << state_.foodGatherBonus << ':'
           << state_.herbGatherBonus << ':' << state_.rockfangFortCleared;
    for (const bool value : state_.worldDiscovered) output << value;
    for (const bool value : state_.outposts) output << value;
    return output.str();
}

OperationResult ExpansionGame::validateState(const ExpansionState& state) {
    if (!validPhase(state.phase) || !validLocation(state.location)
        || !validStance(state.foreignStance) || !validOrder(state.order)) {
        return rejectedOperation("阶段、地点、外交状态或军令枚举无效。");
    }
    const OperationResult squad = validateSquad(state.squad);
    if (!squad) return rejectedOperation("小队状态无效：" + squad.message);
    if (state.turn < 0 || state.leaderActions != state.turn
        || state.followerActions != state.turn * static_cast<int>(state.squad.members.size() - 1U)) {
        return rejectedOperation("队长回合数与队员自动跟随次数不一致。");
    }
    if (state.supplies < 0 || state.herbs < 0 || state.hides < 0 || state.medicine < 0
        || state.tradeGoods < 0 || state.dodges < 0 || state.lastPlayerInitiative < 0
        || state.lastEnemyInitiative < 0) {
        return rejectedOperation("资源或战斗统计不能为负数。");
    }
    for (const Character& member : state.squad.members) {
        if (member.level <= 0 || member.experience < 0 || member.growthPoints < 0
            || member.loyalty < 0 || member.loyalty > 100
            || member.life < 0 || member.fatigue < 0 || member.fatigue > 100) {
            return rejectedOperation("成员成长、生命、疲劳或忠诚超出范围。");
        }
        for (const int value : member.attributes.values) {
            if (value < kMinimumAttribute || value > kMaximumAttribute) {
                return rejectedOperation("成员属性超出范围。");
            }
        }
        if (member.life > maximumLife(member)) return rejectedOperation("成员生命超过上限。");
    }
    if (state.inventory.usedWeight() > state.inventory.weightLimit()
        || state.inventory.usedSlots() > state.inventory.slotLimit()) {
        return rejectedOperation("背包超过容量。");
    }
    std::unordered_set<std::string> itemIds;
    for (const Item& item : state.inventory.items()) {
        if (item.id.empty() || item.name.empty() || !itemIds.insert(item.id).second) {
            return rejectedOperation("背包物品编号为空或重复。");
        }
    }

    if (state.worldMode) {
        if (state.worldLocation < 0 || state.worldLocation >= static_cast<int>(kExpeditionWorldLocationCount)
            || !state.worldDiscovered[static_cast<std::size_t>(state.worldLocation)]
            || !state.worldDiscovered[0] || !state.outposts[0]
            || state.cargoFood < 0 || state.cargoWood < 0 || state.cargoStone < 0 || state.cargoHerbs < 0
            || state.harvestActions < 0 || state.harvestActions > 4 || state.cargoCapacity <= 0
            || cargoTotal(state) > state.cargoCapacity || state.foodGatherBonus < 0 || state.herbGatherBonus < 0) {
            return rejectedOperation("世界任务的位置、发现状态、前哨或载货字段无效。");
        }
        if (state.phase != ExpansionPhase::ForestExploration
            && state.phase != ExpansionPhase::ReturnSettlement) {
            return rejectedOperation("世界任务阶段无效。");
        }
        if (state.settled != (state.phase == ExpansionPhase::ReturnSettlement)) {
            return rejectedOperation("世界任务结算标记与阶段不一致。");
        }
        if (state.phase == ExpansionPhase::ReturnSettlement) {
            const std::size_t location = static_cast<std::size_t>(state.worldLocation);
            if (location != 0U && !state.outposts[location]) return rejectedOperation("世界任务只能在营地或前哨结算。");
        }
        return accepted("世界地图任务状态合法。");
    }

    if ((state.phase == ExpansionPhase::CampPreparation
            || state.phase == ExpansionPhase::ReturnSettlement)
        && state.location != ExpansionLocation::Camp) {
        return rejectedOperation("营地阶段必须位于营地。");
    }
    if (state.phase == ExpansionPhase::ForestExploration
        && state.location != ExpansionLocation::ForestEdge
        && state.location != ExpansionLocation::DeepForest
        && state.location != ExpansionLocation::HuntingGround) {
        return rejectedOperation("森林探索阶段地点无效。");
    }
    if ((state.phase == ExpansionPhase::ForeignEncounter
            || state.phase == ExpansionPhase::FrontlineCombat)
        && state.location != ExpansionLocation::StrangerClearing) {
        return rejectedOperation("外族遭遇和战斗必须发生在林间空地。");
    }
    if (state.phase == ExpansionPhase::FrontlineCombat) {
        if (state.foreignStance != ForeignStance::Hostile || state.frontline < 1 || state.frontline > 3
            || state.enemyLife <= 0 || state.enemySpeed <= 0 || state.battleWon) {
            return rejectedOperation("三段战线状态无效。");
        }
    } else if (state.frontline != 0 || state.enemyLife != 0 || state.enemySpeed != 0) {
        return rejectedOperation("非战斗阶段不能保留战线数值。");
    }
    if (state.battleWon && state.foreignStance != ForeignStance::Defeated) {
        return rejectedOperation("战斗胜利必须对应外族败退状态。");
    }
    if (state.missionFailed) {
        if (state.phase != ExpansionPhase::ReturnSettlement || !state.settled
            || state.squad.members[state.squad.leaderIndex].life > 0 || state.battleWon) {
            return rejectedOperation("任务失败状态与队长阵亡或结算阶段不一致。");
        }
    } else if (state.phase != ExpansionPhase::ReturnSettlement
        && state.squad.members[state.squad.leaderIndex].life <= 0) {
        return rejectedOperation("活动任务中的队长必须存活。");
    }
    if (state.lootAvailable
        && (!state.battleWon || state.phase != ExpansionPhase::ForeignEncounter)) {
        return rejectedOperation("只有三段战线胜利后可以搜取战利品。");
    }
    if (state.settled != (state.phase == ExpansionPhase::ReturnSettlement)) {
        return rejectedOperation("回营结算标记与阶段不一致。");
    }
    return accepted("扩展玩法状态合法。");
}

std::string ExpansionGame::phaseName(const ExpansionPhase phase) {
    switch (phase) {
    case ExpansionPhase::CampPreparation: return "营地准备";
    case ExpansionPhase::ForestExploration: return "森林探索";
    case ExpansionPhase::ForeignEncounter: return "外族遭遇";
    case ExpansionPhase::FrontlineCombat: return "三段战线战斗";
    case ExpansionPhase::ReturnSettlement: return "回营结算";
    }
    return "未知阶段";
}

std::string ExpansionGame::locationName(const ExpansionLocation location) {
    switch (location) {
    case ExpansionLocation::Camp: return "燧火营地";
    case ExpansionLocation::ForestEdge: return "苍林边缘";
    case ExpansionLocation::DeepForest: return "苍林深处";
    case ExpansionLocation::HuntingGround: return "狩猎地";
    case ExpansionLocation::StrangerClearing: return "外族空地";
    }
    return "未知地点";
}

std::string ExpansionGame::orderName(const SquadOrder order) {
    switch (order) {
    case SquadOrder::Follow: return "跟随";
    case SquadOrder::Advance: return "突进";
    case SquadOrder::Hold: return "坚守";
    case SquadOrder::Focus: return "集火";
    case SquadOrder::Withdraw: return "后撤";
    }
    return "未知军令";
}

} // namespace tribe
