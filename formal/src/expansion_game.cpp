#include "tribe/expansion_game.hpp"

#include <algorithm>
#include <array>
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
