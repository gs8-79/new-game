#include "tribe/campaign.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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
    command.verb = asciiLower(std::move(command.verb));
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

template <typename Enum>
bool enumInRange(const Enum value, const Enum first, const Enum last) {
    const int raw = static_cast<int>(value);
    return raw >= static_cast<int>(first) && raw <= static_cast<int>(last);
}

bool parseNonnegative(const std::string_view text, int& value) {
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < 0) return false;
    value = parsed;
    return true;
}

std::optional<ResourceKind> parseResource(const std::string_view text) {
    if (equalsAny(text, {"food", "食物", "粮食"})) return ResourceKind::Food;
    if (equalsAny(text, {"wood", "木材"})) return ResourceKind::Wood;
    if (equalsAny(text, {"stone", "石料", "石头"})) return ResourceKind::Stone;
    if (equalsAny(text, {"herbs", "herb", "草药"})) return ResourceKind::Herbs;
    if (equalsAny(text, {"shells", "shell", "贝币", "货币"})) return ResourceKind::Shells;
    return std::nullopt;
}

std::optional<TribeIdV2> parseTribe(const std::string_view text) {
    if (equalsAny(text, {"river", "riverdeer", "河鹿"})) return TribeIdV2::RiverDeer;
    if (equalsAny(text, {"white", "whitefeather", "白羽"})) return TribeIdV2::WhiteFeather;
    if (equalsAny(text, {"rock", "rockfang", "岩牙"})) return TribeIdV2::Rockfang;
    if (equalsAny(text, {"tide", "tidesalt", "潮盐"})) return TribeIdV2::Tidesalt;
    if (equalsAny(text, {"black", "blackstone", "玄石"})) return TribeIdV2::Blackstone;
    return std::nullopt;
}

std::optional<WorldLocationId> parseLocation(const std::string_view text) {
    static const std::array<std::vector<std::string_view>, kWorldLocationCount> aliases{{
        {"camp", "营地", "燧火营地"},
        {"forest", "苍林"},
        {"plain", "redplain", "红土原"},
        {"marsh", "沼泽", "芦苇沼泽"},
        {"ford", "riverford", "渡口", "河鹿渡口"},
        {"whitecamp", "白羽营地"},
        {"quarry", "矿场", "燧石矿场"},
        {"pass", "oldpass", "山隘", "古老山隘"},
        {"fort", "rockfort", "岩牙要塞"},
        {"coast", "saltwind", "盐风海岸"},
        {"harbor", "tidesaltharbor", "潮盐港"},
        {"beach", "shellbeach", "贝壳滩"},
        {"valley", "blackstonevalley", "玄石谷"},
        {"workshop", "blackstoneworkshop", "玄石工坊"},
        {"market", "mountainmarket", "山前集市"},
        {"road", "cliffroad", "断崖商道"},
    }};
    int numeric = 0;
    if (parseNonnegative(text, numeric) && numeric >= 1 && numeric <= static_cast<int>(kWorldLocationCount)) {
        return static_cast<WorldLocationId>(numeric - 1);
    }
    for (std::size_t index = 0; index < aliases.size(); ++index) {
        if (std::find(aliases[index].begin(), aliases[index].end(), text) != aliases[index].end()) {
            return static_cast<WorldLocationId>(index);
        }
    }
    return std::nullopt;
}

std::optional<BuildingId> parseBuilding(const std::string_view text) {
    if (equalsAny(text, {"granary", "粮仓"})) return BuildingId::Granary;
    if (equalsAny(text, {"wall", "木墙"})) return BuildingId::Wall;
    if (equalsAny(text, {"workshop", "工坊"})) return BuildingId::Workshop;
    if (equalsAny(text, {"healer", "医者小屋"})) return BuildingId::HealerHut;
    if (equalsAny(text, {"tower", "瞭望塔"})) return BuildingId::Watchtower;
    if (equalsAny(text, {"council", "fire", "议事火坛"})) return BuildingId::CouncilFire;
    return std::nullopt;
}

std::optional<TechnologyId> parseTechnology(const std::string_view text) {
    if (equalsAny(text, {"preservation", "食物保存"})) return TechnologyId::FoodPreservation;
    if (equalsAny(text, {"herbal", "草药知识"})) return TechnologyId::HerbalKnowledge;
    if (equalsAny(text, {"irrigation", "引水耕作"})) return TechnologyId::Irrigation;
    if (equalsAny(text, {"spear", "燧石长矛"})) return TechnologyId::FlintSpear;
    if (equalsAny(text, {"shield", "盾墙阵形"})) return TechnologyId::ShieldWall;
    if (equalsAny(text, {"ambush", "伏击训练"})) return TechnologyId::AmbushTraining;
    if (equalsAny(text, {"gift", "赠礼习俗"})) return TechnologyId::GiftCustoms;
    if (equalsAny(text, {"language", "共同语言"})) return TechnologyId::SharedLanguage;
    if (equalsAny(text, {"confederation", "部落联盟"})) return TechnologyId::Confederation;
    return std::nullopt;
}

std::optional<ResidentMission> parseResidentMission(const std::string_view text) {
    if (equalsAny(text, {"none", "停止", "无"})) return ResidentMission::None;
    if (equalsAny(text, {"gather", "采集"})) return ResidentMission::Gather;
    if (equalsAny(text, {"patrol", "巡逻"})) return ResidentMission::Patrol;
    if (equalsAny(text, {"explore", "侦察"})) return ResidentMission::Explore;
    if (equalsAny(text, {"escort", "护送"})) return ResidentMission::Escort;
    if (equalsAny(text, {"train", "训练"})) return ResidentMission::Train;
    return std::nullopt;
}

std::optional<WarOrder> parseWarOrder(const std::string_view text) {
    if (equalsAny(text, {"advance", "推进"})) return WarOrder::Advance;
    if (equalsAny(text, {"hold", "坚守"})) return WarOrder::Hold;
    if (equalsAny(text, {"focus", "集火"})) return WarOrder::Focus;
    if (equalsAny(text, {"flank", "包抄"})) return WarOrder::Flank;
    if (equalsAny(text, {"cover", "掩护"})) return WarOrder::Cover;
    if (equalsAny(text, {"retreat", "撤退"})) return WarOrder::Retreat;
    return std::nullopt;
}

std::optional<CampaignEnding> parseCampaignEnding(const std::string_view text) {
    if (equalsAny(text, {"alliance", "联盟", "联盟共主"})) return CampaignEnding::Alliance;
    if (equalsAny(text, {"conquest", "征服", "山河征服者"})) return CampaignEnding::Conquest;
    if (equalsAny(text, {"prosperity", "繁荣", "燧火繁荣"})) return CampaignEnding::Prosperity;
    if (equalsAny(text, {"migration", "迁徙", "迁徙新生"})) return CampaignEnding::Migration;
    return std::nullopt;
}

Character makeCampaignCharacter(const std::string& name, const Occupation occupation) {
    Character character{name, occupation};
    character.attributes = Attributes{5};
    character.loyalty = 65;
    switch (occupation) {
    case Occupation::Hunter:
        character.attributes[Attribute::Survival] = 8;
        character.attributes[Attribute::Perception] = 7;
        break;
    case Occupation::Warrior:
        character.attributes[Attribute::Strength] = 8;
        character.attributes[Attribute::Endurance] = 7;
        break;
    case Occupation::Scout:
        character.attributes[Attribute::Agility] = 8;
        character.attributes[Attribute::Perception] = 8;
        break;
    case Occupation::Healer:
        character.attributes[Attribute::Survival] = 7;
        character.attributes[Attribute::Willpower] = 8;
        break;
    case Occupation::Crafter:
        character.attributes[Attribute::Endurance] = 7;
        character.attributes[Attribute::Perception] = 7;
        break;
    case Occupation::Envoy:
        character.attributes[Attribute::Diplomacy] = 8;
        character.attributes[Attribute::Leadership] = 8;
        break;
    }
    character.life = maximumLife(character);
    return character;
}

int relationClamp(const int value) { return std::clamp(value, -100, 100); }
int percentClamp(const int value) { return std::clamp(value, 0, 100); }

std::string warOrderName(const WarOrder order) {
    switch (order) {
    case WarOrder::Advance: return "推进";
    case WarOrder::Hold: return "坚守";
    case WarOrder::Focus: return "集火";
    case WarOrder::Flank: return "包抄";
    case WarOrder::Cover: return "掩护";
    case WarOrder::Retreat: return "全军撤退";
    }
    return "未知";
}

std::string crisisName(const FactionCrisis crisis) {
    switch (crisis) {
    case FactionCrisis::Calm: return "平稳";
    case FactionCrisis::Complaint: return "抱怨";
    case FactionCrisis::Slowdown: return "减产";
    case FactionCrisis::Refusal: return "拒绝出队";
    case FactionCrisis::Deposition: return "要求罢免";
    case FactionCrisis::Coup: return "政变";
    }
    return "未知";
}

template <typename Container>
int countTrue(const Container& values) {
    return static_cast<int>(std::count(values.begin(), values.end(), true));
}

} // namespace

const std::array<WorldLocationInfo, kWorldLocationCount>& CampaignGame::worldLocations() {
    static const std::array<WorldLocationInfo, kWorldLocationCount> locations{{
        {WorldLocationId::Camp, "燧火营地", "部落管理、建设与休整", {WorldLocationId::Forest, WorldLocationId::RedPlain}},
        {WorldLocationId::Forest, "苍林", "狩猎、木材和草药", {WorldLocationId::Camp, WorldLocationId::Marsh}},
        {WorldLocationId::RedPlain, "红土原", "狩猎、训练和难民事件", {WorldLocationId::Camp, WorldLocationId::RiverFord, WorldLocationId::Quarry}},
        {WorldLocationId::Marsh, "芦苇沼泽", "草药、白羽线与海岸路线", {WorldLocationId::Forest, WorldLocationId::WhiteFeatherCamp, WorldLocationId::SaltwindCoast}},
        {WorldLocationId::RiverFord, "河鹿渡口", "河鹿外交和粮食贸易", {WorldLocationId::RedPlain, WorldLocationId::MountainMarket}},
        {WorldLocationId::WhiteFeatherCamp, "白羽营地", "医治、侦察与救助", {WorldLocationId::Marsh}},
        {WorldLocationId::Quarry, "燧石矿场", "石料、武器和玄石路线", {WorldLocationId::RedPlain, WorldLocationId::BlackstoneValley}},
        {WorldLocationId::OldPass, "古老山隘", "岩牙要塞入口和迁徙道路", {WorldLocationId::CliffTradeRoad, WorldLocationId::RockfangFort}},
        {WorldLocationId::RockfangFort, "岩牙要塞", "岩牙战争与征服目标", {WorldLocationId::OldPass}},
        {WorldLocationId::SaltwindCoast, "盐风海岸", "盐、渔获和海风事件", {WorldLocationId::Marsh, WorldLocationId::ShellBeach}},
        {WorldLocationId::TidesaltHarbor, "潮盐港", "航运、贝币与潮盐部落", {WorldLocationId::ShellBeach, WorldLocationId::MountainMarket}},
        {WorldLocationId::ShellBeach, "贝壳滩", "贝壳资源和港口前哨", {WorldLocationId::SaltwindCoast, WorldLocationId::TidesaltHarbor}},
        {WorldLocationId::BlackstoneValley, "玄石谷", "矿脉、雇佣兵和工坊路线", {WorldLocationId::Quarry, WorldLocationId::BlackstoneWorkshop}},
        {WorldLocationId::BlackstoneWorkshop, "玄石工坊", "高级装备和玄石部落", {WorldLocationId::BlackstoneValley, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::MountainMarket, "山前集市", "三路交汇的贸易与情报中心", {WorldLocationId::RiverFord, WorldLocationId::TidesaltHarbor, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::CliffTradeRoad, "断崖商道", "山路贸易、护送和伏击", {WorldLocationId::BlackstoneWorkshop, WorldLocationId::MountainMarket, WorldLocationId::OldPass}},
    }};
    return locations;
}

CampaignGame::CampaignGame(CampaignConfig config) {
    state_.mode = config.mode;
    state_.seed = config.seed;
    state_.tribeName = config.tribeName.empty() ? "燧火" : std::move(config.tribeName);
    state_.leaderName = config.leaderName.empty() ? "炎角" : std::move(config.leaderName);
    state_.leaderFocus = config.leaderFocus.empty() ? "生存" : std::move(config.leaderFocus);
    if (state_.mode == CampaignMode::Quick) {
        state_.season = 9;
        state_.seasonLimit = 16;
        state_.food = 42;
        state_.wood = 24;
        state_.stone = 12;
        state_.warriors = 5;
    } else if (state_.mode == CampaignMode::Long) {
        state_.seasonLimit = 32;
    }
    state_.discovered[indexOf(WorldLocationId::Camp)] = true;
    state_.discovered[indexOf(WorldLocationId::Forest)] = true;
    state_.discovered[indexOf(WorldLocationId::RedPlain)] = true;

    state_.tribes[indexOf(TribeIdV2::Player)] = {TribeIdV2::Player, state_.tribeName,
        state_.leaderName, "", "青枝", state_.leaderFocus,
        {{"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
         {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
         {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeIdV2::RiverDeer)] = {TribeIdV2::RiverDeer, "河鹿", "牧河", "", "禾角", "务实农业",
        {{"农耕者", 50, 65, "稳定粮食", "禾角", FactionCrisis::Calm},
         {"渡口商人", 30, 55, "扩大贸易", "舟苇", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeIdV2::WhiteFeather)] = {TribeIdV2::WhiteFeather, "白羽", "羽医", "", "轻翎", "谨慎救助",
        {{"医者", 45, 65, "救助伤者", "轻翎", FactionCrisis::Calm},
         {"远望者", 35, 60, "共享情报", "苍羽", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeIdV2::Rockfang)] = {TribeIdV2::Rockfang, "岩牙", "赤獠", "", "黑牙", "强硬好战",
        {{"战团", 55, 60, "取得战利品", "黑牙", FactionCrisis::Calm},
         {"矿奴监工", 25, 45, "控制矿路", "裂石", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeIdV2::Tidesalt)] = {TribeIdV2::Tidesalt, "潮盐", "澜母", "", "潮舟", "精明航运",
        {{"船主", 45, 60, "保护航路", "潮舟", FactionCrisis::Calm},
         {"盐工", 35, 55, "提高盐价", "白沫", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeIdV2::Blackstone)] = {TribeIdV2::Blackstone, "玄石", "玄砧", "", "黑炉", "冷静工艺",
        {{"工匠", 45, 60, "换取粮食", "黑炉", FactionCrisis::Calm},
         {"雇佣战士", 35, 50, "获得装备", "玄盾", FactionCrisis::Calm}}};

    state_.relations[indexOf(TribeIdV2::RiverDeer)] = {20, 15, 0, 0};
    state_.relations[indexOf(TribeIdV2::WhiteFeather)] = {5, 5, 0, 0};
    state_.relations[indexOf(TribeIdV2::Rockfang)] = {-40, 0, 35, 0};
    state_.relations[indexOf(TribeIdV2::Tidesalt)] = {0, 0, 0, 0};
    state_.relations[indexOf(TribeIdV2::Blackstone)] = {0, 0, 5, 0};
    state_.playerFactions = {{
        {"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
        {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
        {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm},
    }};

    state_.roster = {
        makeCampaignCharacter("青枝", Occupation::Envoy),
        makeCampaignCharacter("石刃", Occupation::Warrior),
        makeCampaignCharacter("苍眼", Occupation::Scout),
        makeCampaignCharacter("白榆", Occupation::Healer),
        makeCampaignCharacter("逐鹿", Occupation::Hunter),
        makeCampaignCharacter("岩槌", Occupation::Crafter),
        makeCampaignCharacter("芦风", Occupation::Hunter),
        makeCampaignCharacter("河矛", Occupation::Warrior),
    };
    state_.squads.push_back({"晨火队", "青枝", {"青枝", "石刃", "苍眼", "白榆"},
        ResidentMission::Gather, 0, 0, false, false});
    state_.leadershipHistory.push_back(state_.leaderName + "（初代首领）");
    addChronicle(state_, 3, "燧火新议", state_.leaderName + "召集族人，决定走向更广阔的世界。");

    std::string error;
    if (!validateState(state_, error)) throw std::logic_error("V2战役初始状态无效：" + error);
}

CampaignGame::CampaignGame(CampaignState state) : state_(std::move(state)) {
    std::string error;
    if (!validateState(state_, error)) throw std::invalid_argument("V2战役状态无效：" + error);
}

CampaignActionResult CampaignGame::execute(const std::string_view input) {
    const ParsedCommand command = parseCommand(input);
    if (command.verb.empty()) return {};

    if (state_.phase == CampaignPhase::Mission) {
        if (verbIs(command, {"abort", "放弃任务"}) && command.args.empty()) {
            CampaignState candidate = state_;
            candidate.activeMission.reset();
            candidate.phase = CampaignPhase::Managing;
            candidate.stability = std::max(0, candidate.stability - 2);
            addChronicle(candidate, 1, "任务中止", "小队提前返回，部落稳定略有下降。");
            return commit(std::move(candidate), "小队中止任务并返回营地。", false);
        }
        return executeMission(input);
    }
    if (state_.phase == CampaignPhase::War) {
        if (verbIs(command, {"order", "下令"}) && command.args.size() == 1U) {
            const auto order = parseWarOrder(command.args.front());
            return order ? setWarOrder(*order) : rejected("未知军令：推进、坚守、集火、包抄、掩护、撤退。");
        }
        if (verbIs(command, {"attack", "攻击"}) && command.args.empty()) return warAttack();
        if (verbIs(command, {"defend", "防御"}) && command.args.empty()) return warDefend();
        if (verbIs(command, {"retreat", "撤退"}) && command.args.empty()) return warRetreat();
        if (verbIs(command, {"status", "状态", "look", "查看"}) && command.args.empty()) {
            std::ostringstream output;
            output << "战争：对" << tribeName(state_.war.enemy) << "，战线" << state_.war.front << "/3，己方战力"
                   << state_.war.playerPower << "，敌方战力" << state_.war.enemyPower << "，军令"
                   << warOrderName(state_.war.order) << "。";
            return {true, true, false, false, false, false, output.str()};
        }
        return rejected("战争中可用：攻击、防御、下令、撤退、状态。");
    }

    if (verbIs(command, {"status", "状态"}) || command.verb == "1") {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, statusText()}
                                    : rejected("用法：status / 状态");
    }
    if (verbIs(command, {"map", "地图"}) || command.verb == "2") {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, worldText()}
                                    : rejected("用法：map / 地图");
    }
    if (verbIs(command, {"diplomacy", "外交"}) || command.verb == "6") {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, diplomacyText()}
                                    : rejected("用法：diplomacy / 外交");
    }
    if (verbIs(command, {"factions", "派系", "稳定"})) {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, factionText()}
                                    : rejected("用法：factions / 派系");
    }
    if (verbIs(command, {"squads", "小队"}) || command.verb == "7") {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, squadText()}
                                    : rejected("用法：squads / 小队");
    }
    if (verbIs(command, {"objectives", "目标"})) {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, objectiveText()}
                                    : rejected("用法：objectives / 目标");
    }
    if (verbIs(command, {"chronicle", "编年史"})) {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, chronicleText()}
                                    : rejected("用法：chronicle / 编年史");
    }
    if (verbIs(command, {"help", "帮助"}) || command.verb == "9") {
        return command.args.empty() ? CampaignActionResult{true, true, false, false, false, false, helpText()}
                                    : rejected("用法：help / 帮助");
    }
    if (state_.phase == CampaignPhase::Finished) {
        if (verbIs(command, {"sandbox", "继续沙盒"}) && command.args.empty()) return continueSandbox();
        return rejected("结局已经确定。长期模式可输入 sandbox / 继续沙盒。");
    }
    if (state_.phase == CampaignPhase::EndingChoice) {
        if (verbIs(command, {"choose", "选择"}) && command.args.size() == 1U) {
            const auto ending = parseCampaignEnding(command.args.front());
            return ending ? chooseEnding(*ending) : rejected("未知结局道路。");
        }
        return rejected("当前必须先查看目标并选择结局：choose <alliance|conquest|prosperity|migration>。");
    }

    if ((verbIs(command, {"gather", "采集"}) && command.args.size() == 1U)
        || (command.verb == "3" && command.args.empty())) {
        const auto resource = command.verb == "3" ? std::optional<ResourceKind>{ResourceKind::Food}
                                                   : parseResource(command.args.front());
        return resource ? gather(*resource) : rejected("可采集食物、木材、石料或草药。");
    }
    if (command.verb == "4" && command.args.empty()) return gather(ResourceKind::Wood);
    if (verbIs(command, {"scout", "侦察"}) && command.args.size() == 1U) {
        const auto location = parseLocation(command.args.front());
        return location ? scout(*location) : rejected("未知地点，可输入地图查看1至16号地点。");
    }
    if (verbIs(command, {"build", "建造"}) && command.args.size() == 1U) {
        const auto building = parseBuilding(command.args.front());
        return building ? build(*building) : rejected("未知建筑。");
    }
    if (verbIs(command, {"research", "研究"}) && command.args.size() == 1U) {
        const auto technology = parseTechnology(command.args.front());
        return technology ? research(*technology) : rejected("未知技术。");
    }
    if ((verbIs(command, {"mission", "出任务"}) && command.args.size() == 1U
            && equalsAny(command.args.front(), {"forest", "hunt", "苍林", "狩猎"}))
        || (command.verb == "5" && command.args.empty())) return startMission();
    if (verbIs(command, {"squadtask", "常驻任务"}) && command.args.size() == 1U) {
        const auto mission = parseResidentMission(command.args.front());
        return mission ? setResidentMission(*mission) : rejected("常驻任务可选采集、巡逻、侦察、护送、训练或停止。");
    }
    if (verbIs(command, {"squadrest", "小队休整"}) && command.args.empty()) return restSquad();

    if (verbIs(command, {"talk", "交谈"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? talk(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"gift", "送礼"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? gift(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"trade", "贸易"}) && command.args.size() == 3U) {
        const auto tribe = parseTribe(command.args[0]);
        const auto offered = parseResource(command.args[1]);
        const auto requested = parseResource(command.args[2]);
        return tribe && offered && requested ? trade(*tribe, *offered, *requested)
                                             : rejected("用法：trade <部落> <给出的资源> <换取的资源>。");
    }
    if (verbIs(command, {"openroute", "开通商路"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? openTradeRoute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"marry", "联姻"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? marriage(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"tribute", "朝贡"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? offerTribute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"demand", "索贡"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? demandTribute(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"ally", "结盟"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? alliance(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"declare", "宣战"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? declareWar(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"truce", "停战"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? negotiateTruce(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"raid", "劫掠"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? raid(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"appease", "安抚"}) && command.args.size() == 1U) {
        int faction = 0;
        if (!parseNonnegative(command.args.front(), faction) || faction < 1
            || faction > static_cast<int>(kPlayerFactionCount)) return rejected("派系编号为1至3。");
        return appeaseFaction(static_cast<std::size_t>(faction - 1));
    }
    if (verbIs(command, {"formarmy", "组建军队"}) && command.args.size() == 2U) {
        int warriors = 0;
        int militia = 0;
        return parseNonnegative(command.args[0], warriors) && parseNonnegative(command.args[1], militia)
            ? formArmy(warriors, militia) : rejected("用法：formarmy <战士数> <民兵数>。");
    }
    if (verbIs(command, {"war", "出征"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? startWar(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"endturn", "end", "结束回合"}) || command.verb == "8") {
        return command.args.empty() ? endSeason() : rejected("用法：endturn / 结束回合");
    }
    return {};
}

bool CampaignGame::canSpendAction(CampaignActionResult& result) const {
    if (state_.phase != CampaignPhase::Managing && state_.phase != CampaignPhase::Sandbox) {
        result = rejected("当前阶段不能执行部落行动。");
        return false;
    }
    if (state_.actionsLeft <= 0) {
        result = rejected("本季小队行动点已经用完，请结束回合。");
        return false;
    }
    return true;
}

void CampaignGame::spendAction(CampaignState& candidate) const { --candidate.actionsLeft; }

CampaignActionResult CampaignGame::gather(const ResourceKind resource) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (resource == ResourceKind::Shells) return rejected("贝币不能直接采集，只能通过贸易获得。");
    CampaignState candidate = state_;
    int gain = 0;
    switch (resource) {
    case ResourceKind::Food:
        gain = 8 + (candidate.technologies[indexOf(TechnologyId::Irrigation)] ? 3 : 0);
        candidate.food += gain;
        break;
    case ResourceKind::Wood:
        gain = 6;
        candidate.wood += gain;
        break;
    case ResourceKind::Stone:
        if (!locationDiscovered(candidate, WorldLocationId::Quarry)) return rejected("需要先发现燧石矿场。");
        gain = 5;
        candidate.stone += gain;
        break;
    case ResourceKind::Herbs:
        if (!locationDiscovered(candidate, WorldLocationId::Marsh)) return rejected("需要先发现芦苇沼泽。");
        gain = 4 + (candidate.technologies[indexOf(TechnologyId::HerbalKnowledge)] ? 2 : 0);
        candidate.herbs += gain;
        break;
    case ResourceKind::Shells: break;
    }
    spendAction(candidate);
    return commit(std::move(candidate), "小队采集" + resourceName(resource) + "，获得" + std::to_string(gain) + "。", true);
}

CampaignActionResult CampaignGame::scout(const WorldLocationId location) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.discovered[indexOf(location)]) return rejected("该地点已经发现。");
    bool adjacent = false;
    for (const auto& info : worldLocations()) {
        if (!state_.discovered[indexOf(info.id)]) continue;
        adjacent = adjacent || std::find(info.neighbors.begin(), info.neighbors.end(), location) != info.neighbors.end();
    }
    if (!adjacent) return rejected("没有从已发现地点通往该处的道路。");

    CampaignState candidate = state_;
    candidate.discovered[indexOf(location)] = true;
    spendAction(candidate);
    const auto& info = worldLocations()[indexOf(location)];
    addChronicle(candidate, 2, "发现" + info.name, info.feature + "。道路已记录在部落地图上。");
    return commit(std::move(candidate), "侦察队发现" + info.name + "：" + info.feature + "。", true);
}

CampaignActionResult CampaignGame::build(const BuildingId building) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.buildings[indexOf(building)]) return rejected("该唯一建筑已经建成。");
    static const std::array<int, kBuildingCount> woodCosts{{8, 10, 8, 6, 8, 6}};
    static const std::array<int, kBuildingCount> stoneCosts{{2, 2, 6, 2, 4, 4}};
    const int woodCost = woodCosts[indexOf(building)];
    const int stoneCost = stoneCosts[indexOf(building)];
    if (state_.wood < woodCost || state_.stone < stoneCost) {
        return rejected("建造需要木材" + std::to_string(woodCost) + "、石料" + std::to_string(stoneCost) + "，资源不足。");
    }
    CampaignState candidate = state_;
    candidate.wood -= woodCost;
    candidate.stone -= stoneCost;
    candidate.buildings[indexOf(building)] = true;
    candidate.stability = std::min(100, candidate.stability + 2);
    spendAction(candidate);
    return commit(std::move(candidate), "建筑完成，部落稳定提高2。", true);
}

CampaignActionResult CampaignGame::research(const TechnologyId technology) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.technologies[indexOf(technology)]) return rejected("该技术已经研究完成。");
    const int raw = static_cast<int>(technology);
    const int tier = raw % 3;
    if (tier > 0 && !state_.technologies[static_cast<std::size_t>(raw - 1)]) return rejected("必须先研究同路线的前一级技术。");
    if (tier == 2 && !state_.buildings[indexOf(BuildingId::Workshop)]) return rejected("高级技术需要先建工坊。");
    const int foodCost = 3 + tier * 2;
    const int woodCost = 2 + tier;
    if (state_.food < foodCost || state_.wood < woodCost) return rejected("研究所需食物或木材不足。");
    CampaignState candidate = state_;
    candidate.food -= foodCost;
    candidate.wood -= woodCost;
    candidate.technologies[indexOf(technology)] = true;
    spendAction(candidate);
    return commit(std::move(candidate), "研究完成，新的部落知识已经记录。", true);
}

CampaignActionResult CampaignGame::setResidentMission(const ResidentMission mission) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.squads.empty()) return rejected("当前没有永久小队。");
    if (state_.squads.front().refusingOrders) return rejected("小队因派系危机拒绝出队，请先安抚派系。");
    if (state_.squads.front().residentMission == mission) return rejected("小队已经执行该常驻任务。");
    CampaignState candidate = state_;
    candidate.squads.front().residentMission = mission;
    spendAction(candidate);
    return commit(std::move(candidate), "晨火队已调整常驻任务，下季开始自动结算。", true);
}

CampaignActionResult CampaignGame::restSquad() {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.squads.empty() || state_.squads.front().fatigue == 0) return rejected("小队当前无需休整。");
    CampaignState candidate = state_;
    const int recovery = candidate.buildings[indexOf(BuildingId::HealerHut)] ? 45 : 30;
    candidate.squads.front().fatigue = std::max(0, candidate.squads.front().fatigue - recovery);
    spendAction(candidate);
    return commit(std::move(candidate), "小队在营地休整，疲劳下降。", true);
}

CampaignActionResult CampaignGame::startMission() {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.squads.empty()) return rejected("当前没有可出发的小队。");
    if (state_.squads.front().refusingOrders) return rejected("晨火队正在抗命，请先安抚派系。");
    if (state_.squads.front().fatigue >= 85) return rejected("晨火队过于疲劳，需要先休整。");

    CampaignState candidate = state_;
    const std::uint32_t missionSeed = candidate.seed + static_cast<std::uint32_t>(candidate.season * 97 + candidate.missionCount * 17);
    ExpansionGame mission{missionSeed, candidate.squads.front().members.size()};
    candidate.activeMission = mission.state();
    candidate.phase = CampaignPhase::Mission;
    candidate.missionRewardClaimed = false;
    candidate.squads.front().personallyDeployedThisSeason = true;
    spendAction(candidate);
    return commit(std::move(candidate), "晨火队进入苍林任务。现在可直接操控队长移动、采集、交谈和战斗。", true);
}

CampaignActionResult CampaignGame::executeMission(const std::string_view input) {
    if (!state_.activeMission) return rejected("任务状态缺失，无法继续。");
    ExpansionGame mission{*state_.activeMission};
    const ExpansionCommandResult result = mission.execute(input);
    if (!result.recognized) return {};
    if (!result.success) return rejected(result.message);

    CampaignState candidate = state_;
    candidate.activeMission = mission.state();
    std::string message = result.message;
    if (mission.state().phase == ExpansionPhase::ReturnSettlement) {
        const ExpansionState settled = mission.state();
        ++candidate.missionCount;
        PermanentSquad& squad = candidate.squads.front();
        const int fatigueTotal = std::accumulate(settled.squad.members.begin(), settled.squad.members.end(), 0,
            [](const int value, const Character& member) { return value + member.fatigue; });
        squad.fatigue = std::clamp(fatigueTotal / static_cast<int>(settled.squad.members.size()), 0, 100);
        squad.eliteExperience += settled.battleWon ? 35 : settled.traded ? 20 : 10;
        if (settled.missionFailed) {
            ++candidate.missionDeaths;
            candidate.population = std::max(0, candidate.population - 1);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.stability = std::max(0, candidate.stability - 8);
            message += " 部落失去一名队长，人口-1、稳定-8。";
            addChronicle(candidate, 3, "苍林失队长", "任务失败，幸存者带回了沉重消息。");
        } else {
            candidate.food += std::max(0, settled.supplies - 8) + settled.hides;
            candidate.herbs += settled.herbs;
            candidate.morale = std::min(100, candidate.morale + (settled.battleWon ? 6 : 2));
            if (settled.traded) {
                auto& relation = candidate.relations[indexOf(TribeIdV2::WhiteFeather)];
                relation.relation = relationClamp(relation.relation + 6);
                relation.trust = percentClamp(relation.trust + 5);
            }
            message += " 任务成果已并入长期战役资源与关系。";
            addChronicle(candidate, 2, "苍林任务归来", settled.battleWon ? "晨火队突破三段战线并带回装备。"
                                                                      : "晨火队完成采集与和平接触。");
        }
        for (const Character& member : settled.squad.members) candidate.highestLevel = std::max(candidate.highestLevel, member.level);
        candidate.activeMission.reset();
        candidate.missionRewardClaimed = true;
        candidate.phase = CampaignPhase::Managing;
    }
    return commit(std::move(candidate), std::move(message), false);
}

CampaignActionResult CampaignGame::talk(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能普通交谈，请先谈停战。");
    CampaignState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::SharedLanguage)] ? 9 : 5;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 4);
    spendAction(candidate);
    return commit(std::move(candidate), "与" + tribeName(tribe) + "交谈：关系+" + std::to_string(bonus) + "，信任+4。", true);
}

CampaignActionResult CampaignGame::gift(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.food < 4) return rejected("送礼需要4食物。");
    CampaignState candidate = state_;
    candidate.food -= 4;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::GiftCustoms)] ? 14 : 9;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 6);
    spendAction(candidate);
    return commit(std::move(candidate), "向" + tribeName(tribe) + "送礼：关系+" + std::to_string(bonus) + "。", true);
}

int CampaignGame::resourceValue(const CampaignState& state, const ResourceKind resource) const {
    const int amount = resource == ResourceKind::Food ? state.food : resource == ResourceKind::Wood ? state.wood
        : resource == ResourceKind::Stone ? state.stone : resource == ResourceKind::Herbs ? state.herbs : state.shells;
    const int base = resource == ResourceKind::Food ? 3 : resource == ResourceKind::Wood ? 2
        : resource == ResourceKind::Stone ? 4 : resource == ResourceKind::Herbs ? 5 : 1;
    return std::max(1, base + (amount < 10 ? 3 : amount < 20 ? 1 : 0));
}

int& CampaignGame::resourceRef(CampaignState& state, const ResourceKind resource) const {
    switch (resource) {
    case ResourceKind::Food: return state.food;
    case ResourceKind::Wood: return state.wood;
    case ResourceKind::Stone: return state.stone;
    case ResourceKind::Herbs: return state.herbs;
    case ResourceKind::Shells: return state.shells;
    }
    return state.food;
}

CampaignActionResult CampaignGame::trade(const TribeIdV2 tribe, const ResourceKind offered, const ResourceKind requested) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点，不能贸易。");
    if (offered == requested) return rejected("以物易物必须选择两种不同资源。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.atWar) return rejected("战争中不能贸易。");
    if (relation.relation < -10) return rejected("关系过低，对方拒绝贸易。");
    if ((offered == ResourceKind::Shells || requested == ResourceKind::Shells) && !state_.currencyUnlocked) {
        return rejected("尚未解锁贝币，只能以物易物。");
    }
    const int offeredAmount = offered == ResourceKind::Shells ? 8 : 4;
    if ((offered == ResourceKind::Food ? state_.food : offered == ResourceKind::Wood ? state_.wood
            : offered == ResourceKind::Stone ? state_.stone : offered == ResourceKind::Herbs ? state_.herbs : state_.shells)
        < offeredAmount) return rejected("给出的资源不足。");

    CampaignState candidate = state_;
    const int relationBonus = std::max(0, relation.relation) / 25;
    const int requestedAmount = std::clamp(
        offeredAmount * resourceValue(candidate, offered) / resourceValue(candidate, requested) + relationBonus, 1, 10);
    resourceRef(candidate, offered) -= offeredAmount;
    resourceRef(candidate, requested) += requestedAmount;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.relation = relationClamp(changed.relation + 2);
    changed.trust = percentClamp(changed.trust + 3);
    changed.tradeDependence = percentClamp(changed.tradeDependence + 8);
    ++candidate.tradeCount;
    candidate.tradePartners[indexOf(tribe)] = true;
    if (candidate.currencyUnlocked && requested != ResourceKind::Shells) ++candidate.shells;
    spendAction(candidate);
    std::string message = "与" + tribeName(tribe) + "以" + std::to_string(offeredAmount) + resourceName(offered)
        + "换得" + std::to_string(requestedAmount) + resourceName(requested) + "。价格受稀缺、关系和依赖影响。";
    unlockCurrencyIfEligible(candidate, message);
    return commit(std::move(candidate), std::move(message), true);
}

CampaignActionResult CampaignGame::openTradeRoute(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    auto required = WorldLocationId::MountainMarket;
    if (tribe == TribeIdV2::RiverDeer) required = WorldLocationId::RiverFord;
    else if (tribe == TribeIdV2::WhiteFeather) required = WorldLocationId::WhiteFeatherCamp;
    else if (tribe == TribeIdV2::Rockfang) required = WorldLocationId::OldPass;
    else if (tribe == TribeIdV2::Tidesalt) required = WorldLocationId::TidesaltHarbor;
    else if (tribe == TribeIdV2::Blackstone) required = WorldLocationId::BlackstoneWorkshop;
    if (!locationDiscovered(state_, required)) return rejected("尚未发现连接该部落的贸易地点。");
    if (state_.relations[indexOf(tribe)].tradeDependence < 16) return rejected("至少先完成两次有效贸易，建立依赖。");
    if (state_.relations[indexOf(tribe)].tradeRoute) return rejected("该商路已经开通。");
    CampaignState candidate = state_;
    candidate.relations[indexOf(tribe)].tradeRoute = true;
    candidate.stability = std::min(100, candidate.stability + 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "开通商路", state_.tribeName + "与" + tribeName(tribe) + "建立稳定商路。");
    return commit(std::move(candidate), "商路开通，稳定+3，后续贸易更可靠。", true);
}

CampaignActionResult CampaignGame::marriage(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.marriage) return rejected("双方已经存在联姻关系。");
    if (relation.atWar || relation.relation < 60 || relation.trust < 50) return rejected("联姻需要关系60、信任50且不在战争中。");
    CampaignState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.marriage = true;
    changed.relation = relationClamp(changed.relation + 15);
    changed.trust = percentClamp(changed.trust + 10);
    candidate.stability = std::min(100, candidate.stability + 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "联姻", "具名使者在共同火坛前交换信物，也留下继承争议的可能。");
    return commit(std::move(candidate), "联姻完成：关系+15、信任+10、稳定+4。", true);
}

CampaignActionResult CampaignGame::offerTribute(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.food < 6) return rejected("建立朝贡需要先交6食物。");
    if (state_.relations[indexOf(tribe)].playerPaysTribute) return rejected("已经向该部落朝贡。");
    CampaignState candidate = state_;
    candidate.food -= 6;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.playerPaysTribute = true;
    relation.relation = relationClamp(relation.relation + 12);
    relation.fear = percentClamp(relation.fear - 5);
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    return commit(std::move(candidate), "建立朝贡：换取和平，但内部稳定-3，每季继续支付2食物。", true);
}

CampaignActionResult CampaignGame::demandTribute(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.otherPaysTribute) return rejected("对方已经进贡。");
    if (relation.fear < 60 || state_.warriors < 6) return rejected("索贡需要恐惧60且至少6名战士。");
    CampaignState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.otherPaysTribute = true;
    changed.relation = relationClamp(changed.relation - 12);
    candidate.stability = std::max(0, candidate.stability - 2);
    spendAction(candidate);
    return commit(std::move(candidate), "对方同意每季进贡2食物，但关系和内部公平感下降。", true);
}

CampaignActionResult CampaignGame::alliance(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.alliance) return rejected("双方已经结盟。");
    if (relation.atWar || relation.relation < 70 || relation.trust < 60) return rejected("结盟需要关系70、信任60且不在战争中。");
    if (!state_.technologies[indexOf(TechnologyId::Confederation)]) return rejected("需要研究部落联盟技术。");
    CampaignState candidate = state_;
    candidate.relations[indexOf(tribe)].alliance = true;
    candidate.stability = std::min(100, candidate.stability + 5);
    spendAction(candidate);
    addChronicle(candidate, 4, "与" + tribeName(tribe) + "结盟", "双方在共同火坛前立誓互助。");
    return commit(std::move(candidate), "联盟成立，稳定+5。", true);
}

CampaignActionResult CampaignGame::declareWar(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    auto relation = state_.relations[indexOf(tribe)];
    if (relation.atWar) return rejected("双方已经处于战争状态。");
    CampaignState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.atWar = true;
    changed.truce = false;
    changed.alliance = false;
    changed.relation = relationClamp(changed.relation - 35);
    candidate.stability = std::max(0, candidate.stability - 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "向" + tribeName(tribe) + "宣战", "战鼓响起，族人开始准备长期代价。");
    return commit(std::move(candidate), "宣战生效：关系-35、稳定-4。请组建军队后出征。", true);
}

CampaignActionResult CampaignGame::negotiateTruce(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (!state_.relations[indexOf(tribe)].atWar) return rejected("双方并未交战。");
    if (state_.food < 5) return rejected("停战谈判需要5食物作为赔偿和宴席。");
    CampaignState candidate = state_;
    candidate.food -= 5;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.atWar = false;
    relation.truce = true;
    relation.relation = std::max(-30, relation.relation);
    relation.trust = std::max(10, relation.trust);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "停战", "双方同意暂时放下武器，伤痕仍未消失。");
    return commit(std::move(candidate), "停战达成，战争状态解除。", true);
}

CampaignActionResult CampaignGame::raid(const TribeIdV2 tribe) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现通往该部落的道路，不能劫掠。");
    if (state_.warriors < 2) return rejected("劫掠至少需要2名战士。");
    CampaignState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int gain = 5 + static_cast<int>((candidate.seed + candidate.season + indexOf(tribe)) % 4U);
    candidate.food += gain;
    relation.relation = relationClamp(relation.relation - 25);
    relation.fear = percentClamp(relation.fear + 15);
    relation.trust = percentClamp(relation.trust - 12);
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "劫掠" + tribeName(tribe), "获得食物" + std::to_string(gain) + "，也播下新的仇恨。");
    return commit(std::move(candidate), "劫掠获得" + std::to_string(gain) + "食物；关系-25、恐惧+15、稳定-3。", true);
}

CampaignActionResult CampaignGame::appeaseFaction(const std::size_t faction) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.food < 4) return rejected("安抚派系需要公平分配4食物。");
    CampaignState candidate = state_;
    candidate.food -= 4;
    FactionState& changed = candidate.playerFactions[faction];
    changed.satisfaction = std::min(100, changed.satisfaction + 20);
    changed.crisis = static_cast<FactionCrisis>(std::max(0, static_cast<int>(changed.crisis) - 2));
    candidate.stability = std::min(100, candidate.stability + 8);
    for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = false;
    spendAction(candidate);
    return commit(std::move(candidate), "公平分配缓和了" + changed.name + "的不满：满意+20、稳定+8。", true);
}

CampaignActionResult CampaignGame::formArmy(const int warriors, const int militia) {
    CampaignActionResult result;
    if (!canSpendAction(result)) return result;
    if (warriors <= 0 || warriors > state_.warriors) return rejected("正式战士数量必须为1至现有战士数。");
    if (militia < 0 || militia > std::max(0, state_.population - 6)) return rejected("民兵征召会影响人口，当前数量不合法。");
    CampaignState candidate = state_;
    candidate.war = {false, TribeIdV2::Rockfang, "石刃", warriors, militia,
        warriors * 2 + militia + candidate.morale / 10, 0, 0, WarOrder::Hold, false};
    if (candidate.technologies[indexOf(TechnologyId::FlintSpear)]) candidate.war.playerPower += 3;
    spendAction(candidate);
    return commit(std::move(candidate), "军队已组建：统帅石刃，正式战士" + std::to_string(warriors)
        + "，民兵" + std::to_string(militia) + "。出征前请查看风险。", true);
}

CampaignActionResult CampaignGame::startWar(const TribeIdV2 enemy) {
    if (state_.phase != CampaignPhase::Managing && state_.phase != CampaignPhase::Sandbox) return rejected("当前不能出征。");
    if (state_.war.commander.empty() || state_.war.warriors <= 0) return rejected("请先组建军队。");
    if (!state_.relations[indexOf(enemy)].atWar) return rejected("需要先正式宣战。");
    const WorldLocationId target = enemy == TribeIdV2::Rockfang ? WorldLocationId::RockfangFort : contactLocation(enemy);
    if (!locationDiscovered(state_, target)) return rejected("尚未发现通往战争目标的路线，不能出征。");
    CampaignState candidate = state_;
    candidate.phase = CampaignPhase::War;
    candidate.war.active = true;
    candidate.war.enemy = enemy;
    candidate.war.front = 1;
    candidate.war.enemyPower = enemy == TribeIdV2::Rockfang ? candidate.rockfangStrength
        : 12 + static_cast<int>(indexOf(enemy)) * 2;
    candidate.war.riskConfirmed = true;
    return commit(std::move(candidate), "出征开始。预计风险：敌方战力" + std::to_string(candidate.war.enemyPower)
        + "，己方" + std::to_string(candidate.war.playerPower) + "；民兵伤亡会减少人口与稳定。", false);
}

CampaignActionResult CampaignGame::setWarOrder(const WarOrder order) {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    if (state_.war.order == order) return rejected("军队已经执行该军令。");
    CampaignState candidate = state_;
    candidate.war.order = order;
    if (order == WarOrder::Retreat) {
        return warRetreat();
    }
    return commit(std::move(candidate), "统帅下令“" + warOrderName(order) + "”。", false);
}

CampaignActionResult CampaignGame::warAttack() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    CampaignState candidate = state_;
    int modifier = 0;
    switch (candidate.war.order) {
    case WarOrder::Advance: modifier = 4; break;
    case WarOrder::Focus: modifier = 3; break;
    case WarOrder::Flank: modifier = candidate.technologies[indexOf(TechnologyId::AmbushTraining)] ? 5 : 1; break;
    case WarOrder::Hold: modifier = -1; break;
    case WarOrder::Cover: modifier = -2; break;
    case WarOrder::Retreat: return warRetreat();
    }
    const int damage = std::max(2, candidate.war.playerPower / 4 + modifier);
    candidate.war.enemyPower = std::max(0, candidate.war.enemyPower - damage);
    std::string message = "军队按“" + warOrderName(candidate.war.order) + "”进攻，敌方战力-" + std::to_string(damage) + "。";
    if (candidate.war.enemyPower == 0) {
        advanceWarFront(candidate, message);
    } else {
        const int casualty = std::max(0, candidate.war.enemyPower / 10
            - (candidate.war.order == WarOrder::Cover || candidate.war.order == WarOrder::Hold ? 1 : 0));
        int remaining = casualty;
        const int militiaLost = std::min(candidate.war.militia, remaining);
        candidate.war.militia -= militiaLost;
        candidate.population = std::max(0, candidate.population - militiaLost);
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        remaining -= militiaLost;
        const int warriorsLost = std::min(candidate.war.warriors, remaining);
        candidate.war.warriors -= warriorsLost;
        candidate.warriors = std::max(0, candidate.warriors - warriorsLost);
        candidate.war.playerPower = std::max(0, candidate.war.playerPower - casualty * 2);
        if (casualty > 0) {
            candidate.stability = std::max(0, candidate.stability - militiaLost * 2 - warriorsLost);
            message += " 反击造成" + std::to_string(casualty) + "人伤亡。";
        }
        if (candidate.war.warriors + candidate.war.militia <= 0 || candidate.war.playerPower <= 0) {
            candidate.war.active = false;
            candidate.phase = CampaignPhase::Managing;
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

CampaignActionResult CampaignGame::warDefend() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    CampaignState candidate = state_;
    const int defense = 2 + (candidate.technologies[indexOf(TechnologyId::ShieldWall)] ? 3 : 0)
        + (candidate.buildings[indexOf(BuildingId::Wall)] ? 2 : 0);
    const int counter = std::max(1, candidate.war.playerPower / 8 + defense);
    const int actualCounter = std::min(counter, std::max(0, candidate.war.enemyPower - 1));
    candidate.war.enemyPower -= actualCounter;
    std::string message = "军队坚守并反击，敌方战力-" + std::to_string(actualCounter) + "。";
    if (candidate.war.enemyPower == 1) {
        message += " 敌军阵线已经动摇，但防守不能占领战线；需要主动攻击才能推进。";
    }
    return commit(std::move(candidate), std::move(message), false);
}

void CampaignGame::advanceWarFront(CampaignState& candidate, std::string& message) const {
    ++candidate.war.front;
    if (candidate.war.front > 3) {
        const TribeIdV2 enemy = candidate.war.enemy;
        candidate.war.active = false;
        candidate.phase = CampaignPhase::Managing;
        ++candidate.warsWon;
        auto& relation = candidate.relations[indexOf(enemy)];
        relation.fear = percentClamp(relation.fear + 25);
        relation.relation = relationClamp(relation.relation - 10);
        if (enemy == TribeIdV2::Rockfang) {
            candidate.rockfangFortCaptured = true;
            candidate.rockfangStrength = 0;
        }
        candidate.morale = std::min(100, candidate.morale + 8);
        candidate.stability = std::min(100, candidate.stability + 5);
        addChronicle(candidate, 4, "战争胜利", "石刃率军击败" + tribeName(enemy) + "，三段战线全部突破。");
        message += " 三段战线全部突破，战争胜利。";
        return;
    }
    candidate.war.enemyPower = 8 + candidate.war.front * 5
        + static_cast<int>((candidate.seed + candidate.season) % 4U);
    message += " 突破一段战线，进入第" + std::to_string(candidate.war.front) + "段。";
}

CampaignActionResult CampaignGame::warRetreat() {
    if (!state_.war.active) return rejected("当前没有进行中的战争。");
    CampaignState candidate = state_;
    candidate.war.active = false;
    candidate.phase = CampaignPhase::Managing;
    candidate.food = std::max(0, candidate.food - 4);
    candidate.morale = std::max(0, candidate.morale - 6);
    candidate.stability = std::max(0, candidate.stability - 3);
    addChronicle(candidate, 2, "军队撤退", "统帅保住主力，但消耗补给并打击士气。");
    return commit(std::move(candidate), "全军撤退：食物-4、士气-6、稳定-3。", false);
}

void CampaignGame::settleResidentSquads(CampaignState& candidate, std::string& message) const {
    for (PermanentSquad& squad : candidate.squads) {
        if (squad.personallyDeployedThisSeason || squad.refusingOrders || squad.residentMission == ResidentMission::None) {
            squad.personallyDeployedThisSeason = false;
            continue;
        }
        const int elite = squad.eliteExperience / 50;
        const int fatiguePenalty = squad.fatigue / 35;
        const int gain = std::max(1, 3 + elite - fatiguePenalty);
        switch (squad.residentMission) {
        case ResidentMission::Gather: candidate.food += gain; message += " 晨火队常驻采集食物+" + std::to_string(gain) + "。"; break;
        case ResidentMission::Patrol: candidate.stability = std::min(100, candidate.stability + 2); message += " 巡逻使稳定+2。"; break;
        case ResidentMission::Explore: candidate.morale = std::min(100, candidate.morale + 1); message += " 侦察情报使士气+1。"; break;
        case ResidentMission::Escort: candidate.shells += candidate.currencyUnlocked ? 2 : 0; candidate.food += 1; message += " 护送带回少量收益。"; break;
        case ResidentMission::Train:
            if (candidate.food < 2) {
                message += " 训练因食物不足未能完成。";
            } else if (candidate.warriors >= candidate.population) {
                message += " 训练暂停：战士人数不能超过部落人口。";
            } else {
                candidate.food -= 2;
                ++candidate.warriors;
                message += " 训练从现有人口中培养1名战士。";
            }
            break;
        case ResidentMission::None: break;
        }
        squad.eliteExperience += 8;
        squad.fatigue = std::min(100, squad.fatigue + 15);
    }
}

void CampaignGame::settleFoodAndTribute(CampaignState& candidate, std::string& message) const {
    int consumption = (candidate.population + 2) / 3;
    if ((candidate.season - 1) % 4 == 3) consumption += 2;
    if (candidate.buildings[indexOf(BuildingId::Granary)] && (candidate.season - 1) % 4 == 3) consumption = std::max(0, consumption - 2);
    for (std::size_t index = 1; index < kTribeV2Count; ++index) {
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
        candidate.warriors = std::min(candidate.warriors, candidate.population);
        candidate.morale = std::max(0, candidate.morale - 12);
        candidate.stability = std::max(0, candidate.stability - 15);
        message += " 食物不足，人口-" + std::to_string(loss) + "、士气-12、稳定-15。";
    }
}

void CampaignGame::settleAutonomousTribes(CampaignState& candidate, std::string& message) const {
    const std::size_t index = 1U + static_cast<std::size_t>((candidate.seed + static_cast<std::uint32_t>(candidate.season * 13)) % 5U);
    auto& relation = candidate.relations[index];
    const TribeIdV2 tribe = static_cast<TribeIdV2>(index);
    const int choice = static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.season * 7 + index)) % 4U);
    if (relation.atWar) {
        relation.fear = percentClamp(relation.fear + 2);
        message += " " + tribeName(tribe) + "在战争中集结兵力，恐惧+2。";
    } else if (choice == 0) {
        relation.relation = relationClamp(relation.relation + 3);
        relation.trust = percentClamp(relation.trust + 2);
        message += " " + tribeName(tribe) + "派来使者，关系+3、信任+2。";
    } else if (choice == 1 && relation.tradeRoute) {
        candidate.food += 2;
        relation.tradeDependence = percentClamp(relation.tradeDependence + 2);
        message += " " + tribeName(tribe) + "商队沿固定路线送来2食物。";
    } else if (choice == 2 && relation.relation < -30) {
        relation.fear = percentClamp(relation.fear + 4);
        candidate.campDurability = std::max(0, candidate.campDurability - 2);
        message += " " + tribeName(tribe) + "边境骚扰使营地耐久-2。";
    } else {
        relation.relation = relationClamp(relation.relation - 2);
        message += " " + tribeName(tribe) + "因内部派系施压而疏远，关系-2。";
    }

    if (candidate.season % 8 == 0 && index != indexOf(TribeIdV2::Player)) {
        TribeProfile& profile = candidate.tribes[index];
        profile.actingLeader = profile.successor;
        profile.leader = profile.successor;
        message += " " + profile.name + "首领更替为" + profile.leader + "。";
        addChronicle(candidate, 3, profile.name + "首领更替", profile.successor + "在派系推举下接掌部落。");
    }
}

void CampaignGame::settleFactions(CampaignState& candidate, std::string& message) const {
    for (FactionState& faction : candidate.playerFactions) {
        const int pressure = candidate.food == 0 ? 12 : candidate.stability < 40 ? 8 : -2;
        faction.satisfaction = percentClamp(faction.satisfaction - pressure);
        int stage = static_cast<int>(faction.crisis);
        if (faction.satisfaction < 25 || candidate.stability < 25) stage = std::min(5, stage + 1);
        else if (faction.satisfaction >= 55 && stage > 0) --stage;
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
            candidate.tribes[indexOf(TribeIdV2::Player)].leader = faction.candidate;
            candidate.leadershipHistory.push_back(faction.candidate + "（派系政变接任）");
            candidate.stability = 35;
            faction.satisfaction = 50;
            faction.crisis = FactionCrisis::Complaint;
            message += " " + faction.name + "发动政变，" + faction.candidate + "成为代理首领。";
            addChronicle(candidate, 4, "部落政变", faction.candidate + "在危机中接替原首领。");
        }
    }
}

void CampaignGame::settleEvent(CampaignState& candidate, std::string& message) const {
    const int event = static_cast<int>((candidate.seed + static_cast<std::uint32_t>(candidate.season * 17)) % 6U);
    switch (event) {
    case 0:
        candidate.food += 5;
        message += " 天气温和，狩猎食物+5。";
        break;
    case 1:
        if (candidate.buildings[indexOf(BuildingId::HealerHut)]) message += " 医者小屋控制了疾病。";
        else {
            candidate.population = std::max(0, candidate.population - 1);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.stability = std::max(0, candidate.stability - 4);
            message += " 疾病使人口-1、稳定-4。";
        }
        break;
    case 2:
        if (candidate.food >= 4) {
            candidate.food -= 4;
            candidate.population += 2;
            candidate.stability = std::min(100, candidate.stability + 4);
            message += " 接纳难民：食物-4、人口+2、稳定+4。";
        } else message += " 无力接纳难民，他们继续远行。";
        break;
    case 3:
        candidate.wood += 4;
        candidate.stone += 2;
        message += " 游商交换情报并留下木材4、石料2。";
        break;
    case 4:
        candidate.campDurability = std::max(0, candidate.campDurability - (candidate.buildings[indexOf(BuildingId::Wall)] ? 1 : 4));
        message += " 野兽冲击营地，耐久下降。";
        break;
    case 5:
        candidate.herbs += 3;
        message += " 草药花期到来，草药+3。";
        break;
    }
}

void CampaignGame::unlockCurrencyIfEligible(CampaignState& candidate, std::string& message) const {
    if (candidate.currencyUnlocked) return;
    const int partners = countTrue(candidate.tradePartners) - (candidate.tradePartners[indexOf(TribeIdV2::Player)] ? 1 : 0);
    if (candidate.tradeCount >= 8 && partners >= 3
        && candidate.technologies[indexOf(TechnologyId::SharedLanguage)]) {
        candidate.currencyUnlocked = true;
        candidate.shells += 20;
        message += " 完成8次贸易、连接3个部落并掌握共同语言，统一度量事件触发，贝币解锁并获得20枚。";
        addChronicle(candidate, 4, "贝币诞生", "多部落接受统一贝壳度量，以物易物仍被保留。");
    }
}

void CampaignGame::finishExtinction(CampaignState& candidate, std::string& message) const {
    if (candidate.population > 0 && candidate.campDurability > 0) return;
    candidate.phase = CampaignPhase::Finished;
    candidate.ending = CampaignEnding::Extinction;
    candidate.activeMission.reset();
    candidate.war.active = false;
    message += " 部落人口或营地耐久归零，燧火熄灭。";
    addChronicle(candidate, 5, "部落覆灭", "最后的火坛在风中熄灭。");
}

CampaignActionResult CampaignGame::endSeason() {
    if (state_.phase != CampaignPhase::Managing && state_.phase != CampaignPhase::Sandbox) return rejected("当前不能结束季节。");
    CampaignState candidate = state_;
    std::string message = "第" + std::to_string(candidate.season) + "季结算：";
    settleResidentSquads(candidate, message);
    settleFoodAndTribute(candidate, message);
    settleAutonomousTribes(candidate, message);
    settleFactions(candidate, message);
    settleEvent(candidate, message);
    unlockCurrencyIfEligible(candidate, message);
    finishExtinction(candidate, message);
    if (candidate.phase == CampaignPhase::Finished) return commit(std::move(candidate), std::move(message), false, true, true);

    if (candidate.season >= candidate.seasonLimit && candidate.phase != CampaignPhase::Sandbox) {
        candidate.phase = CampaignPhase::EndingChoice;
        candidate.actionsLeft = 0;
        candidate.longModeFinalShown = candidate.mode == CampaignMode::Long;
        addChronicle(candidate, 4, "时代结算", "族人围绕火坛讨论已经满足的道路。");
        message += " 已到达模式结算季，请查看目标并选择结局。";
        return commit(std::move(candidate), std::move(message), false, true, false);
    }

    ++candidate.season;
    candidate.actionsLeft = availableTeams(candidate);
    for (PermanentSquad& squad : candidate.squads) squad.personallyDeployedThisSeason = false;
    return commit(std::move(candidate), std::move(message), false, true, false);
}

std::vector<CampaignEnding> CampaignGame::availableEndings() const {
    std::vector<CampaignEnding> endings;
    if (state_.population <= 0 || state_.campDurability <= 0) return {CampaignEnding::Extinction};
    int allies = 0;
    for (std::size_t index = 1; index < kTribeV2Count; ++index) allies += state_.relations[index].alliance ? 1 : 0;
    if (allies >= 2 && state_.relations[indexOf(TribeIdV2::RiverDeer)].relation >= 70
        && state_.relations[indexOf(TribeIdV2::WhiteFeather)].relation >= 70
        && state_.technologies[indexOf(TechnologyId::Confederation)]) endings.push_back(CampaignEnding::Alliance);
    if (state_.rockfangFortCaptured && state_.warriors >= 5 && state_.morale >= 55) endings.push_back(CampaignEnding::Conquest);
    if (state_.population >= 20 && state_.food >= 40 && countTrue(state_.buildings) >= 4
        && countTrue(state_.technologies) >= 4) endings.push_back(CampaignEnding::Prosperity);
    endings.push_back(CampaignEnding::Migration);
    return endings;
}

CampaignActionResult CampaignGame::chooseEnding(const CampaignEnding ending) {
    if (state_.phase != CampaignPhase::EndingChoice) return rejected("现在还不能选择结局。");
    const auto endings = availableEndings();
    if (std::find(endings.begin(), endings.end(), ending) == endings.end()) return rejected("当前条件尚未满足该结局道路。");
    CampaignState candidate = state_;
    candidate.ending = ending;
    candidate.phase = CampaignPhase::Finished;
    addChronicle(candidate, 5, endingName(ending), "族人共同选择了这条道路。");
    return commit(std::move(candidate), "结局已确定：" + endingName(ending) + "。进入独立结算画面。", false, false, true);
}

CampaignActionResult CampaignGame::continueSandbox() {
    if (state_.phase != CampaignPhase::Finished || state_.mode != CampaignMode::Long
        || state_.ending == CampaignEnding::Extinction) return rejected("只有长期模式非覆灭结局可以继续沙盒。");
    CampaignState candidate = state_;
    candidate.phase = CampaignPhase::Sandbox;
    candidate.actionsLeft = availableTeams(candidate);
    ++candidate.season;
    return commit(std::move(candidate), "进入结局后的自由沙盒，部落可以继续经营。", false);
}

CampaignActionResult CampaignGame::commit(CampaignState candidate, std::string message,
    const bool consumesAction, const bool seasonAdvanced, const bool endingReached) {
    std::string error;
    if (!validateState(candidate, error)) return rejected("行动后的状态未通过校验，已原子取消：" + error);
    state_ = std::move(candidate);
    return {true, true, true, consumesAction, seasonAdvanced, endingReached, std::move(message)};
}

CampaignActionResult CampaignGame::rejected(std::string message) const {
    return {true, false, false, false, false, false, std::move(message)};
}

void CampaignGame::addChronicle(CampaignState& candidate, const int importance,
    std::string title, std::string detail) const {
    candidate.chronicle.push_back({candidate.season, std::clamp(importance, 1, 5), std::move(title), std::move(detail)});
    if (candidate.chronicle.size() > 200U) candidate.chronicle.erase(candidate.chronicle.begin());
}

int CampaignGame::availableTeams(const CampaignState& state) const {
    return state.population < 5 ? 1 : state.population < 10 ? 2 : 3;
}

WorldLocationId CampaignGame::contactLocation(const TribeIdV2 tribe) const {
    switch (tribe) {
    case TribeIdV2::RiverDeer: return WorldLocationId::RiverFord;
    case TribeIdV2::WhiteFeather: return WorldLocationId::WhiteFeatherCamp;
    case TribeIdV2::Rockfang: return WorldLocationId::OldPass;
    case TribeIdV2::Tidesalt: return WorldLocationId::TidesaltHarbor;
    case TribeIdV2::Blackstone: return WorldLocationId::BlackstoneWorkshop;
    case TribeIdV2::Player:
    case TribeIdV2::Count: return WorldLocationId::MountainMarket;
    }
    return WorldLocationId::MountainMarket;
}

bool CampaignGame::locationDiscovered(const CampaignState& state, const WorldLocationId location) const {
    return state.discovered[indexOf(location)];
}

bool CampaignGame::replaceState(const CampaignState& candidate, std::string& error) {
    if (!validateState(candidate, error)) return false;
    state_ = candidate;
    error.clear();
    return true;
}

bool CampaignGame::validateState(const CampaignState& candidate, std::string& error) {
    if (!enumInRange(candidate.mode, CampaignMode::Quick, CampaignMode::Long)
        || !enumInRange(candidate.phase, CampaignPhase::Managing, CampaignPhase::Sandbox)
        || !enumInRange(candidate.ending, CampaignEnding::None, CampaignEnding::Extinction)) {
        error = "模式、阶段或结局枚举无效。";
        return false;
    }
    if (candidate.season <= 0 || candidate.seasonLimit <= 0
        || candidate.season > 10000 || candidate.seasonLimit > 10000
        || candidate.actionsLeft < 0 || candidate.actionsLeft > 3) {
        error = "种子、季节或行动点范围无效。";
        return false;
    }
    const std::array<int, 18> nonnegative{{candidate.population, candidate.food, candidate.wood,
        candidate.stone, candidate.herbs, candidate.warriors, candidate.morale, candidate.campDurability,
        candidate.stability, candidate.shells, candidate.tradeCount, candidate.warsWon, candidate.warsLost,
        candidate.missionCount, candidate.missionDeaths, candidate.highestLevel, candidate.rockfangStrength,
        candidate.seasonLimit}};
    if (std::any_of(nonnegative.begin(), nonnegative.end(), [](const int value) { return value < 0; })
        || candidate.morale > 100 || candidate.stability > 100 || candidate.campDurability > 100) {
        error = "资源或百分比超出范围。";
        return false;
    }
    if (candidate.warriors > candidate.population) {
        error = "战士人数不能超过部落人口。";
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
    for (std::size_t index = 0; index < kTribeV2Count; ++index) {
        const TribeProfile& profile = candidate.tribes[index];
        if (profile.id != static_cast<TribeIdV2>(index) || profile.name.empty() || profile.leader.empty()
            || profile.successor.empty() || profile.factions.size() < 2U || profile.factions.size() > 3U) {
            error = "六部落档案不完整。";
            return false;
        }
        const auto& relation = candidate.relations[index];
        if (relation.relation < -100 || relation.relation > 100 || relation.trust < 0 || relation.trust > 100
            || relation.fear < 0 || relation.fear > 100 || relation.tradeDependence < 0 || relation.tradeDependence > 100
            || (relation.atWar && relation.alliance)) {
            error = "外交关系字段越界或矛盾。";
            return false;
        }
    }
    for (const FactionState& faction : candidate.playerFactions) {
        if (faction.name.empty() || faction.candidate.empty() || faction.influence < 0 || faction.influence > 100
            || faction.satisfaction < 0 || faction.satisfaction > 100
            || !enumInRange(faction.crisis, FactionCrisis::Calm, FactionCrisis::Coup)) {
            error = "玩家派系字段无效。";
            return false;
        }
    }
    if (candidate.roster.size() < 2U || candidate.roster.size() > 64U) {
        error = "角色名单人数无效。";
        return false;
    }
    std::unordered_set<std::string> rosterNames;
    for (const Character& character : candidate.roster) {
        if (character.name.empty() || !rosterNames.insert(character.name).second
            || character.level <= 0 || character.level > 100 || character.experience < 0
            || character.growthPoints < 0 || character.life < 0
            || character.fatigue < 0 || character.fatigue > 100
            || character.loyalty < 0 || character.loyalty > 100) {
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
    }
    if (candidate.squads.empty() || candidate.squads.size() > 8U) {
        error = "永久小队数量无效。";
        return false;
    }
    for (const PermanentSquad& squad : candidate.squads) {
        if (squad.name.empty() || squad.captain.empty() || squad.members.size() < 2U || squad.members.size() > 8U
            || squad.fatigue < 0 || squad.fatigue > 100 || squad.eliteExperience < 0) {
            error = "永久小队字段无效。";
            return false;
        }
        std::unordered_set<std::string> members;
        for (const std::string& member : squad.members) {
            if (!rosterNames.count(member) || !members.insert(member).second) {
                error = "小队成员不在角色名单或重复。";
                return false;
            }
        }
        if (!members.count(squad.captain)) {
            error = "小队长必须属于小队。";
            return false;
        }
    }
    if (candidate.phase == CampaignPhase::Mission) {
        if (!candidate.activeMission || !ExpansionGame::validateState(*candidate.activeMission)) {
            error = "任务阶段缺少合法任务状态。";
            return false;
        }
    } else if (candidate.activeMission) {
        error = "非任务阶段不能保留活动任务。";
        return false;
    }
    if (candidate.phase == CampaignPhase::War) {
        if (!candidate.war.active || candidate.war.commander.empty() || candidate.war.warriors < 0
            || candidate.war.militia < 0 || candidate.war.playerPower < 0 || candidate.war.enemyPower <= 0
            || candidate.war.front < 1 || candidate.war.front > 3) {
            error = "战争阶段字段无效。";
            return false;
        }
    } else if (candidate.war.active) {
        error = "非战争阶段不能保留活动战争。";
        return false;
    }
    if (candidate.phase == CampaignPhase::Finished && candidate.ending == CampaignEnding::None) {
        error = "结束阶段必须有结局。";
        return false;
    }
    if (candidate.phase != CampaignPhase::Finished && candidate.phase != CampaignPhase::Sandbox
        && candidate.ending != CampaignEnding::None) {
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

std::string CampaignGame::statusText() const {
    std::ostringstream output;
    output << "V2长期战役  模式：" << modeName(state_.mode) << "  季节：" << state_.season << "/" << state_.seasonLimit
           << "  阶段：" << phaseName(state_.phase) << "  行动点：" << state_.actionsLeft << "\n"
           << "部落：" << state_.tribeName << "  首领：" << state_.leaderName;
    if (!state_.actingLeaderName.empty()) output << "（代理/继任：" << state_.actingLeaderName << "）";
    output << "  稳定：" << state_.stability << "  士气：" << state_.morale << "\n"
           << "人口：" << state_.population << "  食物：" << state_.food << "  木材：" << state_.wood
           << "  石料：" << state_.stone << "  草药：" << state_.herbs << "  战士：" << state_.warriors << "\n"
           << "营地耐久：" << state_.campDurability << "  贝币：" << state_.shells
           << (state_.currencyUnlocked ? "（已流通）" : "（未解锁）") << "  贸易次数：" << state_.tradeCount << "\n"
           << "建筑：" << countTrue(state_.buildings) << "/6  技术：" << countTrue(state_.technologies)
           << "/9  已发现地点：" << countTrue(state_.discovered) << "/16  战争胜负："
           << state_.warsWon << "/" << state_.warsLost;
    return output.str();
}

std::string CampaignGame::worldText() const {
    std::ostringstream output;
    output << "十六地点世界地图（相邻地点逐步侦察）：\n";
    for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
        const auto& location = worldLocations()[index];
        output << (index + 1) << ". " << (state_.discovered[index] ? location.name : "????")
               << (state_.discovered[index] ? " — " + location.feature : "") << '\n';
    }
    return output.str();
}

std::string CampaignGame::diplomacyText() const {
    std::ostringstream output;
    output << "六部落外交（关系/信任/恐惧/贸易依赖）：\n";
    for (std::size_t index = 1; index < kTribeV2Count; ++index) {
        const auto& profile = state_.tribes[index];
        const auto& relation = state_.relations[index];
        output << profile.name << "  首领" << profile.leader << "  " << relation.relation << '/' << relation.trust
               << '/' << relation.fear << '/' << relation.tradeDependence;
        if (relation.atWar) output << " [战争]";
        if (relation.truce) output << " [停战]";
        if (relation.alliance) output << " [联盟]";
        if (relation.marriage) output << " [联姻]";
        if (relation.playerPaysTribute) output << " [我方朝贡]";
        if (relation.otherPaysTribute) output << " [对方进贡]";
        if (relation.tradeRoute) output << " [固定商路]";
        output << '\n';
    }
    return output.str();
}

std::string CampaignGame::factionText() const {
    std::ostringstream output;
    output << "内部稳定：" << state_.stability << "\n";
    for (std::size_t index = 0; index < kPlayerFactionCount; ++index) {
        const auto& faction = state_.playerFactions[index];
        output << (index + 1) << ". " << faction.name << " 影响" << faction.influence << " 满意"
               << faction.satisfaction << " 危机：" << crisisName(faction.crisis) << " 诉求：" << faction.demand
               << " 候选：" << faction.candidate << '\n';
    }
    return output.str();
}

std::string CampaignGame::squadText() const {
    std::ostringstream output;
    output << "具名人物：" << state_.roster.size() << " 最高等级：" << state_.highestLevel << "\n";
    for (const PermanentSquad& squad : state_.squads) {
        output << squad.name << " 队长" << squad.captain << " 人数" << squad.members.size() << " 疲劳"
               << squad.fatigue << " 精锐经验" << squad.eliteExperience << " 常驻任务"
               << static_cast<int>(squad.residentMission) << (squad.refusingOrders ? " [抗命]" : "") << '\n';
    }
    return output.str();
}

std::string CampaignGame::objectiveText() const {
    const auto endings = availableEndings();
    std::ostringstream output;
    output << "当前已满足道路：";
    for (const CampaignEnding ending : endings) output << endingName(ending) << ' ';
    output << "\n联盟：河鹿/白羽关系70、至少2个联盟、部落联盟技术。"
           << "\n征服：攻下岩牙要塞、战士5、士气55。"
           << "\n繁荣：人口20、食物40、建筑4、技术4。"
           << "\n迁徙：只要部落仍存活即可选择。";
    return output.str();
}

std::string CampaignGame::chronicleText() const {
    std::ostringstream output;
    const std::size_t start = state_.chronicle.size() > 12U ? state_.chronicle.size() - 12U : 0U;
    for (std::size_t index = start; index < state_.chronicle.size(); ++index) {
        const auto& entry = state_.chronicle[index];
        output << "第" << entry.season << "季 [" << entry.importance << "] " << entry.title << "：" << entry.detail << '\n';
    }
    return output.str();
}

std::string CampaignGame::helpText() const {
    return
        "查询：1/status状态 2/map地图 6/diplomacy外交 factions派系 squads小队 objectives目标 chronicle编年史\n"
        "经营：gather/采集 <食物|木材|石料|草药>，scout/侦察 <地点>，build/建造 <建筑>，research/研究 <技术>\n"
        "任务：5 或 mission forest；任务内直接使用move/gather/talk/trade/raid/attack/defend/order/loot/return\n"
        "小队：squadtask <采集|巡逻|侦察|护送|训练|停止>，squadrest 小队休整\n"
        "外交：talk gift trade <部落> <给出资源> <换取资源> openroute marry tribute demand ally declare truce raid\n"
        "内政：appease <1至3>；战争：formarmy <战士> <民兵>，war <部落>，战中attack/defend/order/retreat\n"
        "季节：8/endturn；结局：choose <alliance|conquest|prosperity|migration>；长期结局后sandbox。";
}

EndingSummary CampaignGame::endingSummary() const {
    EndingSummary summary;
    summary.ending = state_.ending;
    summary.title = endingName(state_.ending);
    switch (state_.ending) {
    case CampaignEnding::Alliance: summary.epilogue = "诸部落的旗帜围绕共同火坛，争执仍在，但道路第一次由议事而非刀锋决定。"; break;
    case CampaignEnding::Conquest: summary.epilogue = "红金战旗升上岩牙要塞，胜利带来疆土，也要求后人承担统治的代价。"; break;
    case CampaignEnding::Prosperity: summary.epilogue = "粮仓、工坊与炊烟连成新的聚落，燧火从求生之火变成文明之火。"; break;
    case CampaignEnding::Migration: summary.epilogue = "队伍越过山隘，把旧火种带往晨光中的新土地。"; break;
    case CampaignEnding::Extinction: summary.epilogue = "营墙倒塌，火坛变暗；留下的故事提醒后来者饥饿、战争与分裂的代价。"; break;
    case CampaignEnding::None: summary.epilogue = "战役尚未结束。"; break;
    }
    summary.statistics = {
        "生存季节：" + std::to_string(state_.season),
        "人口/食物/稳定：" + std::to_string(state_.population) + "/" + std::to_string(state_.food) + "/" + std::to_string(state_.stability),
        "建筑/技术：" + std::to_string(countTrue(state_.buildings)) + "/" + std::to_string(countTrue(state_.technologies)),
        "小队任务/阵亡：" + std::to_string(state_.missionCount) + "/" + std::to_string(state_.missionDeaths),
        "战争胜负：" + std::to_string(state_.warsWon) + "/" + std::to_string(state_.warsLost),
        "贸易次数/贝币：" + std::to_string(state_.tradeCount) + "/" + std::to_string(state_.shells),
        "最终首领：" + state_.leaderName + "，历任记录" + std::to_string(state_.leadershipHistory.size()) + "条",
    };
    for (const CampaignEnding ending : availableEndings()) {
        if (ending != state_.ending) summary.otherRoads.push_back(endingName(ending));
    }
    std::vector<ChronicleEntry> sorted = state_.chronicle;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ChronicleEntry& left, const ChronicleEntry& right) {
        return left.importance > right.importance;
    });
    if (sorted.size() > 10U) sorted.resize(10U);
    summary.importantChronicle = std::move(sorted);
    return summary;
}

std::string CampaignGame::modeName(const CampaignMode mode) {
    switch (mode) {
    case CampaignMode::Quick: return "快速8季";
    case CampaignMode::Course: return "课程16季";
    case CampaignMode::Long: return "长期32季";
    }
    return "未知模式";
}

std::string CampaignGame::phaseName(const CampaignPhase phase) {
    switch (phase) {
    case CampaignPhase::Managing: return "部落管理";
    case CampaignPhase::Mission: return "可操控小队任务";
    case CampaignPhase::War: return "可操控部落战争";
    case CampaignPhase::EndingChoice: return "时代结算选择";
    case CampaignPhase::Finished: return "独立结局结算";
    case CampaignPhase::Sandbox: return "结局后沙盒";
    }
    return "未知阶段";
}

std::string CampaignGame::endingName(const CampaignEnding ending) {
    switch (ending) {
    case CampaignEnding::None: return "尚未结算";
    case CampaignEnding::Alliance: return "联盟共主";
    case CampaignEnding::Conquest: return "山河征服者";
    case CampaignEnding::Prosperity: return "燧火繁荣";
    case CampaignEnding::Migration: return "迁徙新生";
    case CampaignEnding::Extinction: return "部落覆灭";
    }
    return "未知结局";
}

std::string CampaignGame::tribeName(const TribeIdV2 tribe) {
    switch (tribe) {
    case TribeIdV2::Player: return "玩家部落";
    case TribeIdV2::RiverDeer: return "河鹿";
    case TribeIdV2::WhiteFeather: return "白羽";
    case TribeIdV2::Rockfang: return "岩牙";
    case TribeIdV2::Tidesalt: return "潮盐";
    case TribeIdV2::Blackstone: return "玄石";
    case TribeIdV2::Count: break;
    }
    return "未知部落";
}

std::string CampaignGame::resourceName(const ResourceKind resource) {
    switch (resource) {
    case ResourceKind::Food: return "食物";
    case ResourceKind::Wood: return "木材";
    case ResourceKind::Stone: return "石料";
    case ResourceKind::Herbs: return "草药";
    case ResourceKind::Shells: return "贝币";
    }
    return "未知资源";
}

} // namespace tribe
