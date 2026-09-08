#include "tribe/expansion_game.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tribe {
namespace {

struct ParsedCommand { std::string verb; std::vector<std::string> args; };

std::string lower(std::string value) {
    for (char& character : value) {
        if (static_cast<unsigned char>(character) < 128U) character = static_cast<char>(std::tolower(character));
    }
    return value;
}

ParsedCommand parse(std::string_view input) {
    std::istringstream stream{std::string(input)};
    ParsedCommand command;
    stream >> command.verb;
    command.verb = lower(std::move(command.verb));
    for (std::string argument; stream >> argument;) command.args.push_back(lower(std::move(argument)));
    return command;
}

bool any(std::string_view value, std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

const std::array<std::string_view, kExpeditionWorldLocationCount> kNames{{
    "燧火营地", "苍林", "红土原", "芦苇沼泽", "河鹿渡口", "白羽营地", "燧石矿场", "古老山隘",
    "岩牙要塞", "盐风海岸", "潮盐港", "贝壳滩", "玄石谷", "玄石工坊", "山前集市", "断崖商道"}};

const std::array<std::vector<int>, kExpeditionWorldLocationCount> kRoads{{
    {1, 2}, {0, 3}, {0, 4, 6}, {1, 5, 9}, {2, 14}, {3}, {2, 12}, {15, 8},
    {7}, {3, 11}, {11, 14}, {9, 10}, {6, 13}, {12, 15}, {4, 10, 15}, {13, 14, 7}}};

const std::array<std::pair<int, int>, kExpeditionWorldLocationCount> kCoordinates{{
    {3,3}, {2,3}, {4,3}, {2,2}, {5,3}, {1,2}, {4,4}, {5,1},
    {6,1}, {3,2}, {7,2}, {5,2}, {5,4}, {6,4}, {6,3}, {7,3}}};

std::optional<int> location(std::string_view value) {
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && number >= 1 && number <= 16) return number - 1;
    static const std::array<std::vector<std::string_view>, 16> aliases{{
        {"camp", "营地", "燧火营地"}, {"forest", "苍林"}, {"plain", "redplain", "红土原"}, {"marsh", "芦苇沼泽"},
        {"ford", "riverford", "河鹿渡口"}, {"whitecamp", "白羽营地"}, {"quarry", "燧石矿场"}, {"pass", "古老山隘"},
        {"fort", "rockfang", "岩牙要塞"}, {"coast", "盐风海岸"}, {"harbor", "潮盐港"}, {"beach", "贝壳滩"},
        {"valley", "玄石谷"}, {"workshop", "玄石工坊"}, {"market", "山前集市"}, {"road", "断崖商道"}}};
    for (std::size_t index = 0; index < aliases.size(); ++index) {
        if (std::find(aliases[index].begin(), aliases[index].end(), value) != aliases[index].end()) return static_cast<int>(index);
    }
    return std::nullopt;
}

int cargoTotal(const ExpansionState& state) {
    return state.cargoFood + state.cargoWood + state.cargoStone + state.cargoHerbs + state.cargoHides;
}

bool road(int from, int to) {
    const auto& roads = kRoads[static_cast<std::size_t>(from)];
    return std::find(roads.begin(), roads.end(), to) != roads.end();
}

std::string direction(int from, int to) {
    const auto [x1, y1] = kCoordinates[static_cast<std::size_t>(from)];
    const auto [x2, y2] = kCoordinates[static_cast<std::size_t>(to)];
    return std::string(y2 < y1 ? "北" : y2 > y1 ? "南" : "") + (x2 < x1 ? "西" : x2 > x1 ? "东" : "");
}

Character makeMember(const char* name, Occupation occupation) {
    Character member{name, occupation};
    member.attributes = Attributes{5};
    member.loyalty = 70;
    if (occupation == Occupation::Warrior) { member.attributes[Attribute::Strength] = 9; member.attributes[Attribute::Endurance] = 8; }
    if (occupation == Occupation::Scout) { member.attributes[Attribute::Agility] = 9; member.attributes[Attribute::Perception] = 8; }
    if (occupation == Occupation::Hunter) { member.attributes[Attribute::Survival] = 8; member.attributes[Attribute::Perception] = 8; }
    member.life = maximumLife(member);
    return member;
}

OperationResult invalid(std::string message) { return {false, std::move(message)}; }
OperationResult valid(std::string message) { return {true, std::move(message)}; }

} // namespace

ExpansionGame::ExpansionGame(std::uint32_t seed, std::size_t squadSize) {
    if (squadSize < kMinimumSquadSize || squadSize > kMaximumSquadSize) throw std::invalid_argument("地图小队人数必须为2至8人。");
    state_.seed = seed;
    state_.squad.name = "晨火队";
    state_.squad.cohesion = 72;
    static const std::array<std::pair<const char*, Occupation>, 8> roster{{
        {"青枝", Occupation::Scout}, {"石刃", Occupation::Warrior}, {"苍眼", Occupation::Hunter}, {"白榆", Occupation::Healer},
        {"逐鹿", Occupation::Hunter}, {"岩槌", Occupation::Crafter}, {"芦风", Occupation::Hunter}, {"河矛", Occupation::Warrior}}};
    for (std::size_t index = 0; index < squadSize; ++index) state_.squad.members.push_back(makeMember(roster[index].first, roster[index].second));
    state_.worldDiscovered[0] = true;
    state_.outposts[0] = true;
}

ExpansionGame::ExpansionGame(ExpansionState state) : state_(std::move(state)) {
    const OperationResult result = validateState(state_);
    if (!result) throw std::invalid_argument("地图任务状态无效：" + result.message);
}

ExpansionCommandResult ExpansionGame::execute(std::string_view input) {
    const ParsedCommand command = parse(input);
    if (command.verb.empty()) return {};
    if (any(command.verb, {"look", "查看", "map", "地图"})) return command.args.empty() ? ExpansionCommandResult{true, true, false, false, lookText()} : rejected("用法：look / 查看");
    if (state_.encounterLife > 0) {
        if (any(command.verb, {"attack", "攻击"}) && command.args.empty()) return attackEncounter();
        if (any(command.verb, {"defend", "防御"}) && command.args.empty()) return defendEncounter();
        if (any(command.verb, {"retreat", "撤退"}) && command.args.empty()) return retreatEncounter();
        return rejected("岩牙巡逻拦住道路；只能攻击、防御、撤退或查看。");
    }
    if (any(command.verb, {"move", "移动"})) return command.args.size() == 1U ? move(command.args.front()) : rejected("用法：move <相邻地点>");
    if (any(command.verb, {"gather", "采集"})) return command.args.size() == 1U ? gather(command.args.front()) : rejected("用法：gather <资源>");
    if (any(command.verb, {"attack", "攻击"})) return command.args.empty() ? attackEncounter() : rejected("用法：attack");
    if (any(command.verb, {"use", "使用"}) && command.args.size() == 1U && any(command.args.front(), {"herb", "herbs", "草药"})) return useHerb();
    if (any(command.verb, {"buildoutpost", "outpost", "建造前哨"}) || (any(command.verb, {"build", "建造"}) && command.args.size() == 1U && any(command.args.front(), {"outpost", "前哨"}))) return buildOutpost();
    if (any(command.verb, {"settle", "结算"})) return command.args.empty() ? settle() : rejected("用法：settle");
    return {};
}

ExpansionCommandResult ExpansionGame::move(std::string_view target) {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const auto destination = location(target);
    if (!destination) return rejected("未知地点；输入地图可查看1至16号地点。");
    if (*destination == state_.worldLocation) return rejected("小队已经在这里。");
    if (!road(state_.worldLocation, *destination)) return rejected("两地不相邻，不能跨越道路移动。");
    ExpansionState candidate = state_;
    candidate.worldLocation = *destination;
    const bool discovered = !candidate.worldDiscovered[static_cast<std::size_t>(*destination)];
    candidate.worldDiscovered[static_cast<std::size_t>(*destination)] = true;
    recordTurn(candidate, 2, 1);
    std::string message = "小队沿道路抵达" + std::string(kNames[static_cast<std::size_t>(*destination)]) + "。";
    if (discovered) message += " 新地点已发现。";
    return commit(std::move(candidate), std::move(message), true);
}

ExpansionCommandResult ExpansionGame::gather(std::string_view resource) {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    if (state_.harvestActions >= 4) return rejected("本次任务的采集时段已用完，请前往营地或前哨结算。");
    enum class Cargo { Food = 0, Wood, Stone, Herbs, Hides };
    const int at = state_.worldLocation;
    std::optional<Cargo> kind;
    int base = 0;
    if (any(resource, {"food", "食物", "粮食"}) && (at == 1 || at == 2 || at == 4 || at == 9)) { kind = Cargo::Food; base = 6 + state_.foodGatherBonus; }
    else if (any(resource, {"wood", "木材"}) && (at == 1 || at == 3)) { kind = Cargo::Wood; base = 6; }
    else if (any(resource, {"stone", "石料", "石头"}) && (at == 6 || at == 7 || at == 12)) { kind = Cargo::Stone; base = 5; }
    else if (any(resource, {"herb", "herbs", "草药"}) && (at == 1 || at == 3 || at == 5)) { kind = Cargo::Herbs; base = 4 + state_.herbGatherBonus; }
    else if (any(resource, {"hide", "hides", "兽皮"}) && (at == 1 || at == 2)) { kind = Cargo::Hides; base = 4; }
    else return rejected("当前地点没有这种资源。");
    if (static_cast<int>(*kind) != state_.assignedResource && !(*kind == Cargo::Hides && state_.assignedResource == 0)) return rejected("本次任务由指定资源队执行，不能混采。");
    const int room = state_.cargoCapacity - cargoTotal(state_);
    if (room <= 0) return rejected("小队载货已满，请结算。");
    ExpansionState candidate = state_;
    const int gain = std::min(room, base + std::max(0, candidate.crewSize - 2));
    std::string_view cargoName;
    switch (*kind) {
    case Cargo::Food: candidate.cargoFood += gain; cargoName = "食物"; break;
    case Cargo::Wood: candidate.cargoWood += gain; cargoName = "木材"; break;
    case Cargo::Stone: candidate.cargoStone += gain; cargoName = "石料"; break;
    case Cargo::Herbs: candidate.cargoHerbs += gain; cargoName = "草药"; break;
    case Cargo::Hides: candidate.cargoHides += gain; cargoName = "兽皮"; break;
    }
    ++candidate.harvestActions;
    recordTurn(candidate, 4, 2);
    return commit(std::move(candidate), "小队采集" + std::string(cargoName) + "，装载" + std::to_string(gain) + "单位。", true);
}

ExpansionCommandResult ExpansionGame::buildOutpost() {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    if (at == 0U) return rejected("营地无需建造前哨。");
    if (state_.outposts[at]) return rejected("该地点已经有前哨。");
    if (state_.worldLocation == 8 && !state_.encounterDefeated) return rejected("岩牙要塞仍有敌对巡逻，不能建造前哨。");
    if (state_.cargoWood < 6 || state_.cargoStone < 4) return rejected("建造前哨需要现场携带木材6、石料4。");
    ExpansionState candidate = state_;
    candidate.cargoWood -= 6; candidate.cargoStone -= 4; candidate.outposts[at] = true;
    recordTurn(candidate, 5, 3);
    return commit(std::move(candidate), "小队建成前哨；这里现在可作为结算点。", true);
}

ExpansionCommandResult ExpansionGame::settle() {
    if (state_.phase != ExpansionPhase::Exploring) return rejected("任务已结算。");
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    if (!state_.outposts[at]) return rejected("这里只能停留；请在营地或已建前哨结算。");
    ExpansionState candidate = state_;
    candidate.phase = ExpansionPhase::Settled; candidate.settled = true;
    recordTurn(candidate, 1, 1);
    for (Character& member : candidate.squad.members) {
        const OperationResult gained = gainExperience(member, 15 + candidate.harvestActions * 10);
        if (!gained) return rejected("经验结算失败：" + gained.message);
    }
    candidate.squad.cohesion = std::min(100, candidate.squad.cohesion + 2);
    return commit(std::move(candidate), at == 0U ? "小队在燧火营地结算。" : "小队在前哨结算并驻留。", true);
}

ExpansionCommandResult ExpansionGame::attackEncounter() {
    if (state_.worldLocation != 8 || state_.encounterDefeated) return rejected("这里没有可攻击的遭遇。");
    ExpansionState candidate = state_;
    if (candidate.encounterLife == 0) candidate.encounterLife = 14;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const Attributes attributes = effectiveAttributes(leader);
    const int damage = std::max(2, attributes[Attribute::Strength] / 2 + attributes[Attribute::Agility] / 4);
    candidate.encounterLife = std::max(0, candidate.encounterLife - damage);
    recordTurn(candidate, 4, 2);
    if (candidate.encounterLife == 0) {
        candidate.encounterDefeated = true;
        Item badge; badge.id = "rockfang_badge_" + std::to_string(candidate.seed); badge.name = "岩牙巡逻徽记"; badge.weight = 1; badge.equipmentSlot = EquipmentSlot::Accessory; badge.bonuses[Attribute::Willpower] = 1;
        const OperationResult stored = candidate.backpack.pickupFree(std::move(badge));
        return commit(std::move(candidate), stored ? "小队击退岩牙巡逻，徽记已放入任务背包；要塞仍需军队占领。" : "小队击退岩牙巡逻；任务背包已满，未带走徽记。", true);
    }
    const int loss = std::max(1, 5 - attributes[Attribute::Endurance] / 4);
    leader.life = std::max(1, leader.life - loss);
    return commit(std::move(candidate), "小队攻击巡逻，敌军生命-" + std::to_string(damage) + "；队长生命-" + std::to_string(loss) + "。", true);
}

ExpansionCommandResult ExpansionGame::defendEncounter() {
    if (state_.encounterLife <= 0 || state_.worldLocation != 8) return rejected("当前没有遭遇战。");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    const int loss = std::max(1, 3 - effectiveAttributes(leader)[Attribute::Endurance] / 5);
    leader.life = std::max(1, leader.life - loss);
    recordTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队结阵防御，队长生命-" + std::to_string(loss) + "。", true);
}

ExpansionCommandResult ExpansionGame::retreatEncounter() {
    if (state_.encounterLife <= 0 || state_.worldLocation != 8) return rejected("当前没有遭遇战。");
    ExpansionState candidate = state_;
    candidate.encounterLife = 0; candidate.worldLocation = 7;
    recordTurn(candidate, 2, 1);
    return commit(std::move(candidate), "小队撤回古老山隘。", true);
}

ExpansionCommandResult ExpansionGame::useHerb() {
    if (state_.cargoHerbs <= 0) return rejected("任务载货中没有草药。");
    ExpansionState candidate = state_;
    Character& leader = candidate.squad.members[candidate.squad.leaderIndex];
    --candidate.cargoHerbs; leader.life = std::min(maximumLife(leader), leader.life + 18); leader.fatigue = std::max(0, leader.fatigue - 20);
    return commit(std::move(candidate), "队长使用草药，生命恢复、疲劳下降。", true);
}

std::string ExpansionGame::lookText() const {
    const std::size_t at = static_cast<std::size_t>(state_.worldLocation);
    std::ostringstream output;
    output << "北↑  十六地点道路图\n当前地点：" << (state_.worldLocation + 1) << '.' << kNames[at] << "\n相邻道路：";
    for (const int neighbor : kRoads[at]) output << direction(state_.worldLocation, neighbor) << "→" << (neighbor + 1) << '.' << kNames[static_cast<std::size_t>(neighbor)] << ' ';
    output << "\n载货 " << cargoTotal(state_) << '/' << state_.cargoCapacity << "：食物" << state_.cargoFood << " 木材" << state_.cargoWood << " 石料" << state_.cargoStone << " 草药" << state_.cargoHerbs << " 兽皮" << state_.cargoHides << "；采集" << state_.harvestActions << "/4\n可采资源：";
    bool printed = false; const auto add = [&](std::string_view name) { output << (printed ? "、" : "") << name; printed = true; };
    if (at == 1 || at == 2 || at == 4 || at == 9) add("食物"); if (at == 1 || at == 3) add("木材"); if (at == 6 || at == 7 || at == 12) add("石料"); if (at == 1 || at == 3 || at == 5) add("草药"); if (at == 1 || at == 2) add("兽皮"); if (!printed) output << "无";
    output << "\n结算：" << (state_.outposts[at] ? "当前地点可结算" : "需前往营地或前哨") << "。";
    if (state_.worldLocation == 8 && !state_.encounterDefeated) output << "\n遭遇：岩牙巡逻；可用攻击、防御、撤退。";
    if (state_.encounterLife > 0) output << " 敌军生命" << state_.encounterLife << '。';
    return output.str();
}

OperationResult ExpansionGame::validateState(const ExpansionState& state) {
    if (state.phase != ExpansionPhase::Exploring && state.phase != ExpansionPhase::Settled) return invalid("任务阶段无效。");
    if (state.worldLocation < 0 || state.worldLocation >= 16 || !state.worldDiscovered[0] || !state.outposts[0] || !state.worldDiscovered[static_cast<std::size_t>(state.worldLocation)]) return invalid("地图位置或营地状态无效。");
    if ((state.phase == ExpansionPhase::Settled) != state.settled || (state.settled && !state.outposts[static_cast<std::size_t>(state.worldLocation)])) return invalid("结算点状态无效。");
    if (state.turn < 0 || state.cargoFood < 0 || state.cargoWood < 0 || state.cargoStone < 0 || state.cargoHerbs < 0 || state.cargoHides < 0 || state.harvestActions < 0 || state.harvestActions > 4 || state.cargoCapacity <= 0 || cargoTotal(state) > state.cargoCapacity || state.foodGatherBonus < 0 || state.herbGatherBonus < 0 || state.assignedResource < 0 || state.assignedResource > 5 || state.crewSize < 2 || state.crewSize > 6 || state.encounterLife < 0) return invalid("载货、劳力或遭遇字段无效。");
    const OperationResult squad = validateSquad(state.squad); if (!squad) return invalid("小队无效：" + squad.message);
    if (state.backpack.usedWeight() > state.backpack.weightLimit() || state.backpack.usedSlots() > state.backpack.slotLimit()) return invalid("任务背包超出容量。");
    std::unordered_set<std::string> ids;
    for (const Item& item : state.backpack.items()) if (item.id.empty() || !ids.insert(item.id).second) return invalid("任务背包物品编号无效。");
    return valid("地图任务状态合法。");
}

ExpansionCommandResult ExpansionGame::commit(ExpansionState candidate, std::string message, bool turnAdvanced) {
    const OperationResult check = validateState(candidate);
    if (!check) return rejected("操作已取消：" + check.message);
    state_ = std::move(candidate);
    return {true, true, true, turnAdvanced, std::move(message)};
}

ExpansionCommandResult ExpansionGame::rejected(std::string message) const { return {true, false, false, false, std::move(message)}; }

void ExpansionGame::recordTurn(ExpansionState& candidate, int leaderFatigue, int followerFatigue) const {
    ++candidate.turn;
    for (std::size_t index = 0; index < candidate.squad.members.size(); ++index) candidate.squad.members[index].fatigue = std::clamp(candidate.squad.members[index].fatigue + (index == candidate.squad.leaderIndex ? leaderFatigue : followerFatigue), 0, 100);
    candidate.squad.cohesion = std::clamp(candidate.squad.cohesion, 0, 100);
}

} // namespace tribe
