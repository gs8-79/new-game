#include "tribe/game_engine.hpp"

#include "tribe/content.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

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

bool verbIs(const ParsedCommand& command, const std::initializer_list<std::string_view> aliases) {
    return std::find(aliases.begin(), aliases.end(), command.verb) != aliases.end();
}

bool has(const GameState& state, const BuildingId id) { return state.buildings[indexOf(id)]; }
bool has(const GameState& state, const TechnologyId id) { return state.technologies[indexOf(id)]; }
bool found(const GameState& state, const LocationId id) { return state.discovered[indexOf(id)]; }

int clampRelation(const int value) { return std::clamp(value, -100, 100); }
int clampMorale(const int value) { return std::clamp(value, 0, 100); }

std::string discoveredName(const GameState& state, const LocationId id) {
    return found(state, id) ? location(id).chineseName.data() : "????";
}

std::string numericAlias(const std::string& value, const bool awaitingRaid) {
    static const std::unordered_map<std::string, std::string> aliases{
        {"1", "status"}, {"2", "map"}, {"3", "gather food"}, {"4", "gather wood"},
        {"5", "gather stone"}, {"6", "gather herbs"}, {"7", "train"}, {"8", "endturn"},
        {"10", "celebrate"},
        {"11", "scout marsh"}, {"12", "scout riverford"}, {"13", "scout whitefeather"},
        {"14", "scout quarry"}, {"15", "scout pass"}, {"16", "scout rockfangfort"},
        {"21", "build granary"}, {"22", "build wall"}, {"23", "build workshop"},
        {"24", "build healer"}, {"25", "build watchtower"}, {"26", "build council"},
        {"31", "research preservation"}, {"32", "research herbalism"}, {"33", "research irrigation"},
        {"34", "research spear"}, {"35", "research shield"}, {"36", "research ambush"},
        {"37", "research gifts"}, {"38", "research language"}, {"39", "research confederation"},
        {"41", "talk riverdeer"}, {"42", "talk whitefeather"}, {"43", "talk rockfang"},
        {"44", "gift riverdeer"}, {"45", "gift whitefeather"}, {"46", "gift rockfang"},
        {"47", "quest riverdeer"}, {"48", "quest whitefeather"}, {"49", "quest rockfang"},
        {"71", "choose alliance"}, {"72", "choose conquest"}, {"73", "choose prosperity"},
        {"74", "choose migration"},
    };
    if (value == "51") return awaitingRaid ? "battle assault" : "attack rockfang assault";
    if (value == "52") return awaitingRaid ? "battle ambush" : "attack rockfang ambush";
    if (value == "53") return awaitingRaid ? "battle defend" : "attack rockfang defend";
    if (value == "54") return awaitingRaid ? "battle retreat" : "attack rockfang retreat";
    const auto foundAlias = aliases.find(value);
    return foundAlias == aliases.end() ? value : foundAlias->second;
}

std::string checkbox(const bool value) { return value ? "[完成]" : "[未完成]"; }

int countTrue(const std::array<bool, kBuildingCount>& values) {
    return static_cast<int>(std::count(values.begin(), values.end(), true));
}

int countTrue(const std::array<bool, kTechnologyCount>& values) {
    return static_cast<int>(std::count(values.begin(), values.end(), true));
}

bool endingAvailableForState(const GameState& state, const Ending ending) {
    switch (ending) {
    case Ending::Alliance:
        return state.quests[indexOf(FactionId::RiverDeer)] >= 3
            && state.quests[indexOf(FactionId::WhiteFeather)] >= 3
            && state.relations[indexOf(FactionId::RiverDeer)] >= 70
            && state.relations[indexOf(FactionId::WhiteFeather)] >= 70
            && has(state, TechnologyId::Confederation)
            && (state.rockfangTruce || state.rockfangFortCaptured);
    case Ending::Conquest:
        return state.rockfangFortCaptured && state.warriors >= 6 && state.morale >= 45;
    case Ending::Prosperity:
        return state.population >= 20 && state.food >= 40
            && countTrue(state.buildings) >= 4 && countTrue(state.technologies) >= 4;
    case Ending::Migration:
        return state.population > 0 && state.campDurability > 0;
    case Ending::Extinction:
        return state.population <= 0 || state.campDurability <= 0;
    case Ending::None:
        break;
    }
    return false;
}

} // namespace

GameEngine::GameEngine(const GameConfig config) { newGame(config); }

std::array<int, kSeasonCount> GameEngine::eventScheduleForSeed(const std::uint32_t seed) {
    std::vector<int> optionalEvents(18);
    std::iota(optionalEvents.begin(), optionalEvents.end(), 0);
    std::mt19937 generator(seed);
    for (std::size_t index = optionalEvents.size(); index > 1U; --index) {
        const std::size_t swapIndex = static_cast<std::size_t>(generator()) % index;
        std::swap(optionalEvents[index - 1U], optionalEvents[swapIndex]);
    }

    std::array<int, kSeasonCount> schedule{};
    std::size_t optionalIndex = 0;
    for (std::size_t index = 0; index < schedule.size(); ++index) {
        if (index == 3U) schedule[index] = static_cast<int>(EventId::RiverEnvoys);
        else if (index == 7U) schedule[index] = static_cast<int>(EventId::WhiteFeatherSign);
        else if (index == 11U) schedule[index] = static_cast<int>(EventId::RockfangRaid);
        else if (index == 15U) schedule[index] = static_cast<int>(EventId::FinalCouncil);
        else schedule[index] = optionalEvents.at(optionalIndex++);
    }
    return schedule;
}

void GameEngine::newGame(const GameConfig config) {
    GameState initial;
    initial.mode = config.mode;
    initial.seed = config.seed;
    initial.eventSchedule = eventScheduleForSeed(config.seed);
    initial.discovered[indexOf(LocationId::Camp)] = true;
    initial.discovered[indexOf(LocationId::Forest)] = true;
    initial.discovered[indexOf(LocationId::RedPlain)] = true;
    initial.scouted[indexOf(LocationId::Camp)] = true;
    initial.scouted[indexOf(LocationId::Forest)] = true;
    initial.scouted[indexOf(LocationId::RedPlain)] = true;

    if (config.mode == GameMode::Quick) {
        initial.turn = 9;
        initial.population = 18;
        initial.food = 40;
        initial.wood = 20;
        initial.stone = 10;
        initial.herbs = 5;
        initial.warriors = 4;
        initial.morale = 65;
        initial.relations = {{35, 10, -35}};
        initial.buildings[indexOf(BuildingId::Granary)] = true;
        initial.technologies[indexOf(TechnologyId::FoodPreservation)] = true;
        initial.discovered[indexOf(LocationId::Marsh)] = true;
        initial.discovered[indexOf(LocationId::RiverFord)] = true;
        initial.scouted[indexOf(LocationId::Marsh)] = true;
        initial.scouted[indexOf(LocationId::RiverFord)] = true;
    }
    initial.actionsLeft = teamsForPopulation(initial.population);
    initial.currentEvent = initial.eventSchedule.at(static_cast<std::size_t>(initial.turn - 1));
    applyEvent(initial, static_cast<EventId>(initial.currentEvent), openingMessage_);
    finishExtinctionIfNeeded(initial, openingMessage_);
    state_ = std::move(initial);
}

bool GameEngine::replaceState(const GameState& candidate, std::string& error) {
    if (!validateState(candidate, error)) return false;
    state_ = candidate;
    openingMessage_.clear();
    error.clear();
    return true;
}

ActionResult GameEngine::execute(const std::string_view input) {
    std::string normalized(input);
    ParsedCommand initialCommand = parseCommand(normalized);
    if (!initialCommand.verb.empty() && initialCommand.args.empty()) {
        normalized = numericAlias(initialCommand.verb, state_.phase == Phase::AwaitingRaid);
    }
    const ParsedCommand command = parseCommand(normalized);
    if (command.verb.empty()) return {};

    if (verbIs(command, {"help", "帮助"})) {
        return command.args.empty() ? ActionResult{true, false, false, false, helpText()}
                                    : rejected("用法：help / 帮助");
    }
    if (verbIs(command, {"status", "状态"})) {
        return command.args.empty() ? ActionResult{true, false, false, false, statusText()}
                                    : rejected("用法：status / 状态");
    }
    if (verbIs(command, {"map", "地图"})) {
        return command.args.empty() ? ActionResult{true, false, false, false, mapText()}
                                    : rejected("用法：map / 地图");
    }
    if (verbIs(command, {"objectives", "目标", "任务"})) {
        return command.args.empty() ? ActionResult{true, false, false, false, objectivesText()}
                                    : rejected("用法：objectives / 目标");
    }

    if (state_.phase == Phase::Finished) {
        return rejected("本局已经结束，结局：" + endingName(state_.ending) + "。请返回主菜单开始新游戏。");
    }
    if (state_.phase == Phase::FinalChoice) {
        if (verbIs(command, {"choose", "选择"}) && command.args.size() == 1U) {
            return chooseEnding(command.args.front());
        }
        return rejected("四年已经结束，请先使用 choose/选择 决定部落结局。输入 objectives 查看可选道路。");
    }
    if (state_.phase == Phase::AwaitingRaid) {
        if (verbIs(command, {"battle", "应战", "迎战"}) && command.args.size() == 1U) {
            const auto tactic = findTactic(command.args.front());
            return tactic ? attack(*tactic, true) : rejected("未知战术。可选：正面、伏击、防守、撤退。");
        }
        return rejected("岩牙正在进攻，必须先输入 battle <tactic> / 应战 <战术>。");
    }

    if (verbIs(command, {"gather", "采集"})) {
        return command.args.size() == 1U ? gather(command.args.front())
                                         : rejected("用法：gather food|wood|stone|herbs / 采集 食物|木材|石料|草药");
    }
    if (verbIs(command, {"guard", "守卫", "防卫"})) {
        return command.args.empty() ? guardCamp() : rejected("用法：guard / 守卫");
    }
    if (verbIs(command, {"celebrate", "inspire", "鼓舞", "庆典"})) {
        return command.args.empty() ? celebrate() : rejected("用法：celebrate / 鼓舞");
    }
    if (verbIs(command, {"scout", "explore", "侦察", "探索"})) {
        return command.args.size() == 1U ? scout(command.args.front())
                                         : rejected("用法：scout <location> / 侦察 <地点>");
    }
    if (verbIs(command, {"train", "训练"})) {
        return command.args.empty() ? trainWarrior() : rejected("用法：train / 训练");
    }
    if (verbIs(command, {"build", "建造"})) {
        return command.args.size() == 1U ? build(command.args.front())
                                         : rejected("用法：build <building> / 建造 <建筑>");
    }
    if (verbIs(command, {"research", "研究"})) {
        return command.args.size() == 1U ? research(command.args.front())
                                         : rejected("用法：research <technology> / 研究 <技术>");
    }
    if (verbIs(command, {"talk", "交谈", "gift", "赠礼", "quest", "协助", "任务"})) {
        return command.args.size() == 1U ? diplomacy(command.verb, command.args.front())
                                         : rejected("用法：talk|gift|quest <faction> / 交谈|赠礼|协助 <部落>");
    }
    if (verbIs(command, {"attack", "进攻", "攻击"})) {
        if (command.args.size() != 2U) return rejected("用法：attack rockfang <assault|ambush|retreat> / 进攻 岩牙 <正面|伏击|撤退>");
        const auto faction = findFaction(command.args[0]);
        const auto tactic = findTactic(command.args[1]);
        if (!faction || *faction != FactionId::Rockfang || !tactic) return rejected("只能选择岩牙部落和合法战术。");
        return attack(*tactic, false);
    }
    if (verbIs(command, {"endturn", "end", "结束回合", "结束", "下一季"})) {
        return command.args.empty() ? endSeason() : rejected("用法：endturn / 结束回合 / 下一季");
    }
    return {};
}

bool GameEngine::canUseAction(ActionResult& result) const {
    if (state_.actionsLeft > 0) return true;
    result = rejected("本季已经没有可用小队，请结束回合进入下一季。");
    return false;
}

ActionResult GameEngine::commitAction(GameState candidate, std::string message) {
    --candidate.actionsLeft;
    finishExtinctionIfNeeded(candidate, message);
    state_ = std::move(candidate);
    return {true, true, true, false, std::move(message)};
}

ActionResult GameEngine::rejected(std::string message) const {
    return {true, false, false, false, std::move(message)};
}

ActionResult GameEngine::gather(const std::string_view resource) {
    ActionResult result;
    if (!canUseAction(result)) return result;
    GameState candidate = state_;
    std::ostringstream message;

    if (resource == "food" || resource == "食物") {
        int gain = seasonForTurn(candidate.turn) == Season::Winter ? 8 : 12;
        if (has(candidate, TechnologyId::FoodPreservation)) gain += 2;
        if (has(candidate, TechnologyId::Irrigation)) gain += 3;
        const int capacity = has(candidate, BuildingId::Granary) ? 100 : 60;
        const int actual = std::min(gain, capacity - candidate.food);
        if (actual <= 0) return rejected("食物储存已经达到上限，状态未改变。");
        candidate.food += actual;
        message << "采集队带回 " << actual << " 单位食物。";
    } else if (resource == "wood" || resource == "木材") {
        candidate.wood += 10;
        message << "伐木队从苍林带回10单位木材。";
    } else if (resource == "stone" || resource == "石料" || resource == "石头") {
        if (!found(candidate, LocationId::Quarry)) return rejected("尚未发现燧石矿场，不能开采石料。");
        candidate.stone += 8;
        message << "矿场小队开采了8单位石料。";
    } else if (resource == "herbs" || resource == "herb" || resource == "草药") {
        if (!found(candidate, LocationId::Marsh)) return rejected("尚未发现芦苇沼泽，不能采集草药。");
        const int gain = has(candidate, TechnologyId::HerbalKnowledge) ? 4 : 3;
        candidate.herbs += gain;
        message << "药师在沼泽采集了" << gain << "单位草药。";
    } else {
        return rejected("只能采集食物、木材、石料或草药。");
    }
    return commitAction(std::move(candidate), message.str());
}

ActionResult GameEngine::guardCamp() {
    ActionResult result;
    if (!canUseAction(result)) return result;
    GameState candidate = state_;
    candidate.temporaryDefense = std::min(12, candidate.temporaryDefense + 4);
    return commitAction(std::move(candidate), "一支小队加固警戒，本季临时防御提高4。整季最多提高到12。");
}

ActionResult GameEngine::celebrate() {
    ActionResult result;
    if (!canUseAction(result)) return result;
    if (state_.food < 3) return rejected("鼓舞族人需要3单位食物，状态未改变。");
    if (state_.morale >= 100) return rejected("士气已经达到上限，不需要再次鼓舞。");
    GameState candidate = state_;
    candidate.food -= 3;
    candidate.morale = clampMorale(candidate.morale + 8);
    return commitAction(std::move(candidate), "族人围绕燧火分享食物与故事，食物减少3、士气提高8。");
}

ActionResult GameEngine::scout(const std::string_view target) {
    ActionResult result;
    if (!canUseAction(result)) return result;
    const auto id = findLocation(target);
    if (!id) return rejected("没有这个地点。输入 map/地图 查看已知区域。");
    if (found(state_, *id)) return rejected("该地点已经发现，不需要重复侦察。");
    bool connected = false;
    for (const auto neighbor : location(*id).neighbors) {
        if (found(state_, neighbor)) {
            connected = true;
            break;
        }
    }
    if (!connected) return rejected("没有通往该地点的已知道路，请先探索相邻区域。");
    if (state_.food < 2) return rejected("侦察需要2单位食物作为口粮，状态未改变。");

    GameState candidate = state_;
    candidate.food -= 2;
    candidate.discovered[indexOf(*id)] = true;
    candidate.scouted[indexOf(*id)] = true;
    return commitAction(std::move(candidate), "侦察队发现了" + std::string(location(*id).chineseName)
        + "：" + std::string(location(*id).description));
}

ActionResult GameEngine::trainWarrior() {
    ActionResult result;
    if (!canUseAction(result)) return result;
    if (state_.food < 4) return rejected("训练战士需要4单位食物，状态未改变。");
    if (state_.warriors >= state_.population) return rejected("所有族人都已是战士，状态未改变。");
    GameState candidate = state_;
    candidate.food -= 4;
    ++candidate.warriors;
    return commitAction(std::move(candidate), "一名族人完成训练，战士增加1。人口总数不变。");
}

ActionResult GameEngine::build(const std::string_view target) {
    ActionResult result;
    if (!canUseAction(result)) return result;
    const auto id = findBuilding(target);
    if (!id) return rejected("未知建筑。输入 help/帮助 查看建筑名单。");
    const auto& definition = building(*id);
    if (state_.buildings[indexOf(*id)]) return rejected(std::string(definition.chineseName) + "已经建成，不能重复建造。");
    if (state_.wood < definition.woodCost || state_.stone < definition.stoneCost
        || state_.herbs < definition.herbCost) {
        std::ostringstream message;
        message << "建造" << definition.chineseName << "需要木材" << definition.woodCost
                << "、石料" << definition.stoneCost << "、草药" << definition.herbCost
                << "，资源不足，状态未改变。";
        return rejected(message.str());
    }
    GameState candidate = state_;
    candidate.wood -= definition.woodCost;
    candidate.stone -= definition.stoneCost;
    candidate.herbs -= definition.herbCost;
    candidate.buildings[indexOf(*id)] = true;
    return commitAction(std::move(candidate), std::string(definition.chineseName) + "建成。" + std::string(definition.effect));
}

ActionResult GameEngine::research(const std::string_view target) {
    ActionResult result;
    if (!canUseAction(result)) return result;
    const auto id = findTechnology(target);
    if (!id) return rejected("未知技术。输入 help/帮助 查看技术名单。");
    const auto& definition = technology(*id);
    if (state_.technologies[indexOf(*id)]) return rejected(std::string(definition.chineseName) + "已经掌握，不能重复研究。");
    if (definition.prerequisite && !has(state_, *definition.prerequisite)) {
        return rejected("必须先掌握" + std::string(technology(*definition.prerequisite).chineseName) + "。");
    }
    if (definition.tier >= 2 && !has(state_, BuildingId::Workshop)) return rejected("二级和三级技术需要先建造工坊。");

    int foodCost = 4;
    int woodCost = 4;
    int stoneCost = 0;
    if (definition.tier == 2) {
        foodCost = 6;
        woodCost = 0;
        stoneCost = 4;
    } else if (definition.tier == 3) {
        foodCost = 8;
        woodCost = 6;
        stoneCost = 6;
    }
    if (state_.food < foodCost || state_.wood < woodCost || state_.stone < stoneCost) {
        std::ostringstream message;
        message << "研究" << definition.chineseName << "需要食物" << foodCost
                << "、木材" << woodCost << "、石料" << stoneCost << "，状态未改变。";
        return rejected(message.str());
    }
    GameState candidate = state_;
    candidate.food -= foodCost;
    candidate.wood -= woodCost;
    candidate.stone -= stoneCost;
    candidate.technologies[indexOf(*id)] = true;
    return commitAction(std::move(candidate), "研究完成：" + std::string(definition.chineseName) + "。" + std::string(definition.effect));
}

ActionResult GameEngine::diplomacy(const std::string_view action, const std::string_view factionText) {
    ActionResult result;
    if (!canUseAction(result)) return result;
    const auto faction = findFaction(factionText);
    if (!faction) return rejected("未知部落。可选：河鹿、白羽、岩牙。");
    LocationId requiredLocation = LocationId::RiverFord;
    if (*faction == FactionId::WhiteFeather) requiredLocation = LocationId::WhiteFeatherCamp;
    if (*faction == FactionId::Rockfang) requiredLocation = LocationId::OldPass;
    if (!found(state_, requiredLocation)) return rejected("尚未发现与该部落接触所需的地点。");

    const std::size_t factionIndex = indexOf(*faction);
    GameState candidate = state_;
    const int councilBonus = has(candidate, BuildingId::CouncilFire) ? 5 : 0;
    const bool isTalk = action == "talk" || action == "交谈";
    const bool isGift = action == "gift" || action == "赠礼";
    const bool isQuest = action == "quest" || action == "协助" || action == "任务";

    if (isTalk) {
        const int languageBonus = has(candidate, TechnologyId::SharedLanguage) ? 5 : 0;
        candidate.relations[factionIndex] = clampRelation(candidate.relations[factionIndex] + 8 + languageBonus + councilBonus);
        if (candidate.quests[factionIndex] == 0) candidate.quests[factionIndex] = 1;
        return commitAction(std::move(candidate), "你与" + factionName(*faction)
            + "首领坦诚交谈，对方提出了第一项协助任务。关系有所提高。");
    }
    if (isGift) {
        const int cost = has(candidate, TechnologyId::GiftCustoms) ? 2 : 4;
        if (candidate.food < cost) return rejected("赠礼需要" + std::to_string(cost) + "单位食物，状态未改变。");
        candidate.food -= cost;
        candidate.relations[factionIndex] = clampRelation(candidate.relations[factionIndex] + 15 + councilBonus);
        return commitAction(std::move(candidate), "赠礼被" + factionName(*faction) + "接受，关系提高。");
    }
    if (!isQuest) return rejected("外交动作只能是交谈、赠礼或协助。");
    if (candidate.quests[factionIndex] == 0) return rejected("请先与该部落首领交谈，了解他们需要什么。");
    if (candidate.quests[factionIndex] >= 3) return rejected("该部落的两阶段任务已经全部完成。");

    std::string message;
    int relationGain = 20 + councilBonus;
    if (*faction == FactionId::RiverDeer) {
        if (candidate.quests[factionIndex] == 1) {
            if (candidate.food < 8) return rejected("河鹿的第一项任务需要援助8单位食物。");
            candidate.food -= 8;
            message = "你帮助河鹿渡过歉收，他们邀请燧火共同守护渡口。";
        } else {
            if (permanentDefense() < 6 && candidate.warriors < 5) return rejected("守护渡口需要至少6点永久防御或5名战士。");
            relationGain = 25 + councilBonus;
            message = "燧火守住河鹿渡口，两族的盟誓已经成熟。";
        }
    } else if (*faction == FactionId::WhiteFeather) {
        if (candidate.quests[factionIndex] == 1) {
            if (candidate.food < 6) return rejected("白羽的第一项任务需要6单位食物救助伤员。");
            candidate.food -= 6;
            message = "白羽伤员得到食物，药师愿意分享更深的知识。";
        } else {
            if (!has(candidate, BuildingId::HealerHut) || !has(candidate, TechnologyId::HerbalKnowledge)) {
                return rejected("白羽的第二项任务需要医者小屋和草药知识。");
            }
            relationGain = 25 + councilBonus;
            message = "燧火与白羽共同建立救治约定，两族已彼此信任。";
        }
    } else {
        if (candidate.quests[factionIndex] == 1) {
            if (candidate.warriors < 5 || !candidate.scouted[indexOf(LocationId::OldPass)]) {
                return rejected("岩牙的第一次会面要求至少5名战士，并完成古老山隘侦察。");
            }
            relationGain = 25 + councilBonus;
            message = "你的实力获得岩牙尊重，对方愿意讨论停战条件。";
        } else {
            if (candidate.relations[factionIndex] < 0 || candidate.food < 6) {
                return rejected("停战需要岩牙关系不低于0，并提供6单位食物举行盟餐。");
            }
            candidate.food -= 6;
            candidate.rockfangTruce = true;
            relationGain = 25 + councilBonus;
            message = "燧火与岩牙在山隘盟餐，双方正式停战。";
        }
    }
    ++candidate.quests[factionIndex];
    candidate.relations[factionIndex] = clampRelation(candidate.relations[factionIndex] + relationGain);
    return commitAction(std::move(candidate), message);
}

ActionResult GameEngine::attack(const Tactic tactic, const bool raid) {
    if (!raid) {
        ActionResult result;
        if (!canUseAction(result)) return result;
        if (!found(state_, LocationId::RockfangFort)) return rejected("尚未发现岩牙要塞，不能发动进攻。");
        if (tactic == Tactic::Defend) return rejected("主动进攻不能选择防守；防守战术用于岩牙来袭。");
    }
    if (state_.rockfangFortCaptured || state_.rockfangStrength <= 0) return rejected("岩牙要塞已经被攻下，无需继续战斗。");
    if (state_.warriors <= 0) return rejected("没有战士可以参战，状态未改变。");
    if (tactic == Tactic::Retreat && state_.food < 3) return rejected("撤退需要3单位食物维持队形，状态未改变。");

    BattleContext context;
    context.isRaid = raid;
    context.battlefieldScouted = state_.scouted[indexOf(raid ? LocationId::OldPass : LocationId::RockfangFort)];
    context.hasFlintSpear = has(state_, TechnologyId::FlintSpear);
    context.hasShieldWall = has(state_, TechnologyId::ShieldWall);
    context.hasAmbushTraining = has(state_, TechnologyId::AmbushTraining);
    context.warriors = state_.warriors;
    context.morale = state_.morale;
    context.defense = raid ? permanentDefense() + state_.temporaryDefense : 0;
    context.enemyStrength = state_.rockfangStrength;
    const BattleResult battle = battles_.resolve(context, tactic);

    GameState candidate = state_;
    std::string message = battle.message;
    if (battle.retreated) {
        candidate.food -= 3;
        candidate.morale = clampMorale(candidate.morale + battle.moraleDelta);
    } else {
        candidate.warriors = std::max(0, candidate.warriors - battle.casualties);
        candidate.population = std::max(0, candidate.population - battle.casualties);
        candidate.morale = clampMorale(candidate.morale + battle.moraleDelta);
        candidate.campDurability = std::max(0, candidate.campDurability - battle.campDamage);
        candidate.rockfangStrength = std::max(0, candidate.rockfangStrength - battle.enemyDamage);
        if (!raid && battle.victory && candidate.rockfangStrength == 0 && tactic != Tactic::Defend) {
            candidate.rockfangFortCaptured = true;
            candidate.rockfangTruce = false;
            message += " 岩牙要塞被燧火占领。";
        }
    }
    if (raid) {
        candidate.pendingRaid = false;
        candidate.phase = Phase::Playing;
        finishExtinctionIfNeeded(candidate, message);
        state_ = std::move(candidate);
        return {true, true, false, false, std::move(message)};
    }
    if (candidate.rockfangTruce) {
        candidate.rockfangTruce = false;
        candidate.relations[indexOf(FactionId::Rockfang)] = clampRelation(candidate.relations[indexOf(FactionId::Rockfang)] - 40);
        message += " 你撕毁了停战约定，岩牙关系大幅下降。";
    }
    return commitAction(std::move(candidate), std::move(message));
}

ActionResult GameEngine::endSeason() {
    GameState candidate = state_;
    std::ostringstream message;
    int foodCost = (candidate.population + 2) / 3;
    if (seasonForTurn(candidate.turn) == Season::Winter) foodCost += 2;
    if (seasonForTurn(candidate.turn) == Season::Winter && has(candidate, BuildingId::Granary)) {
        foodCost = std::max(0, foodCost - 2);
    }
    message << "第" << yearForTurn(candidate.turn) << "年" << seasonName(seasonForTurn(candidate.turn))
            << "季结算：需要" << foodCost << "单位食物。";
    if (candidate.food >= foodCost) {
        candidate.food -= foodCost;
        message << "储备足够。";
    } else {
        candidate.food = 0;
        candidate.population = std::max(0, candidate.population - 1);
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        candidate.morale = clampMorale(candidate.morale - 10);
        message << "食物不足，人口减少1、士气下降10。";
    }
    if (candidate.morale == 0 && candidate.population > 0) {
        candidate.population = std::max(0, candidate.population - 1);
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        message << " 士气崩溃，又有1名族人离开。";
    }
    if (seasonForTurn(candidate.turn) == Season::Spring && candidate.population > 0
        && candidate.food >= 15 && candidate.morale >= 65) {
        candidate.population += 2;
        candidate.morale = clampMorale(candidate.morale + 2);
        message << " 春季储备充足，人口增加2、士气提高2。";
    }
    std::string settlementMessage = message.str();
    finishExtinctionIfNeeded(candidate, settlementMessage);
    if (candidate.phase == Phase::Finished) {
        state_ = std::move(candidate);
        return {true, true, false, true, settlementMessage + "\n结局：部落覆灭。"};
    }

    if (candidate.turn == static_cast<int>(kSeasonCount)) {
        candidate.actionsLeft = 0;
        candidate.phase = Phase::FinalChoice;
        state_ = std::move(candidate);
        return {true, true, false, true, settlementMessage
            + "\n四年已经结束。输入 objectives 查看道路，再用 choose/选择 决定结局。"};
    }

    ++candidate.turn;
    candidate.temporaryDefense = 0;
    candidate.actionsLeft = teamsForPopulation(candidate.population);
    candidate.phase = Phase::Playing;
    candidate.currentEvent = candidate.eventSchedule.at(static_cast<std::size_t>(candidate.turn - 1));
    std::string eventMessage;
    applyEvent(candidate, static_cast<EventId>(candidate.currentEvent), eventMessage);
    finishExtinctionIfNeeded(candidate, eventMessage);
    candidate.actionsLeft = std::min(candidate.actionsLeft, teamsForPopulation(candidate.population));
    state_ = std::move(candidate);
    return {true, true, false, true, settlementMessage + "\n" + eventMessage};
}

ActionResult GameEngine::chooseEnding(const std::string_view target) {
    const auto ending = findEnding(target);
    if (!ending || *ending == Ending::None || *ending == Ending::Extinction) return rejected("未知结局道路。可选：联盟、征服、繁荣、迁徙。");
    const auto available = availableEndings();
    if (std::find(available.begin(), available.end(), *ending) == available.end()) {
        return rejected("当前条件尚未解锁" + endingName(*ending) + "。输入 objectives 查看差距。");
    }
    GameState candidate = state_;
    candidate.phase = Phase::Finished;
    candidate.ending = *ending;
    state_ = std::move(candidate);
    return {true, true, false, false, "族人围绕燧火作出共同决定。结局：" + endingName(*ending) + "。"};
}

int GameEngine::permanentDefense() const {
    int defense = 0;
    if (has(state_, BuildingId::Wall)) defense += 6;
    if (has(state_, BuildingId::Watchtower)) defense += 3;
    return defense;
}

int GameEngine::buildingCount() const {
    return static_cast<int>(std::count(state_.buildings.begin(), state_.buildings.end(), true));
}

int GameEngine::technologyCount() const {
    return static_cast<int>(std::count(state_.technologies.begin(), state_.technologies.end(), true));
}

std::vector<Ending> GameEngine::availableEndings() const {
    std::vector<Ending> result;
    if (endingAvailableForState(state_, Ending::Alliance)) result.push_back(Ending::Alliance);
    if (endingAvailableForState(state_, Ending::Conquest)) result.push_back(Ending::Conquest);
    if (endingAvailableForState(state_, Ending::Prosperity)) result.push_back(Ending::Prosperity);
    if (endingAvailableForState(state_, Ending::Migration)) result.push_back(Ending::Migration);
    return result;
}

void GameEngine::applyEvent(GameState& candidate, const EventId eventId, std::string& message) const {
    const auto& definition = event(eventId);
    std::ostringstream output;
    output << "季节事件【" << definition.title << "】：" << definition.description;
    switch (eventId) {
    case EventId::GentleSpring:
        candidate.morale = clampMorale(candidate.morale + 3);
        output << " 士气提高3。";
        break;
    case EventId::RichHunt:
        candidate.food = std::min(has(candidate, BuildingId::Granary) ? 100 : 60, candidate.food + 6);
        output << " 食物增加6。";
        break;
    case EventId::Drought: {
        const int loss = has(candidate, TechnologyId::Irrigation) ? 2 : 5;
        candidate.food = std::max(0, candidate.food - loss);
        output << " 食物减少" << loss << "。";
        break;
    }
    case EventId::Flood: {
        const int campLoss = has(candidate, BuildingId::Wall) ? 0 : 2;
        candidate.wood = std::max(0, candidate.wood - 4);
        candidate.campDurability = std::max(0, candidate.campDurability - campLoss);
        output << " 木材减少4" << (campLoss > 0 ? "、营地耐久减少2。" : "，木墙挡住了洪水。") ;
        break;
    }
    case EventId::Sickness:
        if (has(candidate, BuildingId::HealerHut) && candidate.herbs > 0) {
            --candidate.herbs;
            output << " 医者消耗1草药控制了热病。";
        } else {
            candidate.population = std::max(0, candidate.population - 1);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            output << " 人口减少1。";
        }
        break;
    case EventId::Refugees:
        if (candidate.food >= 8) {
            candidate.food -= 4;
            ++candidate.population;
            candidate.morale = clampMorale(candidate.morale + 2);
            output << " 族人分享4食物，人口增加1、士气提高2。";
        } else {
            candidate.morale = clampMorale(candidate.morale - 3);
            output << " 储备不足，只能拒绝他们，士气下降3。";
        }
        break;
    case EventId::Traders:
        candidate.wood += 3;
        candidate.stone += 2;
        output << " 木材增加3、石料增加2。";
        break;
    case EventId::ForestFire: {
        const int woodLoss = has(candidate, BuildingId::Watchtower) ? 2 : 5;
        const int campLoss = has(candidate, BuildingId::Watchtower) ? 0 : 2;
        candidate.wood = std::max(0, candidate.wood - woodLoss);
        candidate.campDurability = std::max(0, candidate.campDurability - campLoss);
        output << " 木材减少" << woodLoss;
        if (campLoss > 0) output << "、营地耐久减少2";
        output << "。";
        break;
    }
    case EventId::Predators:
        candidate.food = std::max(0, candidate.food - 3);
        output << " 食物减少3。";
        break;
    case EventId::Dispute: {
        const int loss = has(candidate, BuildingId::CouncilFire) ? 2 : 5;
        candidate.morale = clampMorale(candidate.morale - loss);
        output << " 士气下降" << loss << "。";
        break;
    }
    case EventId::HerbBloom:
        candidate.herbs += 3;
        output << " 草药增加3。";
        break;
    case EventId::ColdSnap: {
        const int loss = has(candidate, BuildingId::Granary) ? 1 : 4;
        candidate.food = std::max(0, candidate.food - loss);
        output << " 食物减少" << loss << "。";
        break;
    }
    case EventId::Craftspeople:
        candidate.wood += 2;
        output << " 木材增加2。";
        break;
    case EventId::RockfangScouts:
        if (has(candidate, BuildingId::Watchtower)) output << " 瞭望塔及时发现并驱离了斥候。";
        else {
            candidate.rockfangStrength = std::min(30, candidate.rockfangStrength + 1);
            output << " 岩牙掌握了道路，敌方战力提高1。";
        }
        break;
    case EventId::Newborns:
        if (candidate.food >= 10) {
            ++candidate.population;
            output << " 人口增加1。";
        } else output << " 食物紧张，人口没有增加。";
        break;
    case EventId::Festival:
        if (candidate.food >= 3) {
            candidate.food -= 3;
            candidate.morale = clampMorale(candidate.morale + 8);
            output << " 食物减少3、士气提高8。";
        } else output << " 储备不足，庆典被推迟。";
        break;
    case EventId::LostHunters:
        if (has(candidate, BuildingId::HealerHut)) output << " 搜救者将猎手安全带回。";
        else {
            candidate.population = std::max(0, candidate.population - 1);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            output << " 人口减少1。";
        }
        break;
    case EventId::ClearSky:
        candidate.morale = clampMorale(candidate.morale + 2);
        output << " 士气提高2。";
        break;
    case EventId::RiverEnvoys:
        candidate.relations[indexOf(FactionId::RiverDeer)] = clampRelation(candidate.relations[indexOf(FactionId::RiverDeer)] + 5);
        output << " 河鹿关系提高5。";
        break;
    case EventId::WhiteFeatherSign:
        candidate.relations[indexOf(FactionId::WhiteFeather)] = clampRelation(candidate.relations[indexOf(FactionId::WhiteFeather)] + 5);
        output << " 白羽关系提高5。";
        break;
    case EventId::RockfangRaid:
        if (candidate.rockfangTruce || candidate.rockfangFortCaptured || candidate.rockfangStrength <= 0) {
            output << " 由于停战或岩牙已经败亡，袭击没有发生。";
        } else {
            candidate.pendingRaid = true;
            candidate.phase = Phase::AwaitingRaid;
            output << " 必须输入 应战 正面|伏击|防守|撤退。";
        }
        break;
    case EventId::FinalCouncil:
        output << " 这是最后一个可以行动的季节。";
        break;
    case EventId::Count:
        break;
    }
    candidate.actionsLeft = std::min(candidate.actionsLeft, teamsForPopulation(candidate.population));
    message = output.str();
}

void GameEngine::finishExtinctionIfNeeded(GameState& candidate, std::string& message) const {
    if (candidate.population > 0 && candidate.campDurability > 0) return;
    candidate.population = std::max(0, candidate.population);
    candidate.campDurability = std::max(0, candidate.campDurability);
    candidate.warriors = std::min(candidate.warriors, candidate.population);
    candidate.phase = Phase::Finished;
    candidate.ending = Ending::Extinction;
    candidate.pendingRaid = false;
    if (!message.empty()) message += " ";
    message += "燧火部落失去了最后的生存基础。";
}

std::string GameEngine::statusText() const {
    std::ostringstream output;
    output << modeName(state_.mode) << "  第" << yearForTurn(state_.turn) << "年"
           << seasonName(seasonForTurn(state_.turn)) << "季（" << state_.turn << "/16）\n"
           << "小队：" << state_.actionsLeft << "/" << teamsForPopulation(state_.population)
           << "  人口：" << state_.population << "  战士：" << state_.warriors << "  士气：" << state_.morale << "\n"
           << "食物：" << state_.food << "/" << (has(state_, BuildingId::Granary) ? 100 : 60)
           << "  木材：" << state_.wood << "  石料：" << state_.stone << "  草药：" << state_.herbs << "\n"
           << "营地耐久：" << state_.campDurability << "/20  永久防御：" << permanentDefense()
           << "  本季临时防御：" << state_.temporaryDefense << "\n"
           << "河鹿关系：" << state_.relations[indexOf(FactionId::RiverDeer)]
           << "  白羽关系：" << state_.relations[indexOf(FactionId::WhiteFeather)]
           << "  岩牙关系：" << state_.relations[indexOf(FactionId::Rockfang)]
           << "  岩牙战力：" << state_.rockfangStrength << "\n"
           << "建筑：" << buildingCount() << "/6  技术：" << technologyCount() << "/9  阶段：" << phaseName(state_.phase);
    if (state_.ending != Ending::None) output << "  结局：" << endingName(state_.ending);
    return output.str();
}

std::string GameEngine::mapText() const {
    std::ostringstream output;
    output << "战略地图（???? 表示尚未发现）\n"
           << "[" << discoveredName(state_, LocationId::WhiteFeatherCamp) << "]——["
           << discoveredName(state_, LocationId::Marsh) << "]——[" << discoveredName(state_, LocationId::Forest) << "]\n"
           << "       |                         |\n"
           << "[" << discoveredName(state_, LocationId::RiverFord) << "]——[" << discoveredName(state_, LocationId::RedPlain)
           << "]——[" << discoveredName(state_, LocationId::Camp) << "]\n"
           << "       |              |\n"
           << "[" << discoveredName(state_, LocationId::OldPass) << "]——[" << discoveredName(state_, LocationId::Quarry) << "]\n"
           << "       |\n"
           << "[" << discoveredName(state_, LocationId::RockfangFort) << "]\n";
    output << "已发现：";
    bool first = true;
    for (const auto& item : locations()) {
        if (!found(state_, item.id)) continue;
        if (!first) output << "、";
        output << item.chineseName;
        first = false;
    }
    return output.str();
}

std::string GameEngine::objectivesText() const {
    const bool riverReady = state_.quests[indexOf(FactionId::RiverDeer)] >= 3
        && state_.relations[indexOf(FactionId::RiverDeer)] >= 70;
    const bool whiteReady = state_.quests[indexOf(FactionId::WhiteFeather)] >= 3
        && state_.relations[indexOf(FactionId::WhiteFeather)] >= 70;
    std::ostringstream output;
    output << "结局道路：\n"
           << "  联盟共主：" << checkbox(riverReady) << "河鹿 " << checkbox(whiteReady) << "白羽 "
           << checkbox(has(state_, TechnologyId::Confederation)) << "部落联盟 "
           << checkbox(state_.rockfangTruce || state_.rockfangFortCaptured) << "岩牙停战/败亡\n"
           << "  山河征服者：" << checkbox(state_.rockfangFortCaptured) << "攻下要塞 "
           << checkbox(state_.warriors >= 6) << "战士>=6 " << checkbox(state_.morale >= 45) << "士气>=45\n"
           << "  燧火繁荣：" << checkbox(state_.population >= 20) << "人口>=20 "
           << checkbox(state_.food >= 40) << "食物>=40 " << checkbox(buildingCount() >= 4) << "建筑>=4 "
           << checkbox(technologyCount() >= 4) << "技术>=4\n"
           << "  迁徙新生：只要人口和营地仍然存在，最终始终可以选择。\n"
           << "任务阶段：河鹿 " << state_.quests[indexOf(FactionId::RiverDeer)] << "/3，白羽 "
           << state_.quests[indexOf(FactionId::WhiteFeather)] << "/3，岩牙 "
           << state_.quests[indexOf(FactionId::Rockfang)] << "/3。";
    if (state_.phase == Phase::FinalChoice) {
        output << "\n当前可选：";
        for (const auto ending : availableEndings()) output << ' ' << endingName(ending);
    }
    return output.str();
}

std::string GameEngine::helpText() const {
    return
        "常用命令（数字 / English / 中文）：\n"
        "  1 status 状态          2 map 地图          objectives 目标\n"
        "  3 gather food 采集 食物    4 gather wood 采集 木材\n"
        "  5 gather stone 采集 石料   6 gather herbs 采集 草药\n"
        "  7 train 训练    10 celebrate 鼓舞    guard 守卫    8 endturn 结束回合\n"
        "探索：11沼泽 12河鹿渡口 13白羽营地 14矿场 15山隘 16岩牙要塞；或 scout/侦察 <地点>\n"
        "建筑：21粮仓 22木墙 23工坊 24医者小屋 25瞭望塔 26议事火坛；或 build/建造 <建筑>\n"
        "技术：31食物保存 32草药知识 33引水 34长矛 35盾墙 36伏击 37赠礼 38语言 39联盟\n"
        "外交：41-43交谈河鹿/白羽/岩牙，44-46赠礼，47-49协助；或 talk/gift/quest <部落>\n"
        "战斗：51正面 52伏击 53防守 54撤退；或 attack rockfang <tactic> / 应战 <战术>\n"
        "结局：71联盟 72征服 73繁荣 74迁徙；或 choose/选择 <结局>\n"
        "公共：help 帮助，save 保存，load 读取，back 返回，quit 退出。";
}

bool GameEngine::validateState(const GameState& candidate, std::string& error) {
    const int modeValue = static_cast<int>(candidate.mode);
    const int phaseValue = static_cast<int>(candidate.phase);
    const int endingValue = static_cast<int>(candidate.ending);
    if (modeValue < static_cast<int>(GameMode::Standard) || modeValue > static_cast<int>(GameMode::Quick)
        || phaseValue < static_cast<int>(Phase::Playing) || phaseValue > static_cast<int>(Phase::Finished)
        || endingValue < static_cast<int>(Ending::None) || endingValue > static_cast<int>(Ending::Extinction)) {
        error = "存档中的枚举编号超出范围。";
        return false;
    }
    const int minimumTurn = candidate.mode == GameMode::Quick ? 9 : 1;
    if (candidate.turn < minimumTurn || candidate.turn > static_cast<int>(kSeasonCount)
        || candidate.actionsLeft < 0 || candidate.actionsLeft > 3
        || candidate.population < 0 || candidate.population > 100
        || candidate.food < 0 || candidate.food > (candidate.buildings[indexOf(BuildingId::Granary)] ? 100 : 60)
        || candidate.wood < 0 || candidate.wood > 999
        || candidate.stone < 0 || candidate.stone > 999
        || candidate.herbs < 0 || candidate.herbs > 999
        || candidate.warriors < 0 || candidate.warriors > candidate.population
        || candidate.morale < 0 || candidate.morale > 100
        || candidate.campDurability < 0 || candidate.campDurability > 20
        || candidate.temporaryDefense < 0 || candidate.temporaryDefense > 12
        || candidate.rockfangStrength < 0 || candidate.rockfangStrength > 30) {
        error = "存档中的基础数值超出允许范围。";
        return false;
    }
    if (!candidate.discovered[indexOf(LocationId::Camp)]
        || !candidate.discovered[indexOf(LocationId::Forest)]
        || !candidate.discovered[indexOf(LocationId::RedPlain)]) {
        error = "初始三个地点必须保持已发现。";
        return false;
    }
    for (std::size_t index = 0; index < kLocationCount; ++index) {
        if (candidate.scouted[index] && !candidate.discovered[index]) {
            error = "不能侦察尚未发现的地点。";
            return false;
        }
    }
    std::array<bool, kLocationCount> reachable{};
    std::vector<LocationId> frontier{LocationId::Camp};
    reachable[indexOf(LocationId::Camp)] = true;
    for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
        for (const LocationId neighbor : location(frontier[cursor]).neighbors) {
            if (candidate.discovered[indexOf(neighbor)] && !reachable[indexOf(neighbor)]) {
                reachable[indexOf(neighbor)] = true;
                frontier.push_back(neighbor);
            }
        }
    }
    for (std::size_t index = 0; index < kLocationCount; ++index) {
        if (candidate.discovered[index] && !reachable[index]) {
            error = "已发现地点没有连接到燧火营地。";
            return false;
        }
    }
    for (const auto& definition : technologies()) {
        if (!candidate.technologies[indexOf(definition.id)]) continue;
        if (definition.prerequisite && !candidate.technologies[indexOf(*definition.prerequisite)]) {
            error = "技术状态缺少前置技术。";
            return false;
        }
        if (definition.tier >= 2 && !candidate.buildings[indexOf(BuildingId::Workshop)]) {
            error = "高级技术状态缺少工坊。";
            return false;
        }
    }
    for (const int relation : candidate.relations) {
        if (relation < -100 || relation > 100) {
            error = "部落关系值超出范围。";
            return false;
        }
    }
    for (const int quest : candidate.quests) {
        if (quest < 0 || quest > 3) {
            error = "外交任务阶段超出范围。";
            return false;
        }
    }
    if (candidate.eventSchedule != eventScheduleForSeed(candidate.seed)) {
        error = "事件表与存档种子不一致。";
        return false;
    }
    if (candidate.currentEvent != candidate.eventSchedule.at(static_cast<std::size_t>(candidate.turn - 1))
        || candidate.currentEvent < 0 || candidate.currentEvent >= static_cast<int>(EventId::Count)) {
        error = "当前事件与季节不一致。";
        return false;
    }
    if (candidate.rockfangFortCaptured && candidate.rockfangStrength != 0) {
        error = "要塞占领状态与岩牙战力不一致。";
        return false;
    }
    if (candidate.rockfangFortCaptured
        && (!candidate.discovered[indexOf(LocationId::RockfangFort)]
            || !candidate.scouted[indexOf(LocationId::RockfangFort)])) {
        error = "占领岩牙要塞前必须发现并侦察该地点。";
        return false;
    }
    if (candidate.rockfangTruce
        && (candidate.quests[indexOf(FactionId::Rockfang)] < 3
            || candidate.relations[indexOf(FactionId::Rockfang)] < 0)) {
        error = "岩牙停战状态与外交任务不一致。";
        return false;
    }
    if ((candidate.phase == Phase::AwaitingRaid) != candidate.pendingRaid) {
        error = "敌袭状态与游戏阶段不一致。";
        return false;
    }
    if (candidate.pendingRaid
        && (candidate.turn != 12 || candidate.currentEvent != static_cast<int>(EventId::RockfangRaid)
            || candidate.rockfangTruce || candidate.rockfangFortCaptured)) {
        error = "敌袭只能发生在第三年冬季且不能与停战状态并存。";
        return false;
    }
    if (candidate.phase == Phase::FinalChoice && candidate.turn != 16) {
        error = "结局议事只能发生在最后一季。";
        return false;
    }
    if (candidate.phase == Phase::FinalChoice && candidate.actionsLeft != 0) {
        error = "结局议事阶段不能保留行动小队。";
        return false;
    }
    if (candidate.phase == Phase::Finished) {
        if (candidate.ending == Ending::None) {
            error = "已经结束的游戏必须记录结局。";
            return false;
        }
        if (candidate.ending != Ending::Extinction && candidate.turn != 16) {
            error = "成功结局只能在第四年结束后产生。";
            return false;
        }
        if (!endingAvailableForState(candidate, candidate.ending)) {
            error = "结局与实际达成条件不一致。";
            return false;
        }
    } else if (candidate.ending != Ending::None || candidate.population <= 0 || candidate.campDurability <= 0) {
        error = "进行中的游戏状态与结局不一致。";
        return false;
    }
    error.clear();
    return true;
}

} // namespace tribe
