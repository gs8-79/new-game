#include "game_engine_internal.hpp"

#include "command_parser.hpp"
#include "seasonal_event_rules.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>
#include <utility>

namespace tribe::game_engine_detail {

using command_parser::Command;
using command_parser::equalsAny;
using command_parser::parse;
using command_parser::verbIs;

// 物品无论位于仓库、战争编制还是任务背包，都必须满足同一组可持久化字段约束。
bool validStoredItem(const Item& item) {
    return !item.id.empty() && !item.name.empty() &&
           enumInRange(item.quality, ItemQuality::Crude, ItemQuality::Legendary) &&
           enumInRange(item.condition, ItemCondition::Intact, ItemCondition::Scrapped) && item.weight >= 0 &&
           item.slotCount > 0 &&
           (!item.equipmentSlot ||
            enumInRange(*item.equipmentSlot, EquipmentSlot::MainHand, EquipmentSlot::Accessory)) &&
           std::all_of(item.bonuses.values.begin(), item.bonuses.values.end(),
                       [](const int bonus) { return bonus >= -kMaximumItemBonus && bonus <= kMaximumItemBonus; });
}

// 地图任务保存的是出发角色的可变镜像；装备没有在任务中流转，故镜像装备必须始终与长期角色一致。
bool sameItem(const Item& left, const Item& right) {
    return left.id == right.id && left.name == right.name && left.quality == right.quality &&
           left.condition == right.condition && left.weight == right.weight && left.slotCount == right.slotCount &&
           left.equipmentSlot == right.equipmentSlot && left.bonuses.values == right.bonuses.values;
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
    if (equalsAny(text, {"housing", "house", "住房", "长屋", "住房岗位"})) return WorkforceRole::Housing;
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
    if (equalsAny(text, {"longhouse", "housing", "住房", "长屋"})) return BuildingId::Longhouse;
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
    if (!state.pendingEvent.active && state.pendingEvents.empty()) return "本季暂无待决事件。";
    const PendingEventKind current = state.pendingEvents.empty() ? state.pendingEvent.kind : state.pendingEvents.front();
    std::ostringstream out;
    const std::size_t total = static_cast<std::size_t>(state.pendingEventIndex) + state.pendingEvents.size() - 1U;
    out << "本季待决事件 " << state.pendingEventIndex << "/" << total
        << "：" << eventName(current) << '\n';
    switch (current) {
        case PendingEventKind::Refugees:
            out << "1. 接纳：食物-4、人口+1、稳定-2\n2. 拒绝：稳定-3";
            break;
        case PendingEventKind::Disease: {
            const bool staffed = state.buildings[indexOf(BuildingId::HealerHut)] && state.workforce.healers > 0;
            out << "1. 医治：草药-" << (staffed ? 1 : state.buildings[indexOf(BuildingId::HealerHut)] ? 3 : 2)
                << "、稳定+2\n2. 隔离失败：人口-" << (staffed ? 1 : 2) << "、稳定-"
                << (state.buildings[indexOf(BuildingId::HealerHut)] ? 6 : 4);
            break;
        }
        case PendingEventKind::Extortion:
            out << "1. 缴纳：食物-4、稳定+3\n2. 抵抗：稳定-2、营地耐久-"
                << seasonal_event_rules::extortionDamage(state);
            break;
        case PendingEventKind::FactionDemand: {
            const bool council = state.buildings[indexOf(BuildingId::CouncilFire)] && state.workforce.envoys > 0;
            out << "1. 让步：食物-3、全派系满意+" << (council ? 8 : 5) << "\n2. 拒绝：全派系满意-"
                << (state.buildings[indexOf(BuildingId::CouncilFire)] ? 10 : 6) << "、稳定-"
                << (state.buildings[indexOf(BuildingId::CouncilFire)] ? 4 : 2);
            break;
        }
    }
    return out.str();
}

} // namespace tribe::game_engine_detail
