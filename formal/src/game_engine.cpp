#include "tribe/game_engine.hpp"

#include "population_rules.hpp"
#include "seasonal_event_rules.hpp"
#include "war_rules.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <iterator>
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
    if (equalsAny(text, {"hides", "hide", "兽皮"})) return ResourceKind::Hides;
    return std::nullopt;
}

std::optional<WorkforceRole> parseWorkforceRole(const std::string_view text) {
    if (equalsAny(text, {"food", "食物队", "食物"})) return WorkforceRole::FoodCrew;
    if (equalsAny(text, {"wood", "木材队", "木材"})) return WorkforceRole::WoodCrew;
    if (equalsAny(text, {"stone", "石料队", "石料"})) return WorkforceRole::StoneCrew;
    if (equalsAny(text, {"herbs", "草药队", "草药"})) return WorkforceRole::HerbCrew;
    if (equalsAny(text, {"crafters", "crafter", "工匠"})) return WorkforceRole::Crafters;
    if (equalsAny(text, {"healers", "healer", "医者"})) return WorkforceRole::Healers;
    if (equalsAny(text, {"scouts", "scout", "侦察"})) return WorkforceRole::Scouts;
    if (equalsAny(text, {"envoys", "envoy", "使者"})) return WorkforceRole::Envoys;
    if (equalsAny(text, {"guards", "guard", "营地守卫", "守卫"})) return WorkforceRole::CampGuards;
    return std::nullopt;
}

std::string equipmentSlotName(const EquipmentSlot slot) {
    static const std::array<const char*, kEquipmentSlotCount> names{
        {"主手", "副手", "头部", "身体", "手部", "腿脚", "工具", "饰品"}};
    return names[indexOf(slot)];
}

std::optional<EquipmentSlot> parseEquipmentSlot(const std::string_view value) {
    if (equalsAny(value, {"mainhand", "main", "主手"})) return EquipmentSlot::MainHand;
    if (equalsAny(value, {"offhand", "off", "副手"})) return EquipmentSlot::OffHand;
    if (equalsAny(value, {"head", "头部"})) return EquipmentSlot::Head;
    if (equalsAny(value, {"body", "身体"})) return EquipmentSlot::Body;
    if (equalsAny(value, {"hands", "手部"})) return EquipmentSlot::Hands;
    if (equalsAny(value, {"legs", "legsfeet", "腿脚"})) return EquipmentSlot::LegsFeet;
    if (equalsAny(value, {"tool", "工具"})) return EquipmentSlot::Tool;
    if (equalsAny(value, {"accessory", "饰品"})) return EquipmentSlot::Accessory;
    return std::nullopt;
}

std::optional<TribeId> parseTribe(const std::string_view text) {
    if (equalsAny(text, {"river", "riverdeer", "河鹿"})) return TribeId::RiverDeer;
    if (equalsAny(text, {"white", "whitefeather", "白羽"})) return TribeId::WhiteFeather;
    if (equalsAny(text, {"rock", "rockfang", "岩牙"})) return TribeId::Rockfang;
    if (equalsAny(text, {"tide", "tidesalt", "潮盐"})) return TribeId::Tidesalt;
    if (equalsAny(text, {"black", "blackstone", "玄石"})) return TribeId::Blackstone;
    return std::nullopt;
}

std::optional<TribeId> missionTribeAt(const int location) {
    switch (static_cast<WorldLocationId>(location)) {
        case WorldLocationId::RiverFord:
            return TribeId::RiverDeer;
        case WorldLocationId::WhiteFeatherCamp:
            return TribeId::WhiteFeather;
        case WorldLocationId::OldPass:
            return TribeId::Rockfang;
        case WorldLocationId::TidesaltHarbor:
            return TribeId::Tidesalt;
        case WorldLocationId::BlackstoneWorkshop:
            return TribeId::Blackstone;
        default:
            return std::nullopt;
    }
}

std::string tribeCommandName(const TribeId tribe) {
    switch (tribe) {
        case TribeId::RiverDeer:
            return "river";
        case TribeId::WhiteFeather:
            return "white";
        case TribeId::Rockfang:
            return "rock";
        case TribeId::Tidesalt:
            return "tide";
        case TribeId::Blackstone:
            return "black";
        default:
            return {};
    }
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

int enemyBasePower(const TribeId tribe) {
    switch (tribe) {
        case TribeId::RiverDeer:
            return 14;
        case TribeId::WhiteFeather:
            return 16;
        case TribeId::Rockfang:
            return 20;
        case TribeId::Tidesalt:
            return 18;
        case TribeId::Blackstone:
            return 22;
        case TribeId::Player:
        case TribeId::Count:
            return 0;
    }
    return 0;
}

std::optional<BuildingId> parseBuilding(const std::string_view text) {
    if (equalsAny(text, {"granary", "粮仓"})) return BuildingId::Granary;
    if (equalsAny(text, {"wall", "木墙"})) return BuildingId::Wall;
    if (equalsAny(text, {"workshop", "武备工坊"})) return BuildingId::Workshop;
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

std::optional<WarOrder> parseWarOrder(const std::string_view text) {
    if (equalsAny(text, {"advance", "推进"})) return WarOrder::Advance;
    if (equalsAny(text, {"hold", "坚守"})) return WarOrder::Hold;
    if (equalsAny(text, {"focus", "集火"})) return WarOrder::Focus;
    if (equalsAny(text, {"flank", "包抄"})) return WarOrder::Flank;
    if (equalsAny(text, {"cover", "掩护"})) return WarOrder::Cover;
    if (equalsAny(text, {"retreat", "撤退"})) return WarOrder::Retreat;
    return std::nullopt;
}

std::optional<GameEnding> parseGameEnding(const std::string_view text) {
    if (equalsAny(text, {"alliance", "联盟", "联盟共主"})) return GameEnding::Alliance;
    if (equalsAny(text, {"conquest", "征服", "山河征服者"})) return GameEnding::Conquest;
    if (equalsAny(text, {"prosperity", "繁荣", "燧火繁荣"})) return GameEnding::Prosperity;
    if (equalsAny(text, {"migration", "迁徙", "迁徙新生"})) return GameEnding::Migration;
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

Item makeCampaignLeaderBow() {
    Item item;
    item.id = "leader_bow";
    item.name = "苍林短弓";
    item.weight = 3;
    item.equipmentSlot = EquipmentSlot::MainHand;
    item.bonuses[Attribute::Perception] = 2;
    return item;
}

Item makeCampaignSpareKnife() {
    Item item;
    item.id = "spare_knife";
    item.name = "备用石刀";
    item.weight = 2;
    item.equipmentSlot = EquipmentSlot::MainHand;
    item.bonuses[Attribute::Strength] = 1;
    return item;
}

Character* findRosterCharacter(std::vector<Character>& roster, const std::string_view name) {
    const auto found =
        std::find_if(roster.begin(), roster.end(), [&](const Character& character) { return character.name == name; });
    return found == roster.end() ? nullptr : &*found;
}

const Character* findRosterCharacter(const std::vector<Character>& roster, const std::string_view name) {
    const auto found =
        std::find_if(roster.begin(), roster.end(), [&](const Character& character) { return character.name == name; });
    return found == roster.end() ? nullptr : &*found;
}

int permanentSquadFatigue(const PermanentSquad& squad, const std::vector<Character>& roster) {
    int total = 0;
    int count = 0;
    for (const std::string& name : squad.members) {
        const Character* member = findRosterCharacter(roster, name);
        if (member == nullptr) continue;
        total += member->fatigue;
        ++count;
    }
    return count == 0 ? 0 : std::clamp(total / count, 0, 100);
}

int relationClamp(const int value) { return std::clamp(value, -100, 100); }
int percentClamp(const int value) { return std::clamp(value, 0, 100); }

bool containsAny(const std::string_view text, const std::initializer_list<std::string_view> keywords) {
    return std::any_of(keywords.begin(), keywords.end(),
                       [&](const std::string_view keyword) { return text.find(keyword) != std::string_view::npos; });
}

const FactionState& dominantFaction(const TribeProfile& profile) {
    return *std::max_element(
        profile.factions.begin(), profile.factions.end(),
        [](const FactionState& left, const FactionState& right) { return left.influence < right.influence; });
}

bool knowsFactionDemand(const DiplomacyRelation& relation) {
    return relation.trust >= 20 || relation.tradeDependence >= 10 || relation.alliance || relation.marriage ||
           relation.tradeRoute;
}

bool knowsFullFactionNetwork(const DiplomacyRelation& relation) {
    return relation.trust >= 45 || relation.tradeDependence >= 30 || relation.alliance || relation.marriage;
}

std::string warOrderName(const WarOrder order) {
    switch (order) {
        case WarOrder::Advance:
            return "推进";
        case WarOrder::Hold:
            return "坚守";
        case WarOrder::Focus:
            return "集火";
        case WarOrder::Flank:
            return "包抄";
        case WarOrder::Cover:
            return "掩护";
        case WarOrder::Retreat:
            return "全军撤退";
    }
    return "未知";
}

std::string crisisName(const FactionCrisis crisis) {
    switch (crisis) {
        case FactionCrisis::Calm:
            return "平稳";
        case FactionCrisis::Complaint:
            return "抱怨";
        case FactionCrisis::Slowdown:
            return "减产";
        case FactionCrisis::Refusal:
            return "拒绝出队";
        case FactionCrisis::Deposition:
            return "要求罢免";
        case FactionCrisis::Coup:
            return "政变";
    }
    return "未知";
}

template <typename Container>
int countTrue(const Container& values) {
    return static_cast<int>(std::count(values.begin(), values.end(), true));
}

int itemQualityTier(const ItemQuality quality) {
    switch (quality) {
        case ItemQuality::Fine:
            return 1;
        case ItemQuality::Rare:
            return 2;
        case ItemQuality::Legendary:
            return 3;
        case ItemQuality::Crude:
        case ItemQuality::Common:
            return 0;
    }
    return 0;
}

std::string itemQualityName(const ItemQuality quality) {
    switch (quality) {
        case ItemQuality::Crude:
            return "粗制";
        case ItemQuality::Common:
            return "普通";
        case ItemQuality::Fine:
            return "精良";
        case ItemQuality::Rare:
            return "稀有";
        case ItemQuality::Legendary:
            return "传说";
    }
    return "未知";
}

std::string occupationName(const Occupation occupation) {
    switch (occupation) {
        case Occupation::Hunter:
            return "猎手";
        case Occupation::Warrior:
            return "战士";
        case Occupation::Scout:
            return "侦察";
        case Occupation::Healer:
            return "医者";
        case Occupation::Crafter:
            return "工匠";
        case Occupation::Envoy:
            return "使者";
    }
    return "未知";
}

std::string locationRoleName(const LocationRole role) {
    switch (role) {
        case LocationRole::Camp:
            return "营地";
        case LocationRole::Resource:
            return "资源";
        case LocationRole::Diplomacy:
            return "外交";
        case LocationRole::Route:
            return "路线";
        case LocationRole::War:
            return "战争";
    }
    return "未知";
}

bool isPermanentSquadMember(const GameState& state, const std::string_view name) {
    return std::any_of(state.squads.begin(), state.squads.end(), [&](const PermanentSquad& squad) {
        return std::find(squad.members.begin(), squad.members.end(), name) != squad.members.end();
    });
}

int craftRankFor(const Character* supervisor) {
    if (supervisor == nullptr) return 0;
    return std::clamp(
        (supervisor->attributes[Attribute::Endurance] + supervisor->attributes[Attribute::Perception] - 10) / 4, 0, 3);
}

int craftSupervisorRank(const GameState& state) {
    return craftRankFor(findRosterCharacter(state.roster, state.workshopSupervisor));
}

int craftRank(const GameState& state) {
    if (state.workforce.crafters == 0 || !state.buildings[indexOf(BuildingId::Workshop)]) return 0;
    return craftSupervisorRank(state);
}

int medicineRankFor(const Character* supervisor) {
    if (supervisor == nullptr) return 0;
    return std::clamp(
        (supervisor->attributes[Attribute::Survival] + supervisor->attributes[Attribute::Willpower] - 10) / 5, 0, 3);
}

int medicineSupervisorRank(const GameState& state) {
    return medicineRankFor(findRosterCharacter(state.roster, state.healerSupervisor));
}

int medicineRank(const GameState& state) {
    if (state.workforce.healers == 0 || !state.buildings[indexOf(BuildingId::HealerHut)]) return 0;
    return medicineSupervisorRank(state);
}

std::string eventName(const PendingEventKind kind) {
    switch (kind) {
        case PendingEventKind::Refugees:
            return "难民来访";
        case PendingEventKind::Disease:
            return "疾病蔓延";
        case PendingEventKind::Extortion:
            return "边境勒索";
        case PendingEventKind::FactionDemand:
            return "派系诉求";
    }
    return "未知事件";
}

std::string eventText(const GameState& state) {
    if (!state.pendingEvent.active) return "本季暂无待决事件。";
    std::ostringstream out;
    out << eventName(state.pendingEvent.kind) << '\n';
    switch (state.pendingEvent.kind) {
        case PendingEventKind::Refugees:
            out << "1. 接纳：食物-4、人口+1、稳定+2\n2. 拒绝：稳定-3";
            break;
        case PendingEventKind::Disease: {
            const bool staffed = state.buildings[indexOf(BuildingId::HealerHut)] && state.workforce.healers > 0;
            out << "1. 医治：草药-" << (staffed ? 1 : 2) << "、稳定+2\n2. 隔离失败：人口-1、稳定-4";
            break;
        }
        case PendingEventKind::Extortion:
            out << "1. 缴纳：食物-4、稳定+3\n2. 抵抗：稳定-2、营地耐久-"
                << seasonal_event_rules::extortionDamage(state);
            break;
        case PendingEventKind::FactionDemand: {
            const bool council = state.buildings[indexOf(BuildingId::CouncilFire)] && state.workforce.envoys > 0;
            out << "1. 让步：食物-3、全派系满意+" << (council ? 8 : 5) << "\n2. 拒绝：全派系满意-6、稳定-2";
            break;
        }
    }
    return out.str();
}

} // namespace

const std::array<WorldLocationInfo, kWorldLocationCount>& GameEngine::worldLocations() {
    static const std::array<WorldLocationInfo, kWorldLocationCount> locations{{
        {WorldLocationId::Camp,
         "燧火营地",
         "部落管理、建设与结算",
         LocationRole::Camp,
         {WorldLocationId::Forest, WorldLocationId::RedPlain}},
        {WorldLocationId::Forest,
         "苍林",
         "食物、木材、草药与兽皮",
         LocationRole::Resource,
         {WorldLocationId::Camp, WorldLocationId::Marsh}},
        {WorldLocationId::RedPlain,
         "红土原",
         "食物与兽皮采集，通往渡口和矿场",
         LocationRole::Resource,
         {WorldLocationId::Camp, WorldLocationId::RiverFord, WorldLocationId::Quarry}},
        {WorldLocationId::Marsh,
         "芦苇沼泽",
         "木材与草药采集，连接白羽与海岸",
         LocationRole::Resource,
         {WorldLocationId::Forest, WorldLocationId::WhiteFeatherCamp, WorldLocationId::SaltwindCoast}},
        {WorldLocationId::RiverFord,
         "河鹿渡口",
         "河鹿部落交谈、贸易与商路",
         LocationRole::Diplomacy,
         {WorldLocationId::RedPlain, WorldLocationId::MountainMarket}},
        {WorldLocationId::WhiteFeatherCamp,
         "白羽营地",
         "白羽部落交谈、贸易与联盟",
         LocationRole::Diplomacy,
         {WorldLocationId::Marsh}},
        {WorldLocationId::Quarry,
         "燧石矿场",
         "石料采集，通往玄石谷",
         LocationRole::Resource,
         {WorldLocationId::RedPlain, WorldLocationId::BlackstoneValley}},
        {WorldLocationId::OldPass,
         "古老山隘",
         "连接岩牙要塞的山路",
         LocationRole::Route,
         {WorldLocationId::CliffTradeRoad, WorldLocationId::RockfangFort}},
        {WorldLocationId::RockfangFort,
         "岩牙要塞",
         "岩牙巡逻与征服目标",
         LocationRole::War,
         {WorldLocationId::OldPass}},
        {WorldLocationId::SaltwindCoast,
         "盐风海岸",
         "远程食物采集，通往潮盐港",
         LocationRole::Resource,
         {WorldLocationId::Marsh, WorldLocationId::ShellBeach}},
        {WorldLocationId::TidesaltHarbor,
         "潮盐港",
         "潮盐部落交谈、贸易与商路",
         LocationRole::Diplomacy,
         {WorldLocationId::ShellBeach, WorldLocationId::MountainMarket}},
        {WorldLocationId::ShellBeach,
         "贝壳滩",
         "连接海岸与潮盐港的潮汐通道",
         LocationRole::Route,
         {WorldLocationId::SaltwindCoast, WorldLocationId::TidesaltHarbor}},
        {WorldLocationId::BlackstoneValley,
         "玄石谷",
         "石料采集，通往玄石工坊",
         LocationRole::Resource,
         {WorldLocationId::Quarry, WorldLocationId::BlackstoneWorkshop}},
        {WorldLocationId::BlackstoneWorkshop,
         "玄石工坊",
         "玄石部落交谈、贸易与联盟",
         LocationRole::Diplomacy,
         {WorldLocationId::BlackstoneValley, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::MountainMarket,
         "山前集市",
         "三路交汇，开通商路的交通节点",
         LocationRole::Route,
         {WorldLocationId::RiverFord, WorldLocationId::TidesaltHarbor, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::CliffTradeRoad,
         "断崖商道",
         "连接集市、玄石与山隘的商路",
         LocationRole::Route,
         {WorldLocationId::BlackstoneWorkshop, WorldLocationId::MountainMarket, WorldLocationId::OldPass}},
    }};
    return locations;
}

GameEngine::GameEngine(GameConfig config) {
    state_.mode = config.mode;
    state_.seed = config.seed;
    state_.tribeName = config.tribeName.empty() ? "燧火" : std::move(config.tribeName);
    state_.leaderName = config.leaderName.empty() ? "炎角" : std::move(config.leaderName);
    state_.leaderFocus = config.leaderFocus.empty() ? "生存" : std::move(config.leaderFocus);
    if (state_.mode == GameMode::Quick) {
        state_.season = 1;
        state_.seasonLimit = 8;
        state_.food = 42;
        state_.wood = 24;
        state_.stone = 12;
        state_.warriors = 5;
    } else if (state_.mode == GameMode::Long) {
        state_.seasonLimit = 32;
        state_.food = 20;
    } else {
        state_.food = 20;
    }
    state_.discovered[indexOf(WorldLocationId::Camp)] = true;
    state_.discovered[indexOf(WorldLocationId::Forest)] = true;
    state_.discovered[indexOf(WorldLocationId::RedPlain)] = true;
    state_.outposts[indexOf(WorldLocationId::Camp)] = true;

    state_.tribes[indexOf(TribeId::Player)] = {TribeId::Player,
                                               state_.tribeName,
                                               state_.leaderName,
                                               "",
                                               "青枝",
                                               state_.leaderFocus,
                                               {{"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
                                                {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
                                                {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::RiverDeer)] = {TribeId::RiverDeer,
                                                  "河鹿",
                                                  "牧河",
                                                  "",
                                                  "禾角",
                                                  "务实农业",
                                                  {{"农耕者", 50, 65, "稳定粮食", "禾角", FactionCrisis::Calm},
                                                   {"渡口商人", 30, 55, "扩大贸易", "舟苇", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::WhiteFeather)] = {TribeId::WhiteFeather,
                                                     "白羽",
                                                     "羽医",
                                                     "",
                                                     "轻翎",
                                                     "谨慎救助",
                                                     {{"医者", 45, 65, "救助伤者", "轻翎", FactionCrisis::Calm},
                                                      {"远望者", 35, 60, "共享情报", "苍羽", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Rockfang)] = {TribeId::Rockfang,
                                                 "岩牙",
                                                 "赤獠",
                                                 "",
                                                 "黑牙",
                                                 "强硬好战",
                                                 {{"战团", 55, 60, "取得战利品", "黑牙", FactionCrisis::Calm},
                                                  {"矿奴监工", 25, 45, "控制矿路", "裂石", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Tidesalt)] = {TribeId::Tidesalt,
                                                 "潮盐",
                                                 "澜母",
                                                 "",
                                                 "潮舟",
                                                 "精明航运",
                                                 {{"船主", 45, 60, "保护航路", "潮舟", FactionCrisis::Calm},
                                                  {"盐工", 35, 55, "提高盐价", "白沫", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Blackstone)] = {TribeId::Blackstone,
                                                   "玄石",
                                                   "玄砧",
                                                   "",
                                                   "黑炉",
                                                   "冷静工艺",
                                                   {{"工匠", 45, 60, "换取粮食", "黑炉", FactionCrisis::Calm},
                                                    {"雇佣战士", 35, 50, "获得装备", "玄盾", FactionCrisis::Calm}}};

    state_.relations[indexOf(TribeId::RiverDeer)] = {20, 15, 0, 0};
    state_.relations[indexOf(TribeId::WhiteFeather)] = {5, 5, 0, 0};
    state_.relations[indexOf(TribeId::Rockfang)] = {-40, 0, 35, 0};
    state_.relations[indexOf(TribeId::Tidesalt)] = {0, 0, 0, 0};
    state_.relations[indexOf(TribeId::Blackstone)] = {0, 0, 5, 0};
    state_.playerFactions = {{
        {"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
        {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
        {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm},
    }};

    state_.roster = {
        makeCampaignCharacter("青枝", Occupation::Envoy),  makeCampaignCharacter("石刃", Occupation::Warrior),
        makeCampaignCharacter("苍眼", Occupation::Scout),  makeCampaignCharacter("白榆", Occupation::Healer),
        makeCampaignCharacter("逐鹿", Occupation::Hunter), makeCampaignCharacter("岩槌", Occupation::Crafter),
        makeCampaignCharacter("芦风", Occupation::Hunter), makeCampaignCharacter("河矛", Occupation::Warrior),
    };
    const OperationResult equipped = equipItem(state_.roster.front(), EquipmentSlot::MainHand, makeCampaignLeaderBow());
    if (!equipped) throw std::logic_error("长期人物初始装备失败：" + equipped.message);
    state_.squads.push_back({"晨火队", "青枝", {"青枝", "石刃", "苍眼", "芦风"}, 0, 0, false, false});
    state_.leadershipHistory.push_back(state_.leaderName + "（初代首领）");
    addChronicle(state_, 3, "燧火新议", state_.leaderName + "召集族人，决定走向更广阔的世界。");

    std::string error;
    if (!validateState(state_, error)) throw std::logic_error("游戏初始状态无效：" + error);
}

GameEngine::GameEngine(GameState state) : state_(std::move(state)) {
    std::string error;
    if (!validateState(state_, error)) throw std::invalid_argument("游戏状态无效：" + error);
}

ActionResult GameEngine::execute(const std::string_view input) {
    const ParsedCommand command = parseCommand(input);
    if (command.verb.empty()) return {};

    if (state_.phase == GamePhase::Mission) {
        if (verbIs(command, {"abort", "放弃任务"}) && command.args.empty()) {
            GameState candidate = state_;
            if (!candidate.squads.empty()) candidate.squads.front().station = WorldLocationId::Camp;
            candidate.activeMission.reset();
            candidate.phase = GamePhase::Managing;
            candidate.stability = std::max(0, candidate.stability - 2);
            addChronicle(candidate, 1, "任务中止", "小队提前返回，部落稳定略有下降。");
            return commit(std::move(candidate), "小队放弃当前载货并返回营地。", false);
        }
        return executeMission(input);
    }
    if (state_.phase == GamePhase::War) {
        if (verbIs(command, {"order", "下令"}) && command.args.size() == 1U) {
            const auto order = parseWarOrder(command.args.front());
            return order ? setWarOrder(*order) : rejected("未知军令：推进、坚守、集火、包抄、掩护、撤退。");
        }
        if (verbIs(command, {"attack", "攻击"}) && command.args.empty()) return warAttack();
        if (verbIs(command, {"defend", "防御"}) && command.args.empty()) return warDefend();
        if (verbIs(command, {"retreat", "撤退"}) && command.args.empty()) return warRetreat();
        if (verbIs(command, {"status", "状态", "look", "查看"}) && command.args.empty()) {
            std::ostringstream output;
            output << "战争：对" << tribeName(state_.war.enemy) << "，己方战力" << state_.war.playerPower
                   << "，敌方战力" << state_.war.enemyPower << "，军令" << warOrderName(state_.war.order) << "。";
            return {true, true, false, false, false, false, output.str()};
        }
        return rejected("战争中可用：攻击、防御、下令、撤退、状态。");
    }

    if (verbIs(command, {"status", "状态"}) || command.verb == "1") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, statusText()}
                                    : rejected("用法：status / 状态");
    }
    if (verbIs(command, {"map", "地图"}) || command.verb == "2") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, worldText()}
                                    : rejected("用法：map / 地图");
    }
    if (verbIs(command, {"diplomacy", "外交"}) || command.verb == "6") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, diplomacyText()}
                                    : rejected("用法：diplomacy / 外交");
    }
    if (verbIs(command, {"factions", "派系", "稳定"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, factionText()}
                                    : rejected("用法：factions / 派系");
    }
    if (verbIs(command, {"squad", "小队配置"}) && command.args.size() >= 3U &&
        equalsAny(command.args.front(), {"configure", "配置"})) {
        return configureSquad(std::vector<std::string>(command.args.begin() + 1, command.args.end()));
    }
    if (verbIs(command, {"squads", "小队"}) || command.verb == "7") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, squadText()}
                                    : rejected("用法：squads / 小队");
    }
    if (verbIs(command, {"objectives", "目标"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, objectiveText()}
                                    : rejected("用法：objectives / 目标");
    }
    if (verbIs(command, {"chronicle", "编年史"})) {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, chronicleText()}
                                    : rejected("用法：chronicle / 编年史");
    }
    if (verbIs(command, {"help", "帮助"}) || command.verb == "9") {
        return command.args.empty() ? ActionResult{true, true, false, false, false, false, helpText()}
                                    : rejected("用法：help / 帮助");
    }
    if (state_.phase == GamePhase::Finished) {
        if (verbIs(command, {"sandbox", "继续沙盒"}) && command.args.empty()) return continueSandbox();
        return rejected("结局已经确定。长期模式可输入 sandbox / 继续沙盒。");
    }
    if (state_.phase == GamePhase::EndingChoice) {
        if (verbIs(command, {"choose", "选择"}) && command.args.size() == 1U) {
            const auto ending = parseGameEnding(command.args.front());
            return ending ? chooseEnding(*ending) : rejected("未知结局道路。");
        }
        return rejected("当前必须先查看目标并选择结局：choose <alliance|conquest|prosperity|migration>。");
    }
    if (state_.workforceReassignmentRequired) {
        const bool loweringRole = verbIs(command, {"assign", "分配"}) && command.args.size() == 2U &&
                                  parseWorkforceRole(command.args[0]).has_value();
        const bool loweringOutpost = verbIs(command, {"assign", "分配"}) && command.args.size() == 3U &&
                                     equalsAny(command.args[0], {"outpost", "前哨"});
        const bool loweringGarrison = verbIs(command, {"garrison", "驻军"}) && command.args.size() == 2U;
        const bool releasingArmy = verbIs(command, {"disbandarmy", "解散军队"}) && command.args.empty();
        const bool viewingEvent = verbIs(command, {"event", "事件"}) && command.args.empty();
        if (!loweringRole && !loweringOutpost && !loweringGarrison && !releasingArmy && !viewingEvent) {
            return rejected(
                "人口已不足以维持现有岗位。请降低劳力或驻军，或用 disbandarmy / 解散军队释放军队后再行动。");
        }
    }
    if (state_.pendingEvent.active && !state_.workforceReassignmentRequired && !verbIs(command, {"event", "事件"})) {
        return rejected("本季有待决事件；请先输入 event 查看并选择 event <1|2>。 ");
    }

    if (verbIs(command, {"build", "建造"}) && command.args.size() == 1U) {
        const auto building = parseBuilding(command.args.front());
        return building ? build(*building) : rejected("未知建筑。");
    }
    if (verbIs(command, {"research", "研究"}) && command.args.size() == 1U) {
        const auto technology = parseTechnology(command.args.front());
        return technology ? research(*technology) : rejected("未知技术。");
    }
    if (verbIs(command, {"mission", "出任务"}) && command.args.size() <= 1U) {
        if (command.args.empty() || equalsAny(command.args.front(), {"world", "map", "地图", "探索"}))
            return startMission();
        if (equalsAny(command.args.front(), {"outpost", "前哨"})) return startOutpostMission();
        const auto resource = parseResource(command.args.front());
        return resource ? startMission(*resource) : rejected("任务类型：食物、木材、石料、草药或兽皮。");
    }
    if (command.verb == "5" && command.args.empty()) return startMission();
    if ((verbIs(command, {"workforce", "劳力"}) || command.verb == "3") && command.args.empty())
        return {true, true, false, false, false, false, workforceText()};
    if (verbIs(command, {"assign", "分配"}) && command.args.size() == 3U &&
        equalsAny(command.args[0], {"outpost", "前哨"})) {
        int count = 0;
        const auto location = parseLocation(command.args[1]);
        return location && parseNonnegative(command.args[2], count) ? assignOutpostGuards(*location, count)
                                                                    : rejected("用法：assign outpost <地点> <人数>。");
    }
    if (verbIs(command, {"assign", "分配"}) && command.args.size() == 2U) {
        int count = 0;
        const auto role = parseWorkforceRole(command.args[0]);
        return role && parseNonnegative(command.args[1], count) ? assignWorkforce(*role, count)
                                                                : rejected("用法：assign <岗位> <人数>。");
    }
    if ((verbIs(command, {"inventory", "仓库"}) || command.verb == "4") && command.args.empty())
        return {true, true, false, false, false, false, inventoryText()};
    if (verbIs(command, {"people", "人物"}) && command.args.empty())
        return {true, true, false, false, false, false, peopleText()};
    if (verbIs(command, {"person", "人物"}) && command.args.size() == 1U)
        return {true, true, false, false, false, false, personText(command.args[0])};
    if (verbIs(command, {"buildings", "建筑清单"}) && command.args.empty())
        return {true, true, false, false, false, false, buildingsText()};
    if (verbIs(command, {"technologies", "技术清单"}) && command.args.empty())
        return {true, true, false, false, false, false, technologiesText()};
    if (verbIs(command, {"craft", "制造"}) && command.args.size() == 1U) return craft(command.args[0]);
    if (verbIs(command, {"repair", "维修"}) && command.args.size() == 1U) return repair(command.args[0]);
    if (verbIs(command, {"scrap", "报废"}) && command.args.size() == 1U) return scrap(command.args[0]);
    if (verbIs(command, {"equip", "装备"}) && command.args.size() == 3U)
        return equipPerson(command.args[0], command.args[1], command.args[2]);
    if (verbIs(command, {"unequip", "卸下"}) && command.args.size() == 2U)
        return unequipPerson(command.args[0], command.args[1]);
    if (verbIs(command, {"appoint", "任命"}) && command.args.size() == 2U)
        return appoint(command.args[0], command.args[1]);
    if (verbIs(command, {"unappoint", "卸任"}) && command.args.size() == 1U) return unappoint(command.args[0]);
    if (verbIs(command, {"treat", "医治"}) && command.args.size() == 1U) return treat(command.args[0]);
    if (verbIs(command, {"war", "战争"}) && command.args.size() == 1U &&
        equalsAny(command.args[0], {"targets", "目标"}))
        return {true, true, false, false, false, false, warTargetsText()};
    if (verbIs(command, {"power", "战力"}) && command.args.empty())
        return {true, true, false, false, false, false, powerText()};
    if (verbIs(command, {"garrison", "驻军"}) && command.args.size() == 2U) {
        int count = 0;
        const auto tribe = parseTribe(command.args[0]);
        return tribe && parseNonnegative(command.args[1], count) ? garrison(*tribe, count)
                                                                 : rejected("用法：garrison <部落> <战士数>。");
    }
    if (verbIs(command, {"event", "事件"})) {
        if (command.args.empty()) return {true, true, false, false, false, false, eventText(state_)};
        int option = 0;
        return command.args.size() == 1U && parseNonnegative(command.args[0], option) ? chooseEvent(option)
                                                                                      : rejected("用法：event <1|2>。");
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
        if (!parseNonnegative(command.args.front(), faction) || faction < 1 ||
            faction > static_cast<int>(kPlayerFactionCount))
            return rejected("派系编号为1至3。");
        return appeaseFaction(static_cast<std::size_t>(faction - 1));
    }
    if (verbIs(command, {"formarmy", "组建军队"}) && (command.args.size() == 2U || command.args.size() == 3U)) {
        int warriors = 0;
        int militia = 0;
        return parseNonnegative(command.args[command.args.size() - 2U], warriors) &&
                       parseNonnegative(command.args.back(), militia)
                   ? formArmy(warriors, militia, command.args.size() == 3U ? command.args[0] : "")
                   : rejected("用法：formarmy <统帅> <战士数> <民兵数>。");
    }
    if (verbIs(command, {"disbandarmy", "解散军队"}) && command.args.empty()) return disbandArmy();
    if (verbIs(command, {"war", "出征"}) && command.args.size() == 1U) {
        const auto tribe = parseTribe(command.args.front());
        return tribe ? startWar(*tribe) : rejected("未知部落。");
    }
    if (verbIs(command, {"endturn", "end", "结束回合"}) || command.verb == "8") {
        return command.args.empty() ? endSeason() : rejected("用法：endturn / 结束回合");
    }
    return {};
}

bool GameEngine::canSpendAction(ActionResult& result) const {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox) {
        result = rejected("当前阶段不能执行部落行动。");
        return false;
    }
    if (state_.actionsLeft <= 0) {
        result = rejected("本季小队行动点已经用完，请结束回合。");
        return false;
    }
    return true;
}

bool GameEngine::diplomacyUsedThisSeason(const TribeId tribe) const {
    const std::string marker = "本季外交：" + tribeName(tribe);
    return std::any_of(state_.chronicle.begin(), state_.chronicle.end(), [&](const ChronicleEntry& entry) {
        return entry.season == state_.season && entry.title == marker;
    });
}

ActionResult GameEngine::finalizeDiplomacy(const TribeId tribe, ActionResult result) {
    if (!result.success) return result;
    addChronicle(state_, 1, "本季外交：" + tribeName(tribe), "该部落本季的主动外交已经完成。");
    return result;
}

void GameEngine::spendAction(GameState& candidate) const { --candidate.actionsLeft; }

ActionResult GameEngine::build(const BuildingId building) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.buildings[indexOf(building)]) return rejected("该唯一建筑已经建成。");
    static const std::array<int, kBuildingCount> woodCosts{{8, 10, 8, 6, 8, 6}};
    static const std::array<int, kBuildingCount> stoneCosts{{2, 2, 6, 2, 4, 4}};
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
    spendAction(candidate);
    return commit(std::move(candidate), "建筑完成，部落稳定提高2。", true);
}

ActionResult GameEngine::research(const TechnologyId technology) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.technologies[indexOf(technology)]) return rejected("该技术已经研究完成。");
    const int raw = static_cast<int>(technology);
    const int tier = raw % 3;
    if (tier > 0 && !state_.technologies[static_cast<std::size_t>(raw - 1)])
        return rejected("必须先研究同路线的前一级技术。");
    if (tier == 2 && !state_.buildings[indexOf(BuildingId::Workshop)]) return rejected("高级技术需要先建武备工坊。");
    const int foodCost = 3 + tier * 2;
    const int woodCost = 2 + tier;
    if (state_.food < foodCost || state_.wood < woodCost) return rejected("研究所需食物或木材不足。");
    GameState candidate = state_;
    candidate.food -= foodCost;
    candidate.wood -= woodCost;
    candidate.technologies[indexOf(technology)] = true;
    spendAction(candidate);
    return commit(std::move(candidate), "研究完成，新的部落知识已经记录。", true);
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

ActionResult GameEngine::startMission(const ResourceKind resource) {
    return startMission(MissionKind::Gather, resource);
}

ActionResult GameEngine::startOutpostMission() {
    return startMission(MissionKind::OutpostConstruction, ResourceKind::Wood);
}

ActionResult GameEngine::startMission(const MissionKind kind, const ResourceKind resource) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.squads.empty()) return rejected("当前没有可出发的小队。");
    const bool constructionMission = kind == MissionKind::OutpostConstruction;
    const int crewSize = [&] {
        if (constructionMission) return std::max(state_.workforce.woodCrew, state_.workforce.stoneCrew);
        switch (resource) {
            case ResourceKind::Food:
            case ResourceKind::Hides:
                return state_.workforce.foodCrew;
            case ResourceKind::Wood:
                return state_.workforce.woodCrew;
            case ResourceKind::Stone:
                return state_.workforce.stoneCrew;
            case ResourceKind::Herbs:
                return state_.workforce.herbCrew;
            default:
                return 0;
        }
    }();
    if (constructionMission && (state_.workforce.woodCrew < 2 || state_.workforce.stoneCrew < 2)) {
        return rejected("前哨建设任务需要各至少2名木材队和石料队劳力。 ");
    }
    if (crewSize < 2) return rejected("该资源队至少需要分配2名劳力；先用 assign/分配 配置。 ");
    if (constructionMission && (state_.wood < 6 || state_.stone < 4)) {
        return rejected("前哨建设任务需要从仓库带走木材6、石料4。 ");
    }
    if (state_.squads.front().refusingOrders) return rejected("晨火队正在抗命，请先安抚派系。");
    if (state_.squads.front().fatigue >= 85) return rejected("晨火队过于疲劳，需要先休整。");

    GameState candidate = state_;
    const std::uint32_t missionSeed =
        candidate.seed + static_cast<std::uint32_t>(candidate.season * 97 + candidate.missionCount * 17);
    PermanentSquad& permanent = candidate.squads.front();
    ExpansionState missionState;
    missionState.seed = missionSeed;
    missionState.squad.name = permanent.name;
    missionState.squad.cohesion = 70;
    missionState.squad.members.clear();
    missionState.squad.members.reserve(permanent.members.size());
    for (const std::string& name : permanent.members) {
        const Character* character = findRosterCharacter(candidate.roster, name);
        if (character == nullptr || character->life <= 0) return rejected("小队成员缺失或已阵亡，任务未开始。");
        missionState.squad.members.push_back(*character);
    }
    const auto captain = std::find_if(missionState.squad.members.begin(), missionState.squad.members.end(),
                                      [&](const Character& character) { return character.name == permanent.captain; });
    if (captain == missionState.squad.members.end()) return rejected("长期小队的队长不在出发名单中。");
    missionState.squad.leaderIndex =
        static_cast<std::size_t>(std::distance(missionState.squad.members.begin(), captain));
    missionState.backpack = Inventory{80, 20};
    missionState.phase = ExpansionPhase::Exploring;
    missionState.settled = false;
    missionState.worldLocation = static_cast<int>(permanent.station);
    missionState.worldDiscovered = candidate.discovered;
    missionState.outposts = candidate.outposts;
    missionState.foodGatherBonus = (candidate.technologies[indexOf(TechnologyId::FoodPreservation)] ? 2 : 0) +
                                   (candidate.technologies[indexOf(TechnologyId::Irrigation)] ? 3 : 0);
    missionState.herbGatherBonus = candidate.technologies[indexOf(TechnologyId::HerbalKnowledge)] ? 2 : 0;
    missionState.missionKind = kind;
    missionState.assignedResource = resource;
    missionState.crewSize = crewSize;
    missionState.cargoCapacity = 16 + crewSize * 4;
    if (constructionMission) {
        missionState.cargoWood = 6;
        missionState.cargoStone = 4;
        candidate.wood -= 6;
        candidate.stone -= 4;
    }
    const OperationResult missionValid = ExpansionGame::validateState(missionState);
    if (!missionValid)
        return rejected("长期小队无法进入任务（凝聚力" + std::to_string(missionState.squad.cohesion) + "）：" +
                        missionValid.message);
    candidate.activeMission = std::move(missionState);
    candidate.phase = GamePhase::Mission;
    if (population_rules::committedPopulation(candidate) > population_rules::populationCapacity(candidate)) {
        return rejected("统一人口池不足：劳力、驻军、已组建军队和出任务小队合计不能超过人口-2。 ");
    }
    permanent.personallyDeployedThisSeason = true;
    spendAction(candidate);
    const std::string taskName = constructionMission ? "前哨建设" : resourceName(resource) + "采集";
    return commit(std::move(candidate),
                  "晨火队带领" + std::to_string(crewSize) + "名劳力从驻地进入十六地点地图，执行" + taskName + "任务。",
                  true);
}

ActionResult GameEngine::executeMission(const std::string_view input) {
    if (!state_.activeMission) return rejected("任务状态缺失，无法继续。");
    const ParsedCommand command = parseCommand(input);
    const bool diplomaticVerb =
        verbIs(command, {"talk",    "交谈", "gift",    "送礼", "trade",  "贸易", "openroute", "开通商路",
                         "marry",   "联姻", "tribute", "朝贡", "demand", "索贡", "ally",      "结盟",
                         "declare", "宣战", "truce",   "停战", "raid",   "劫掠"});
    if (diplomaticVerb) {
        const auto tribe = missionTribeAt(state_.activeMission->worldLocation);
        if (!tribe) return rejected("这里没有可接触的部落；请前往河鹿渡口、白羽营地、古老山隘、潮盐港或玄石工坊。 ");
        const bool isTrade = verbIs(command, {"trade", "贸易"});
        if ((isTrade && command.args.size() != 2U) || (!isTrade && !command.args.empty())) {
            return rejected(isTrade ? "地图贸易用法：trade <给出的资源> <换取的资源>。"
                                    : "地图外交由当前位置确定对象；该指令不需要部落名称。");
        }
        GameState proxyState = state_;
        proxyState.phase = GamePhase::Managing;
        proxyState.activeMission.reset();
        proxyState.actionsLeft = 1;
        proxyState.discovered = state_.activeMission->worldDiscovered;
        GameEngine proxy{std::move(proxyState)};
        std::string proxyInput = command.verb + " " + tribeCommandName(*tribe);
        if (isTrade) proxyInput += " " + command.args[0] + " " + command.args[1];
        ActionResult diplomatic = proxy.execute(proxyInput);
        if (!diplomatic.success) return diplomatic;

        GameState candidate = proxy.state();
        candidate.phase = GamePhase::Mission;
        candidate.actionsLeft = state_.actionsLeft;
        candidate.activeMission = state_.activeMission;
        ExpansionState& missionState = *candidate.activeMission;
        ++missionState.turn;
        for (Character& member : missionState.squad.members) member.fatigue = std::min(100, member.fatigue + 1);
        const std::string locationName = worldLocations()[static_cast<std::size_t>(missionState.worldLocation)].name;
        return commit(std::move(candidate), "在" + locationName + "进行外交：" + diplomatic.message, false);
    }
    ExpansionGame mission{*state_.activeMission};
    const ExpansionCommandResult result = mission.execute(input);
    if (!result.recognized) return {};
    if (!result.success) return rejected(result.message);

    GameState candidate = state_;
    candidate.activeMission = mission.state();
    std::string message = result.message;
    if (mission.state().phase == ExpansionPhase::Settled) {
        const ExpansionState& settled = mission.state();
        ++candidate.missionCount;
        PermanentSquad& squad = candidate.squads.front();
        candidate.discovered = settled.worldDiscovered;
        candidate.outposts = settled.outposts;
        candidate.food += settled.cargoFood;
        candidate.wood += settled.cargoWood;
        candidate.stone += settled.cargoStone;
        candidate.herbs += settled.cargoHerbs;
        candidate.hides += settled.cargoHides;
        squad.station = static_cast<WorldLocationId>(settled.worldLocation);
        for (const Item& item : settled.backpack.items()) candidate.stockpile.push_back(item);
        const std::vector<std::size_t> originalSquadSizes = [&candidate] {
            std::vector<std::size_t> sizes;
            sizes.reserve(candidate.squads.size());
            for (const PermanentSquad& permanent : candidate.squads) sizes.push_back(permanent.members.size());
            return sizes;
        }();

        std::unordered_set<std::string> deployedNames;
        std::unordered_set<std::string> deadNames;
        for (const Character& member : settled.squad.members) {
            Character* permanent = findRosterCharacter(candidate.roster, member.name);
            if (permanent == nullptr || !deployedNames.insert(member.name).second) {
                return rejected("任务成员与长期角色名单不一致，回营结算已原子取消。");
            }
            if (member.life <= 0)
                deadNames.insert(member.name);
            else
                *permanent = member;
        }
        squad.eliteExperience += 10 + settled.harvestActions * 5;

        bool coreSquadLost = false;
        if (!deadNames.empty()) {
            candidate.roster.erase(
                std::remove_if(candidate.roster.begin(), candidate.roster.end(),
                               [&](const Character& character) { return deadNames.count(character.name) != 0U; }),
                candidate.roster.end());

            const int deaths = static_cast<int>(deadNames.size());
            candidate.missionDeaths += deaths;
            candidate.population = std::max(0, candidate.population - deaths);
            candidate.warriors = std::min(candidate.warriors, candidate.population);
            candidate.stability = std::max(0, candidate.stability - 8);
            message += " 本次共阵亡" + std::to_string(deaths) + "人，长期名单与小队编制已同步。";
            addChronicle(candidate, 3, "地图任务伤亡", "本次任务阵亡" + std::to_string(deaths) + "人。");

            for (std::size_t index = 0; index < candidate.squads.size(); ++index) {
                PermanentSquad& permanent = candidate.squads[index];
                permanent.members.erase(std::remove_if(permanent.members.begin(), permanent.members.end(),
                                                       [&](const std::string& name) {
                                                           const Character* character =
                                                               findRosterCharacter(candidate.roster, name);
                                                           return !character || character->life <= 0;
                                                       }),
                                        permanent.members.end());

                const std::size_t targetSize = std::min(originalSquadSizes[index], kMaximumSquadSize);
                for (const Character& reserve : candidate.roster) {
                    if (permanent.members.size() >= targetSize) break;
                    if (reserve.life > 0 && std::find(permanent.members.begin(), permanent.members.end(),
                                                      reserve.name) == permanent.members.end()) {
                        permanent.members.push_back(reserve.name);
                    }
                }
                if (permanent.members.size() < kMinimumSquadSize) {
                    coreSquadLost = true;
                    permanent.members.clear();
                    permanent.captain.clear();
                    continue;
                }
                if (std::find(permanent.members.begin(), permanent.members.end(), permanent.captain) ==
                    permanent.members.end()) {
                    permanent.captain = permanent.members.front();
                }
            }
            candidate.squads.erase(
                std::remove_if(candidate.squads.begin(), candidate.squads.end(),
                               [](const PermanentSquad& permanent) { return permanent.members.empty(); }),
                candidate.squads.end());
        }

        for (PermanentSquad& permanent : candidate.squads) {
            permanent.fatigue = permanentSquadFatigue(permanent, candidate.roster);
        }
        candidate.highestLevel = 1;
        for (const Character& member : candidate.roster) {
            candidate.highestLevel = std::max(candidate.highestLevel, member.level);
        }

        message += " 地图任务载货及任务背包已并入部落库存，小队驻地已更新。";
        addChronicle(
            candidate, 2, "地图任务结算",
            "晨火队在" + worldLocations()[static_cast<std::size_t>(settled.worldLocation)].name + "完成结算并驻留。");
        candidate.activeMission.reset();
        if (coreSquadLost && candidate.squads.empty()) {
            candidate.campDurability = 0;
            candidate.actionsLeft = 0;
            candidate.phase = GamePhase::Finished;
            candidate.ending = GameEnding::Extinction;
            message += " 没有足够的存活骨干重建小队，营地在混乱中瓦解，进入部落覆灭结算。";
            addChronicle(candidate, 5, "部落覆灭",
                         "地图任务重创后已无足够存活骨干维持营地。早先发生的伤亡不会被回滚。");
        } else {
            candidate.phase = GamePhase::Managing;
        }
        const bool endingReached = coreSquadLost && candidate.squads.empty();
        return commit(std::move(candidate), std::move(message), false, false, endingReached);
    }
    return commit(std::move(candidate), std::move(message), false);
}

ActionResult GameEngine::talk(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能普通交谈，请先谈停战。");
    GameState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::SharedLanguage)] ? 9 : 5;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 4);
    spendAction(candidate);
    return finalizeDiplomacy(
        tribe, commit(std::move(candidate),
                      "与" + tribeName(tribe) + "交谈：关系+" + std::to_string(bonus) + "，信任+4。", true));
}

ActionResult GameEngine::gift(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能送礼，请先谈停战。");
    if (state_.food < 4) return rejected("送礼需要4食物。");
    GameState candidate = state_;
    candidate.food -= 4;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::GiftCustoms)] ? 14 : 9;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 6);
    spendAction(candidate);
    return finalizeDiplomacy(
        tribe,
        commit(std::move(candidate), "向" + tribeName(tribe) + "送礼：关系+" + std::to_string(bonus) + "。", true));
}

int GameEngine::resourceValue(const GameState& state, const ResourceKind resource) const {
    const int amount = resource == ResourceKind::Food    ? state.food
                       : resource == ResourceKind::Wood  ? state.wood
                       : resource == ResourceKind::Stone ? state.stone
                       : resource == ResourceKind::Herbs ? state.herbs
                                                         : state.hides;
    const int base = resource == ResourceKind::Food    ? 3
                     : resource == ResourceKind::Wood  ? 2
                     : resource == ResourceKind::Stone ? 4
                     : resource == ResourceKind::Herbs ? 5
                                                       : 4;
    return std::max(1, base + (amount < 10 ? 3 : amount < 20 ? 1 : 0));
}

int& GameEngine::resourceRef(GameState& state, const ResourceKind resource) const {
    switch (resource) {
        case ResourceKind::Food:
            return state.food;
        case ResourceKind::Wood:
            return state.wood;
        case ResourceKind::Stone:
            return state.stone;
        case ResourceKind::Herbs:
            return state.herbs;
        case ResourceKind::Hides:
            return state.hides;
    }
    return state.food;
}

ActionResult GameEngine::trade(const TribeId tribe, const ResourceKind offered, const ResourceKind requested) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点，不能贸易。");
    if (offered == requested) return rejected("以物易物必须选择两种不同资源。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.atWar) return rejected("战争中不能贸易。");
    if (relation.relation < -10) return rejected("关系过低，对方拒绝贸易。");
    const int offeredAmount = 4;
    if (resourceRef(state_, offered) < offeredAmount) return rejected("给出的资源不足。");

    GameState candidate = state_;
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
    spendAction(candidate);
    std::string message = "与" + tribeName(tribe) + "以" + std::to_string(offeredAmount) + resourceName(offered) +
                          "换得" + std::to_string(requestedAmount) + resourceName(requested) +
                          "。价格受稀缺、关系和依赖影响。";
    return finalizeDiplomacy(tribe, commit(std::move(candidate), std::move(message), true));
}

ActionResult GameEngine::openTradeRoute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    auto required = WorldLocationId::MountainMarket;
    if (tribe == TribeId::RiverDeer)
        required = WorldLocationId::RiverFord;
    else if (tribe == TribeId::WhiteFeather)
        required = WorldLocationId::WhiteFeatherCamp;
    else if (tribe == TribeId::Rockfang)
        required = WorldLocationId::OldPass;
    else if (tribe == TribeId::Tidesalt)
        required = WorldLocationId::TidesaltHarbor;
    else if (tribe == TribeId::Blackstone)
        required = WorldLocationId::BlackstoneWorkshop;
    if (!locationDiscovered(state_, required)) return rejected("尚未发现连接该部落的贸易地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能开通商路。");
    if (state_.relations[indexOf(tribe)].tradeDependence < 16) return rejected("至少先完成两次有效贸易，建立依赖。");
    if (state_.relations[indexOf(tribe)].tradeRoute) return rejected("该商路已经开通。");
    GameState candidate = state_;
    candidate.relations[indexOf(tribe)].tradeRoute = true;
    candidate.stability = std::min(100, candidate.stability + 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "开通商路", state_.tribeName + "与" + tribeName(tribe) + "建立稳定商路。");
    return finalizeDiplomacy(tribe, commit(std::move(candidate), "商路开通，稳定+3，后续贸易更可靠。", true));
}

ActionResult GameEngine::marriage(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.marriage) return rejected("双方已经存在联姻关系。");
    if (relation.atWar || relation.relation < 60 || relation.trust < 50)
        return rejected("联姻需要关系60、信任50且不在战争中。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.marriage = true;
    changed.relation = relationClamp(changed.relation + 15);
    changed.trust = percentClamp(changed.trust + 10);
    candidate.stability = std::min(100, candidate.stability + 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "联姻",
                 "具名使者在共同火坛前交换信物，也留下继承争议的可能。");
    return finalizeDiplomacy(tribe, commit(std::move(candidate), "联姻完成：关系+15、信任+10、稳定+4。", true));
}

ActionResult GameEngine::offerTribute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能直接建立朝贡，请先停战。");
    if (state_.relations[indexOf(tribe)].otherPaysTribute) return rejected("对方正在向我方进贡，不能同时双向朝贡。");
    if (state_.relations[indexOf(tribe)].alliance) return rejected("盟友之间不能建立屈从式朝贡关系。");
    if (state_.food < 6) return rejected("建立朝贡需要先交6食物。");
    if (state_.relations[indexOf(tribe)].playerPaysTribute) return rejected("已经向该部落朝贡。");
    GameState candidate = state_;
    candidate.food -= 6;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.playerPaysTribute = true;
    relation.relation = relationClamp(relation.relation + 12);
    relation.fear = percentClamp(relation.fear - 5);
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    return finalizeDiplomacy(
        tribe, commit(std::move(candidate), "建立朝贡：换取和平，但内部稳定-3，每季继续支付2食物。", true));
}

ActionResult GameEngine::demandTribute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.atWar) return rejected("战争中不能直接索贡，请先结束战斗并停战。");
    if (relation.playerPaysTribute) return rejected("我方正在朝贡，不能同时要求对方进贡。");
    if (relation.alliance || relation.marriage) return rejected("联盟或联姻关系下不能强行索贡。");
    if (relation.otherPaysTribute) return rejected("对方已经进贡。");
    if (relation.fear < 60 || state_.warriors < 6) return rejected("索贡需要恐惧60且至少6名战士。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.otherPaysTribute = true;
    changed.relation = relationClamp(changed.relation - 12);
    candidate.stability = std::max(0, candidate.stability - 2);
    spendAction(candidate);
    return finalizeDiplomacy(tribe,
                             commit(std::move(candidate), "对方同意每季进贡2食物，但关系和内部公平感下降。", true));
}

ActionResult GameEngine::alliance(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.alliance) return rejected("双方已经结盟。");
    if (relation.atWar || relation.relation < 70 || relation.trust < 60)
        return rejected("结盟需要关系70、信任60且不在战争中。");
    if (!state_.technologies[indexOf(TechnologyId::Confederation)]) return rejected("需要研究部落联盟技术。");
    GameState candidate = state_;
    candidate.relations[indexOf(tribe)].alliance = true;
    candidate.stability = std::min(100, candidate.stability + 5);
    spendAction(candidate);
    addChronicle(candidate, 4, "与" + tribeName(tribe) + "结盟", "双方在共同火坛前立誓互助。");
    return finalizeDiplomacy(tribe, commit(std::move(candidate), "联盟成立，稳定+5。", true));
}

ActionResult GameEngine::declareWar(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    auto relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点，不能宣战。");
    if (relation.atWar) return rejected("双方已经处于战争状态。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.atWar = true;
    changed.truce = false;
    changed.alliance = false;
    changed.marriage = false;
    changed.tradeRoute = false;
    changed.playerPaysTribute = false;
    changed.otherPaysTribute = false;
    changed.relation = relationClamp(changed.relation - 35);
    candidate.stability = std::max(0, candidate.stability - 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "向" + tribeName(tribe) + "宣战", "战鼓响起，族人开始准备长期代价。");
    return finalizeDiplomacy(tribe,
                             commit(std::move(candidate), "宣战生效：关系-35、稳定-4。请组建军队后出征。", true));
}

ActionResult GameEngine::negotiateTruce(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!state_.relations[indexOf(tribe)].atWar) return rejected("双方并未交战。");
    if (state_.food < 5) return rejected("停战谈判需要5食物作为赔偿和宴席。");
    GameState candidate = state_;
    candidate.food -= 5;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.atWar = false;
    relation.truce = true;
    relation.relation = std::max(-30, relation.relation);
    relation.trust = std::max(10, relation.trust);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "停战", "双方同意暂时放下武器，伤痕仍未消失。");
    return finalizeDiplomacy(tribe, commit(std::move(candidate), "停战达成，战争状态解除。", true));
}

ActionResult GameEngine::raid(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现通往该部落的道路，不能劫掠。");
    if (state_.warriors < 2) return rejected("劫掠至少需要2名战士。");
    GameState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int gain = 5 + static_cast<int>((candidate.seed + candidate.season + indexOf(tribe)) % 4U);
    candidate.food += gain;
    relation.relation = relationClamp(relation.relation - 25);
    relation.fear = percentClamp(relation.fear + 15);
    relation.trust = percentClamp(relation.trust - 12);
    relation.alliance = false;
    relation.marriage = false;
    relation.tradeRoute = false;
    relation.playerPaysTribute = false;
    relation.otherPaysTribute = false;
    relation.truce = false;
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "劫掠" + tribeName(tribe), "获得食物" + std::to_string(gain) + "，也播下新的仇恨。");
    return finalizeDiplomacy(
        tribe,
        commit(std::move(candidate), "劫掠获得" + std::to_string(gain) + "食物；关系-25、恐惧+15、稳定-3。", true));
}

ActionResult GameEngine::appeaseFaction(const std::size_t faction) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.food < 4) return rejected("安抚派系需要公平分配4食物。");
    GameState candidate = state_;
    candidate.food -= 4;
    FactionState& changed = candidate.playerFactions[faction];
    changed.satisfaction = std::min(100, changed.satisfaction + 20);
    changed.crisis = static_cast<FactionCrisis>(std::max(0, static_cast<int>(changed.crisis) - 2));
    candidate.stability = std::min(100, candidate.stability + 8);
    const bool refusalRemains =
        std::any_of(candidate.playerFactions.begin(), candidate.playerFactions.end(),
                    [](const FactionState& state) { return state.crisis == FactionCrisis::Refusal; });
    for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = refusalRemains;
    spendAction(candidate);
    const std::string message = "公平分配缓和了" + changed.name + "的不满：满意+20、稳定+8。";
    return commit(std::move(candidate), message, true);
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

void GameEngine::settleFoodAndTribute(GameState& candidate, std::string& message) const {
    int consumption = (candidate.population + 1) / 2;
    if ((candidate.season - 1) % 4 == 3) consumption += 3;
    if (candidate.buildings[indexOf(BuildingId::Granary)] && (candidate.season - 1) % 4 == 3)
        consumption = std::max(0, consumption - 2);
    for (std::size_t index = 1; index < kTribeCount; ++index) {
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
        candidate.morale = std::max(0, candidate.morale - 12);
        candidate.stability = std::max(0, candidate.stability - 15);
        message += " 食物不足，人口-" + std::to_string(loss) + "、士气-12、稳定-15。";
    }
}

void GameEngine::settleAutonomousTribes(GameState& candidate, std::string& message) const {
    const std::size_t index =
        1U + static_cast<std::size_t>((candidate.seed + static_cast<std::uint32_t>(candidate.season * 13)) % 5U);
    auto& relation = candidate.relations[index];
    TribeProfile& profile = candidate.tribes[index];
    const TribeId tribe = static_cast<TribeId>(index);
    const FactionState& faction = dominantFaction(profile);
    const int choice =
        static_cast<int>((candidate.seed * 3U + static_cast<std::uint32_t>(candidate.season * 7 + index)) % 4U);

    const bool tradeDriven = containsAny(profile.personality, {"农业", "航运", "工艺", "精明", "务实"}) ||
                             containsAny(faction.demand, {"粮食", "贸易", "航路", "盐价", "换取", "矿路", "装备"});
    const bool conciliatory = containsAny(profile.personality, {"谨慎", "救助", "温和"}) ||
                              containsAny(faction.demand, {"救助", "伤者", "共享", "情报", "停战", "谈判"});
    const bool aggressive = containsAny(profile.personality, {"强硬", "好战", "武勇"}) ||
                            containsAny(faction.demand, {"战利品", "控制", "征服", "复仇"});

    const bool contacted = locationDiscovered(candidate, contactLocation(tribe));
    const std::string reason = contacted
                                   ? " " + profile.name + "首领" + profile.leader + "秉持“" + profile.personality +
                                         "”，主导派系" + faction.name + "要求“" + faction.demand + "”；"
                                   : " 一个尚未正式接触的部落受其首领取向和内部派系诉求推动；";
    if (candidate.workforce.envoys > 0 && contacted && !relation.atWar) {
        relation.relation = relationClamp(relation.relation + 1);
        relation.trust = percentClamp(relation.trust + 1);
        message += reason + "使者维持往来，关系+1、信任+1。";
    } else if (relation.atWar) {
        relation.fear = percentClamp(relation.fear + 2);
        message += reason + "双方仍处战争，因此优先集结兵力，恐惧+2。";
    } else if (relation.tradeRoute && (tradeDriven || choice <= 1)) {
        candidate.food += 2;
        relation.tradeDependence = percentClamp(relation.tradeDependence + 2);
        message +=
            reason + "双方关系" + std::to_string(relation.relation) + "且固定商路畅通，因此商队送来2食物，贸易依赖+2。";
    } else if (relation.relation < 0 && (aggressive || choice == 2)) {
        relation.fear = percentClamp(relation.fear + 4);
        const bool towerWarning =
            candidate.buildings[indexOf(BuildingId::Watchtower)] && candidate.workforce.scouts > 0;
        const int protection = (candidate.workforce.campGuards > 0 ? 1 : 0) + (candidate.workforce.scouts > 0 ? 1 : 0) +
                               (towerWarning ? 1 : 0);
        const int damage = std::max(0, 2 - protection);
        candidate.campDurability = std::max(0, candidate.campDurability - damage);
        message += reason + "双方关系仅" + std::to_string(relation.relation) +
                   "且尚无固定商路，因此发动边境骚扰，恐惧+4、营地耐久-" + std::to_string(damage) +
                   (towerWarning ? "；瞭望塔提前预警，额外抵消1点损失。" : "。");
    } else if (conciliatory || tradeDriven || relation.relation >= 15 || relation.trust >= 20 || choice == 0) {
        const int relationBefore = relation.relation;
        const int trustBefore = relation.trust;
        relation.relation = relationClamp(relation.relation + 3);
        relation.trust = percentClamp(relation.trust + 2);
        message += reason + "考虑当前关系" + std::to_string(relationBefore) + "、信任" + std::to_string(trustBefore) +
                   "且尚无固定商路，因此派来使者，关系+3、信任+2。";
    } else {
        relation.relation = relationClamp(relation.relation - 2);
        message += reason + "当前信任" + std::to_string(relation.trust) + "且尚无固定商路，因此暂时疏远，关系-2。";
    }

    if (candidate.season % 8 == 0 && index != indexOf(TribeId::Player)) {
        profile.actingLeader = profile.successor;
        profile.leader = profile.successor;
        message += " " + profile.name + "首领更替为" + profile.leader + "。";
        addChronicle(candidate, 3, profile.name + "首领更替", profile.successor + "在派系推举下接掌部落。");
    }
}

void GameEngine::settleFactions(GameState& candidate, std::string& message) const {
    const bool councilRelief = candidate.buildings[indexOf(BuildingId::CouncilFire)] && candidate.workforce.envoys > 0;
    const bool councilReliefApplied = councilRelief && (candidate.food == 0 || candidate.stability < 40);
    for (FactionState& faction : candidate.playerFactions) {
        const int basePressure = candidate.food == 0 ? 12 : candidate.stability < 40 ? 8 : -2;
        const int pressure = councilReliefApplied ? std::max(0, basePressure - 2) : basePressure;
        faction.satisfaction = percentClamp(faction.satisfaction - pressure);
        int stage = static_cast<int>(faction.crisis);
        if (faction.satisfaction < 25 || candidate.stability < 25)
            stage = std::min(5, stage + 1);
        else if (faction.satisfaction >= 55 && stage > 0)
            --stage;
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
            candidate.tribes[indexOf(TribeId::Player)].leader = faction.candidate;
            candidate.leadershipHistory.push_back(faction.candidate + "（派系政变接任）");
            candidate.stability = 35;
            faction.satisfaction = 50;
            faction.crisis = FactionCrisis::Complaint;
            message += " " + faction.name + "发动政变，" + faction.candidate + "成为代理首领。";
            addChronicle(candidate, 4, "部落政变", faction.candidate + "在危机中接替原首领。");
        }
    }
    if (councilReliefApplied) message += " 议事火坛由使者主持，派系满意流失-2。";
    const bool refusing =
        std::any_of(candidate.playerFactions.begin(), candidate.playerFactions.end(),
                    [](const FactionState& faction) { return faction.crisis == FactionCrisis::Refusal; });
    for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = refusing;
}

void GameEngine::settleEvent(GameState& candidate, std::string& message) const {
    (void)candidate;
    (void)message;
}

void GameEngine::finishExtinction(GameState& candidate, std::string& message) const {
    if (candidate.population > 0 && candidate.campDurability > 0) return;
    candidate.phase = GamePhase::Finished;
    candidate.ending = GameEnding::Extinction;
    candidate.activeMission.reset();
    releaseWarEquipment(candidate, true);
    candidate.war = {};
    message += " 部落人口或营地耐久归零，燧火熄灭。";
    addChronicle(candidate, 5, "部落覆灭", "最后的火坛在风中熄灭。");
}

ActionResult GameEngine::endSeason() {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox)
        return rejected("当前不能结束季节。");
    GameState candidate = state_;
    std::string message = "第" + std::to_string(candidate.season) + "季结算：";
    settleFoodAndTribute(candidate, message);
    settleAutonomousTribes(candidate, message);
    settleFactions(candidate, message);
    settleEvent(candidate, message);
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        OccupationState& site = candidate.occupations[index];
        if (!site.occupied) continue;
        const int required = index == indexOf(TribeId::Rockfang) || index == indexOf(TribeId::Blackstone) ? 4 : 2;
        if (site.garrison < required) {
            ++site.unrest;
            message += " " + tribeName(static_cast<TribeId>(index)) + "据点驻军不足，动乱+1。";
        } else {
            site.unrest = std::max(0, site.unrest - 1);
            candidate.food += 2;
        }
        if (site.unrest >= 3) {
            site = {};
            candidate.stability = std::max(0, candidate.stability - 6);
            message += " 据点反抗成功，失去占领并稳定-6。";
        }
    }
    for (std::size_t index = 1; index < kWorldLocationCount; ++index)
        if (candidate.outposts[index]) {
            if (candidate.workforce.outpostGuards[index] == 0)
                ++candidate.workforce.outpostIdleSeasons[index];
            else
                candidate.workforce.outpostIdleSeasons[index] = 0;
            const bool occupiedBySquad =
                std::any_of(candidate.squads.begin(), candidate.squads.end(),
                            [&](const PermanentSquad& squad) { return indexOf(squad.station) == index; });
            if (candidate.workforce.outpostIdleSeasons[index] >= 2 && !occupiedBySquad) {
                candidate.outposts[index] = false;
                candidate.workforce.outpostIdleSeasons[index] = 0;
                message += " 一座无人前哨荒废。";
            }
        }
    if (!candidate.pendingEvent.active) {
        const int kind = static_cast<int>((candidate.seed + candidate.season) % 4U);
        candidate.pendingEvent.active = true;
        candidate.pendingEvent.kind = static_cast<PendingEventKind>(kind);
        message += " 新的必须抉择事件已出现（输入 event 查看）。";
    }
    finishExtinction(candidate, message);
    if (candidate.phase == GamePhase::Finished)
        return commit(std::move(candidate), std::move(message), false, true, true);

    if (candidate.season >= candidate.seasonLimit && candidate.phase != GamePhase::Sandbox) {
        candidate.phase = GamePhase::EndingChoice;
        candidate.actionsLeft = 0;
        candidate.longModeFinalShown = candidate.mode == GameMode::Long;
        addChronicle(candidate, 4, "时代结算", "族人围绕火坛讨论已经满足的道路。");
        message += " 已到达模式结算季，请查看目标并选择结局。";
        return commit(std::move(candidate), std::move(message), false, true, false);
    }

    ++candidate.season;
    candidate.actionsLeft = availableTeams(candidate);
    for (PermanentSquad& squad : candidate.squads) squad.personallyDeployedThisSeason = false;
    return commit(std::move(candidate), std::move(message), false, true, false);
}

std::vector<GameEnding> GameEngine::availableEndings() const {
    std::vector<GameEnding> endings;
    if (state_.population <= 0 || state_.campDurability <= 0) return {GameEnding::Extinction};
    int allies = 0;
    for (std::size_t index = 1; index < kTribeCount; ++index) allies += state_.relations[index].alliance ? 1 : 0;
    if (allies >= 2 && state_.relations[indexOf(TribeId::RiverDeer)].relation >= 70 &&
        state_.relations[indexOf(TribeId::WhiteFeather)].relation >= 70 &&
        state_.technologies[indexOf(TechnologyId::Confederation)])
        endings.push_back(GameEnding::Alliance);
    const int occupied = static_cast<int>(std::count_if(state_.occupations.begin() + 1, state_.occupations.end(),
                                                        [](const OccupationState& site) { return site.occupied; }));
    if (occupied >= 2 && state_.warriors >= 5 && state_.morale >= 55) endings.push_back(GameEnding::Conquest);
    if (state_.population >= 20 && state_.food >= 40 && countTrue(state_.buildings) >= 4 &&
        countTrue(state_.technologies) >= 4)
        endings.push_back(GameEnding::Prosperity);
    endings.push_back(GameEnding::Migration);
    return endings;
}

ActionResult GameEngine::chooseEnding(const GameEnding ending) {
    if (state_.phase != GamePhase::EndingChoice) return rejected("现在还不能选择结局。");
    const auto endings = availableEndings();
    if (std::find(endings.begin(), endings.end(), ending) == endings.end())
        return rejected("当前条件尚未满足该结局道路。");
    GameState candidate = state_;
    candidate.ending = ending;
    candidate.phase = GamePhase::Finished;
    addChronicle(candidate, 5, endingName(ending), "族人共同选择了这条道路。");
    return commit(std::move(candidate), "结局已确定：" + endingName(ending) + "。进入独立结算画面。", false, false,
                  true);
}

ActionResult GameEngine::continueSandbox() {
    if (state_.phase != GamePhase::Finished || state_.mode != GameMode::Long || state_.ending == GameEnding::Extinction)
        return rejected("只有长期模式非覆灭结局可以继续沙盒。");
    GameState candidate = state_;
    candidate.phase = GamePhase::Sandbox;
    candidate.actionsLeft = availableTeams(candidate);
    ++candidate.season;
    return commit(std::move(candidate), "进入结局后的自由沙盒，部落可以继续经营。", false);
}

void GameEngine::releaseWarEquipment(GameState& candidate, const bool damaged) const {
    war_rules::releaseLockedEquipment(candidate, damaged);
}

ActionResult GameEngine::assignWorkforce(const WorkforceRole role, const int count) {
    if (count < 0 || count > 6) return rejected("每个岗位人数为0至6；资源队非零时至少2人。 ");
    if ((role == WorkforceRole::FoodCrew || role == WorkforceRole::WoodCrew || role == WorkforceRole::StoneCrew ||
         role == WorkforceRole::HerbCrew) &&
        count == 1)
        return rejected("资源队必须配置2至6人，或设为0。 ");
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
    }
    const int previous = *target;
    if (state_.workforceReassignmentRequired && count >= previous) {
        return rejected("劳力待重分配时只能降低岗位人数。 ");
    }
    *target = count;
    return commit(std::move(candidate), "劳力分配已调整；下季行动容量会按已配置资源队重新计算。", false);
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
    Item item;
    item.id = std::string(found->key) + "_" + std::to_string(candidate.season) + "_" +
              std::to_string(candidate.stockpile.size() + 1U);
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

ActionResult GameEngine::chooseEvent(const int option) {
    GameState candidate = state_;
    const seasonal_event_rules::EventResolution resolution = seasonal_event_rules::resolveChoice(candidate, option);
    if (!resolution.success) return rejected(resolution.message);
    std::string outcome = resolution.message;
    finishExtinction(candidate, outcome);
    return commit(std::move(candidate), "事件抉择已执行：" + outcome, false);
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

ActionResult GameEngine::commit(GameState candidate, std::string message, const bool consumesAction,
                                const bool seasonAdvanced, const bool endingReached) {
    population_rules::refreshWorkforceReassignment(candidate);
    std::string error;
    if (!validateState(candidate, error)) return rejected("行动后的状态未通过校验，已原子取消：" + error);
    state_ = std::move(candidate);
    return {true, true, true, consumesAction, seasonAdvanced, endingReached, std::move(message)};
}

ActionResult GameEngine::rejected(std::string message) const {
    return {true, false, false, false, false, false, std::move(message)};
}

void GameEngine::addChronicle(GameState& candidate, const int importance, std::string title, std::string detail) const {
    candidate.chronicle.push_back(
        {candidate.season, std::clamp(importance, 1, 5), std::move(title), std::move(detail)});
    if (candidate.chronicle.size() > 200U) candidate.chronicle.erase(candidate.chronicle.begin());
}

int GameEngine::availableTeams(const GameState& state) const {
    const WorkforceState& work = state.workforce;
    const int configuredCrews = (work.foodCrew >= 2 ? 1 : 0) + (work.woodCrew >= 2 ? 1 : 0) +
                                (work.stoneCrew >= 2 ? 1 : 0) + (work.herbCrew >= 2 ? 1 : 0);
    return std::min(7, 3 + configuredCrews);
}

WorldLocationId GameEngine::contactLocation(const TribeId tribe) const {
    switch (tribe) {
        case TribeId::RiverDeer:
            return WorldLocationId::RiverFord;
        case TribeId::WhiteFeather:
            return WorldLocationId::WhiteFeatherCamp;
        case TribeId::Rockfang:
            return WorldLocationId::OldPass;
        case TribeId::Tidesalt:
            return WorldLocationId::TidesaltHarbor;
        case TribeId::Blackstone:
            return WorldLocationId::BlackstoneWorkshop;
        case TribeId::Player:
        case TribeId::Count:
            return WorldLocationId::MountainMarket;
    }
    return WorldLocationId::MountainMarket;
}

bool GameEngine::locationDiscovered(const GameState& state, const WorldLocationId location) const {
    return state.discovered[indexOf(location)];
}

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
        candidate.seasonLimit > 10000 || candidate.actionsLeft < 0 || candidate.actionsLeft > 7) {
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
    if (candidate.warriors > candidate.population) {
        error = "战士人数不能超过部落人口。";
        return false;
    }
    if (!population_rules::validateWorkforce(candidate.workforce, error)) return false;
    const int overage = population_rules::populationOverage(candidate);
    if (candidate.workforceReassignmentRequired != (overage > 0)) {
        error = "劳力待重分配标记与统一人口池不一致。";
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
        for (const auto& item : character.equipment)
            if (item && (!itemOwners.insert(item->id).second || item->condition == ItemCondition::Scrapped)) {
                error = "装备物品编号重复或报废装备仍被穿戴。";
                return false;
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
        if (item.id.empty() || item.name.empty() || !itemOwners.insert(item.id).second) {
            error = "共享仓库物品编号重复或所有权冲突。";
            return false;
        }
    }
    for (const Item& item : candidate.war.lockedEquipment) {
        if (item.id.empty() || item.condition == ItemCondition::Scrapped || !itemOwners.insert(item.id).second) {
            error = "军队锁定装备与其他位置冲突。";
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
        if (candidate.squads.empty() ||
            candidate.activeMission->squad.members.size() != candidate.squads.front().members.size()) {
            error = "活动地图任务与永久小队不一致。";
            return false;
        }
        for (const Character& member : candidate.activeMission->squad.members) {
            if (std::find(candidate.squads.front().members.begin(), candidate.squads.front().members.end(),
                          member.name) == candidate.squads.front().members.end() ||
                findRosterCharacter(candidate.roster, member.name) == nullptr) {
                error = "活动地图任务成员不属于出发小队。";
                return false;
            }
        }
        for (const Item& item : candidate.activeMission->backpack.items()) {
            if (item.id.empty() || item.condition == ItemCondition::Scrapped || !itemOwners.insert(item.id).second) {
                error = "任务背包装备与其他位置冲突。";
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

std::string GameEngine::statusText() const {
    std::ostringstream output;
    output << "部落战役  模式：" << modeName(state_.mode) << "  季节：" << state_.season << "/" << state_.seasonLimit
           << "  阶段：" << phaseName(state_.phase) << "  行动点：" << state_.actionsLeft << "\n"
           << "部落：" << state_.tribeName << "  首领：" << state_.leaderName;
    if (!state_.actingLeaderName.empty()) output << "（代理/继任：" << state_.actingLeaderName << "）";
    output << "  稳定：" << state_.stability << "  士气：" << state_.morale << "\n"
           << "人口：" << state_.population << "  食物：" << state_.food << "  木材：" << state_.wood << "  石料："
           << state_.stone << "  草药：" << state_.herbs << "  兽皮：" << state_.hides << "  战士：" << state_.warriors
           << "\n"
           << "营地耐久：" << state_.campDurability << "  贸易次数：" << state_.tradeCount << "\n"
           << "统一人口池：已占用" << population_rules::committedPopulation(state_) << '/'
           << population_rules::populationCapacity(state_)
           << "（劳力、前哨、驻军、军队、出任务小队；首领与基础留守2人不分配）";
    if (state_.workforceReassignmentRequired) output << " [劳力待重分配]";
    output << "\n"
           << "建筑：" << countTrue(state_.buildings) << "/6  技术：" << countTrue(state_.technologies)
           << "/9  已发现地点：" << countTrue(state_.discovered) << "/16  战争胜负：" << state_.warsWon << "/"
           << state_.warsLost;
    return output.str();
}

std::string GameEngine::workforceText() const {
    const WorkforceState& w = state_.workforce;
    std::ostringstream out;
    out << "劳力分工（资源只能由地图任务带回；所有岗位均占用统一人口池）\n"
        << "食物队" << w.foodCrew << " 木材队" << w.woodCrew << " 石料队" << w.stoneCrew << " 草药队" << w.herbCrew
        << "\n支持岗位（0未配置/1已配置）：工匠" << (w.crafters > 0 ? 1 : 0) << " 医者" << (w.healers > 0 ? 1 : 0)
        << " 侦察" << (w.scouts > 0 ? 1 : 0) << " 使者" << (w.envoys > 0 ? 1 : 0) << " 营地守卫"
        << (w.campGuards > 0 ? 1 : 0) << "\n前哨守卫：";
    bool hasOutpostGuard = false;
    for (std::size_t index = 1; index < kWorldLocationCount; ++index) {
        if (!state_.outposts[index]) continue;
        out << worldLocations()[index].name << w.outpostGuards[index] << ' ';
        hasOutpostGuard = true;
    }
    if (!hasOutpostGuard) out << "无";
    out << "\n人口占用：" << population_rules::committedPopulation(state_) << '/'
        << population_rules::populationCapacity(state_) << "；当前/下季行动容量：" << state_.actionsLeft << '/'
        << availableTeams(state_) << "（基础3，每支2至6人的资源队+1，最高7）"
        << "\n资源队可分配2至6人；支持岗位与前哨守卫只分配0或1人。"
        << "\n已激活效果：";
    bool hasSupportEffect = false;
    if (w.crafters > 0) {
        out << (state_.buildings[indexOf(BuildingId::Workshop)] ? "工坊可制造、维修" : "工匠等待武备工坊");
        hasSupportEffect = true;
    }
    if (w.healers > 0) {
        out << (hasSupportEffect ? "；" : "")
            << (state_.buildings[indexOf(BuildingId::HealerHut)] ? "医者维护疾病防护并可治疗" : "医者等待医者小屋");
        hasSupportEffect = true;
    }
    if (w.scouts > 0) {
        out << (hasSupportEffect ? "；" : "") << "侦察降低袭扰与野兽风险";
        hasSupportEffect = true;
    }
    if (w.envoys > 0) {
        out << (hasSupportEffect ? "；" : "") << "使者维持外交";
        if (state_.buildings[indexOf(BuildingId::CouncilFire)]) out << "、主持议事火坛";
        hasSupportEffect = true;
    }
    if (w.campGuards > 0) {
        out << (hasSupportEffect ? "；" : "") << "营地守卫抵御袭扰";
        hasSupportEffect = true;
    }
    if (!hasSupportEffect) out << "尚未激活支持岗位";
    out << "\n负责人：工坊"
        << (state_.workshopSupervisor.empty()
                ? "未任命"
                : state_.workshopSupervisor + "（工艺等级" + std::to_string(craftSupervisorRank(state_)) + "）" +
                      (w.crafters == 0 ? "[未配置劳力，停工]" : ""))
        << "；医者"
        << (state_.healerSupervisor.empty()
                ? "未任命"
                : state_.healerSupervisor + "（医疗等级" + std::to_string(medicineSupervisorRank(state_)) + "）" +
                      (w.healers == 0 ? "[未配置劳力，停工]" : ""));
    if (state_.workforceReassignmentRequired)
        out << "\n[劳力待重分配] 仅可降低劳力或驻军，或解散军队，直到人口占用恢复合法。";
    out << "\n用法：assign <岗位> <人数>；assign outpost <地点> <0|1>。";
    return out.str();
}

std::string GameEngine::inventoryText() const {
    std::ostringstream out;
    out << "共享装备仓库（" << state_.stockpile.size() << "件）：\n";
    for (const Item& item : state_.stockpile) {
        out << item.id << "  " << item.name << "  状态"
            << (item.condition == ItemCondition::Intact    ? "完好"
                : item.condition == ItemCondition::Damaged ? "损坏"
                                                           : "报废")
            << "  品质" << itemQualityName(item.quality) << "  实际属性";
        bool hasBonus = false;
        const int divisor = item.condition == ItemCondition::Damaged ? 2 : 1;
        const int quality = itemQualityTier(item.quality);
        for (std::size_t index = 0; index < kAttributeCount; ++index) {
            const int base = item.bonuses.values[index];
            const int effective = (base > 0 ? base + quality : base) / divisor;
            if (effective == 0) continue;
            out << (hasBonus ? "," : "") << "属性" << (index + 1U) << (effective > 0 ? "+" : "") << effective;
            hasBonus = true;
        }
        if (!hasBonus) out << "无";
        out << '\n';
    }
    if (state_.stockpile.empty()) out << "（空）\n";
    return out.str();
}

std::string GameEngine::peopleText() const {
    std::ostringstream out;
    out << "人物档案：工坊负责人"
        << (state_.workshopSupervisor.empty()
                ? "未任命"
                : state_.workshopSupervisor + "（工艺等级" + std::to_string(craftSupervisorRank(state_)) +
                      (state_.workforce.crafters == 0 ? "，停工）" : "）"))
        << "；医者负责人"
        << (state_.healerSupervisor.empty()
                ? "未任命"
                : state_.healerSupervisor + "（医疗等级" + std::to_string(medicineSupervisorRank(state_)) +
                      (state_.workforce.healers == 0 ? "，停工）" : "）"))
        << "\n";
    for (const Character& character : state_.roster)
        out << character.name << " 等级" << character.level << " 生命" << character.life << " 疲劳" << character.fatigue
            << " 忠诚" << character.loyalty << " 职业" << occupationName(character.occupation)
            << (character.name == state_.workshopSupervisor ? " [工坊负责人]" : "")
            << (character.name == state_.healerSupervisor ? " [医者负责人]" : "") << '\n';
    return out.str();
}

std::string GameEngine::personText(const std::string_view name) const {
    const Character* person = findRosterCharacter(state_.roster, name);
    if (person == nullptr) return "没有这个人物。";
    std::ostringstream out;
    out << person->name << " 职业" << occupationName(person->occupation) << " 等级" << person->level << " 经验"
        << person->experience << " 生命" << person->life << " 疲劳" << person->fatigue << " 忠诚" << person->loyalty
        << "\n八项属性：";
    for (int value : person->attributes.values) out << ' ' << value;
    out << "\n装备：";
    bool any = false;
    for (std::size_t i = 0; i < person->equipment.size(); ++i) {
        const auto& equipped = person->equipment[i];
        if (!equipped.has_value()) continue;
        const Item& item = equipped.value();
        out << equipmentSlotName(static_cast<EquipmentSlot>(i)) << ':' << item.name << ' ';
        any = true;
    }
    if (!any) out << "无";
    return out.str();
}

std::string GameEngine::buildingsText() const {
    return "建筑清单（木材/石料/行动/维护/收益）\n粮仓 8/2/1/无/降低粮食风险\n木墙 10/2/1/营地守卫/提高防御\n武备工坊 "
           "8/6/1/工匠/制造、维修、三阶技术；工匠负责人决定装备品质\n医者小屋 "
           "6/2/1/医者/医者维护疾病防护、草药医治；医者负责人强化治疗与休整\n瞭望塔 "
           "8/4/1/侦察/预警，袭扰与野兽伤害-1\n议事火坛 6/4/1/使者/派系满意流失-2";
}

std::string GameEngine::technologiesText() const {
    return "技术清单（食物/木材/前置/效果）\n食物保存 3/2/无/食物任务增益\n草药知识 "
           "3/2/无/草药任务增益、披风与护符\n引水耕作 "
           "3/2/无/食物任务增益\n燧石长矛、盾墙阵形、伏击训练需武备工坊及前序技术。";
}

std::string GameEngine::warTargetsText() const {
    std::ostringstream out;
    out << "战争目标（据点/占领/驻军/动乱）：\n";
    for (std::size_t i = 1; i < kTribeCount; ++i) {
        const OccupationState& site = state_.occupations[i];
        out << tribeName(static_cast<TribeId>(i)) << " / "
            << worldLocations()[indexOf(contactLocation(static_cast<TribeId>(i)))].name << " / "
            << (site.occupied ? "已占领" : "未占领") << " / " << site.garrison << " / " << site.unrest
            << "（需2至4驻军）\n";
    }
    return out.str();
}

std::string GameEngine::powerText() const {
    const WarState& war = state_.war;
    std::ostringstream out;
    out << "军队战力：正式战士" << war.warriors << " 民兵" << war.militia << " 长矛" << war.spearMilitia << " 盾兵"
        << war.shieldBearers << " 重装长矛" << war.heavySpears << "\n锁定装备" << war.lockedEquipment.size()
        << "件，工坊负责人" << (state_.workshopSupervisor.empty() ? "未任命" : state_.workshopSupervisor)
        << "，品质战力+" << war.craftsmanshipPower
        << "（品质总和最多+4）；攻击/防御基础=正式战士×2+民兵+士气÷10，长矛+2、盾+1、重装组合再+2；当前战力"
        << war.playerPower;
    return out.str();
}

std::string GameEngine::worldText() const {
    std::ostringstream output;
    output << "十六地点世界地图（小队沿相邻道路探索）：\n";
    for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
        const auto& location = worldLocations()[index];
        output << (index + 1) << ". " << (state_.discovered[index] ? location.name : "????")
               << (state_.discovered[index] ? " [" + locationRoleName(location.role) + "] — " + location.feature : "")
               << (state_.outposts[index] ? " [结算点]" : "") << '\n';
    }
    return output.str();
}

std::string GameEngine::diplomacyText() const {
    std::ostringstream output;
    output << "六部落外交（关系/信任/恐惧/贸易依赖）：\n";
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        const auto& profile = state_.tribes[index];
        const auto& relation = state_.relations[index];
        const TribeId tribe = static_cast<TribeId>(index);
        output << profile.name << "  " << relation.relation << '/' << relation.trust << '/' << relation.fear << '/'
               << relation.tradeDependence;
        if (relation.atWar) output << " [战争]";
        if (relation.truce) output << " [停战]";
        if (relation.alliance) output << " [联盟]";
        if (relation.marriage) output << " [联姻]";
        if (relation.playerPaysTribute) output << " [我方朝贡]";
        if (relation.otherPaysTribute) output << " [对方进贡]";
        if (relation.tradeRoute) output << " [固定商路]";
        if (!locationDiscovered(state_, contactLocation(tribe))) {
            output << " [尚未充分接触]";
        } else {
            output << "  首领" << profile.leader << " 性格：" << profile.personality;
            const FactionState& faction = dominantFaction(profile);
            output << "  主导派系：" << faction.name;
            if (knowsFactionDemand(relation))
                output << " 诉求：" << faction.demand;
            else
                output << " [诉求待查]";
            if (knowsFullFactionNetwork(relation) && profile.factions.size() > 1U) {
                output << "  其他派系：";
                bool first = true;
                for (const FactionState& other : profile.factions) {
                    if (&other == &faction) continue;
                    if (!first) output << "、";
                    output << other.name << "（" << other.demand << "）";
                    first = false;
                }
            }
        }
        output << '\n';
    }
    return output.str();
}

std::string GameEngine::factionText() const {
    std::ostringstream output;
    output << "内部稳定：" << state_.stability << "\n";
    for (std::size_t index = 0; index < kPlayerFactionCount; ++index) {
        const auto& faction = state_.playerFactions[index];
        output << (index + 1) << ". " << faction.name << " 影响" << faction.influence << " 满意" << faction.satisfaction
               << " 危机：" << crisisName(faction.crisis) << " 诉求：" << faction.demand << " 候选："
               << faction.candidate << '\n';
    }
    return output.str();
}

std::string GameEngine::squadText() const {
    std::ostringstream output;
    output << "具名人物：" << state_.roster.size() << " 最高等级：" << state_.highestLevel << "\n";
    for (const PermanentSquad& squad : state_.squads) {
        output << squad.name << " 队长" << squad.captain << " 人数" << squad.members.size() << " 疲劳" << squad.fatigue
               << " 精锐经验" << squad.eliteExperience << " 驻地" << worldLocations()[indexOf(squad.station)].name
               << (squad.refusingOrders ? " [抗命]" : "") << '\n';
    }
    return output.str();
}

std::string GameEngine::objectiveText() const {
    const auto endings = availableEndings();
    std::ostringstream output;
    output << "当前已满足道路：";
    for (const GameEnding ending : endings) output << endingName(ending) << ' ';
    output << "\n联盟：河鹿/白羽关系70、至少2个联盟、部落联盟技术。"
           << "\n征服：占领任意两个外部据点、战士5、士气55。"
           << "\n繁荣：人口20、食物40、建筑4、技术4。"
           << "\n迁徙：只要部落仍存活即可选择。";
    return output.str();
}

std::string GameEngine::chronicleText() const {
    std::ostringstream output;
    const std::size_t start = state_.chronicle.size() > 12U ? state_.chronicle.size() - 12U : 0U;
    for (std::size_t index = start; index < state_.chronicle.size(); ++index) {
        const auto& entry = state_.chronicle[index];
        output << "第" << entry.season << "季 [" << entry.importance << "] " << entry.title << "：" << entry.detail
               << '\n';
    }
    return output.str();
}

std::string GameEngine::helpText() const {
    return "查询：1/status状态 2/map地图 3/workforce劳力 4/inventory仓库 6/diplomacy外交 factions派系 squads小队 "
           "objectives目标 chronicle编年史\n"
           "经营：build/建造 <建筑>，research/研究 <技术>；资源和地点只能通过地图任务取得\n"
           "任务：5 或 mission；任务内使用move/移动、gather/采集、build outpost/建造前哨、settle/结算\n"
           "劳力：资源队可分配2至6人；工匠、医者、侦察、使者、守卫和前哨守卫只分配0或1人；所有岗位、驻军、军队和出任务"
           "小队共用人口-2\n"
           "小队：squadrest 小队休整；appoint/unappoint <workshop|healer> 任免负责人；负责人不可加入小队\n"
           "外交：talk gift trade <部落> <给出资源> <换取资源> openroute marry tribute demand ally declare truce raid\n"
           "内政：appease <1至3>；战争：formarmy <战士> <民兵>，disbandarmy 解散军队，war "
           "<部落>，战中attack/defend/order/retreat\n"
           "季节：8/endturn；结局：choose <alliance|conquest|prosperity|migration>；长期结局后sandbox。";
}

EndingSummary GameEngine::endingSummary() const {
    EndingSummary summary;
    summary.ending = state_.ending;
    summary.title = endingName(state_.ending);
    switch (state_.ending) {
        case GameEnding::Alliance:
            summary.epilogue = "诸部落的旗帜围绕共同火坛，争执仍在，但道路第一次由议事而非刀锋决定。";
            break;
        case GameEnding::Conquest:
            summary.epilogue = "红金战旗升上岩牙要塞，胜利带来疆土，也要求后人承担统治的代价。";
            break;
        case GameEnding::Prosperity:
            summary.epilogue = "粮仓、武备工坊与炊烟连成新的聚落，燧火从求生之火变成文明之火。";
            break;
        case GameEnding::Migration:
            summary.epilogue = "队伍越过山隘，把旧火种带往晨光中的新土地。";
            break;
        case GameEnding::Extinction:
            summary.epilogue = "营墙倒塌，火坛变暗；留下的故事提醒后来者饥饿、战争与分裂的代价。";
            break;
        case GameEnding::None:
            summary.epilogue = "战役尚未结束。";
            break;
    }
    summary.statistics = {
        "生存季节：" + std::to_string(state_.season),
        "人口/食物/稳定：" + std::to_string(state_.population) + "/" + std::to_string(state_.food) + "/" +
            std::to_string(state_.stability),
        "建筑/技术：" + std::to_string(countTrue(state_.buildings)) + "/" +
            std::to_string(countTrue(state_.technologies)),
        "小队任务/阵亡：" + std::to_string(state_.missionCount) + "/" + std::to_string(state_.missionDeaths),
        "战争胜负：" + std::to_string(state_.warsWon) + "/" + std::to_string(state_.warsLost),
        "贸易次数：" + std::to_string(state_.tradeCount),
        "最终首领：" + state_.leaderName + "，历任记录" + std::to_string(state_.leadershipHistory.size()) + "条",
    };
    for (const GameEnding ending : availableEndings()) {
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

std::string GameEngine::modeName(const GameMode mode) {
    switch (mode) {
        case GameMode::Quick:
            return "快速游戏（8季）";
        case GameMode::Standard:
            return "正式游戏（16季）";
        case GameMode::Long:
            return "长期游戏（32季）";
    }
    return "未知模式";
}

std::string GameEngine::phaseName(const GamePhase phase) {
    switch (phase) {
        case GamePhase::Managing:
            return "部落管理";
        case GamePhase::Mission:
            return "可操控小队任务";
        case GamePhase::War:
            return "可操控部落战争";
        case GamePhase::EndingChoice:
            return "时代结算选择";
        case GamePhase::Finished:
            return "独立结局结算";
        case GamePhase::Sandbox:
            return "结局后沙盒";
    }
    return "未知阶段";
}

std::string GameEngine::endingName(const GameEnding ending) {
    switch (ending) {
        case GameEnding::None:
            return "尚未结算";
        case GameEnding::Alliance:
            return "联盟共主";
        case GameEnding::Conquest:
            return "山河征服者";
        case GameEnding::Prosperity:
            return "燧火繁荣";
        case GameEnding::Migration:
            return "迁徙新生";
        case GameEnding::Extinction:
            return "部落覆灭";
    }
    return "未知结局";
}

std::string GameEngine::tribeName(const TribeId tribe) {
    switch (tribe) {
        case TribeId::Player:
            return "玩家部落";
        case TribeId::RiverDeer:
            return "河鹿";
        case TribeId::WhiteFeather:
            return "白羽";
        case TribeId::Rockfang:
            return "岩牙";
        case TribeId::Tidesalt:
            return "潮盐";
        case TribeId::Blackstone:
            return "玄石";
        case TribeId::Count:
            break;
    }
    return "未知部落";
}

std::string GameEngine::resourceName(const ResourceKind resource) {
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
    return "未知资源";
}

} // namespace tribe
