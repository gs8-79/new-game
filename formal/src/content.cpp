#include "tribe/content.hpp"

#include <algorithm>
#include <initializer_list>

namespace tribe {
namespace {

const std::array<LocationDefinition, kLocationCount> kLocations{{
    {LocationId::Camp, "camp", "燧火营地", "族人的家园与所有建设的中心。", {LocationId::Forest, LocationId::RedPlain}},
    {LocationId::Forest, "forest", "苍林", "猎物和木材丰富的古老森林。", {LocationId::Camp, LocationId::Marsh}},
    {LocationId::RedPlain, "plain", "红土原", "适合采集和未来耕作的开阔土地。", {LocationId::Camp, LocationId::RiverFord, LocationId::Quarry}},
    {LocationId::Marsh, "marsh", "芦苇沼泽", "草药生长在浅水与芦苇之间。", {LocationId::Forest, LocationId::WhiteFeatherCamp}},
    {LocationId::RiverFord, "riverford", "河鹿渡口", "河鹿部落控制的河谷贸易通道。", {LocationId::RedPlain, LocationId::WhiteFeatherCamp, LocationId::OldPass}},
    {LocationId::WhiteFeatherCamp, "whitefeather", "白羽营地", "游猎者和草药师搭起的轻便营地。", {LocationId::Marsh, LocationId::RiverFord}},
    {LocationId::Quarry, "quarry", "燧石矿场", "裸露岩层中可以开采坚硬石料。", {LocationId::RedPlain, LocationId::OldPass}},
    {LocationId::OldPass, "pass", "古老山隘", "狭窄山路适合侦察和伏击。", {LocationId::RiverFord, LocationId::Quarry, LocationId::RockfangFort}},
    {LocationId::RockfangFort, "rockfangfort", "岩牙要塞", "岩牙部落盘踞的石岭堡垒。", {LocationId::OldPass}},
}};

const std::array<BuildingDefinition, kBuildingCount> kBuildings{{
    {BuildingId::Granary, "granary", "粮仓", 8, 4, 0, "食物上限提高到100，冬季消耗减少2。"},
    {BuildingId::Wall, "wall", "木墙", 12, 4, 0, "营地永久防御提高6。"},
    {BuildingId::Workshop, "workshop", "工坊", 10, 8, 0, "开放二级和三级技术。"},
    {BuildingId::HealerHut, "healer", "医者小屋", 8, 4, 3, "疾病和受伤事件少损失1人口。"},
    {BuildingId::Watchtower, "watchtower", "瞭望塔", 10, 6, 0, "防御提高3，并提前发现敌袭。"},
    {BuildingId::CouncilFire, "council", "议事火坛", 8, 6, 0, "每次有效外交额外提高5点关系。"},
}};

const std::array<TechnologyDefinition, kTechnologyCount> kTechnologies{{
    {TechnologyId::FoodPreservation, "preservation", "食物保存", 1, std::nullopt, "采集食物额外获得2。"},
    {TechnologyId::HerbalKnowledge, "herbalism", "草药知识", 2, TechnologyId::FoodPreservation, "采集草药额外获得1。"},
    {TechnologyId::Irrigation, "irrigation", "引水耕作", 3, TechnologyId::HerbalKnowledge, "采集食物再额外获得3。"},
    {TechnologyId::FlintSpear, "spear", "燧石长矛", 1, std::nullopt, "所有战斗战力提高2。"},
    {TechnologyId::ShieldWall, "shield", "盾墙阵形", 2, TechnologyId::FlintSpear, "防守战力提高2。"},
    {TechnologyId::AmbushTraining, "ambush", "伏击训练", 3, TechnologyId::ShieldWall, "成功准备的伏击战力再提高3。"},
    {TechnologyId::GiftCustoms, "gifts", "赠礼习俗", 1, std::nullopt, "外交赠礼的食物费用由4降到2。"},
    {TechnologyId::SharedLanguage, "language", "共同语言", 2, TechnologyId::GiftCustoms, "交谈额外提高5点关系。"},
    {TechnologyId::Confederation, "confederation", "部落联盟", 3, TechnologyId::SharedLanguage, "开放联盟共主结局。"},
}};

const std::array<EventDefinition, kEventCount> kEvents{{
    {EventId::GentleSpring, "gentle_spring", "温润春风", "新芽遍布红土原，族人对未来更有信心。"},
    {EventId::RichHunt, "rich_hunt", "丰盛狩猎", "猎手追踪到一支庞大的鹿群。"},
    {EventId::Drought, "drought", "旱季", "河床变浅，能采集的食物明显减少。"},
    {EventId::Flood, "flood", "山洪", "暴雨冲击营地边缘和木材堆。"},
    {EventId::Sickness, "sickness", "热病", "潮湿空气令几名族人病倒。"},
    {EventId::Refugees, "refugees", "流浪者", "一小群失去家园的人请求加入燧火。"},
    {EventId::Traders, "traders", "远方商旅", "陌生旅人带来木料和石块。"},
    {EventId::ForestFire, "forest_fire", "林火", "雷火沿着枯枝蔓延，威胁储存物资。"},
    {EventId::Predators, "predators", "兽群侵扰", "饥饿猛兽偷走营地外的食物。"},
    {EventId::Dispute, "dispute", "族人争执", "分配劳动的矛盾让营地气氛紧张。"},
    {EventId::HerbBloom, "herb_bloom", "草药花期", "沼泽里出现大片可以入药的花朵。"},
    {EventId::ColdSnap, "cold_snap", "寒潮", "反常低温迫使族人消耗更多储备。"},
    {EventId::Craftspeople, "craftspeople", "巧手族人", "工匠改进工具，节省了一批木料。"},
    {EventId::RockfangScouts, "rockfang_scouts", "岩牙斥候", "山隘出现岩牙部落的侦察痕迹。"},
    {EventId::Newborns, "newborns", "新生命", "食物充足的营地迎来新的孩子。"},
    {EventId::Festival, "festival", "燧火节", "族人希望拿出食物庆祝共同度过的岁月。"},
    {EventId::LostHunters, "lost_hunters", "迷失的猎手", "一支猎队迟迟没有回到营地。"},
    {EventId::ClearSky, "clear_sky", "澄澈长空", "平静季节给了所有人喘息的机会。"},
    {EventId::RiverEnvoys, "river_envoys", "河鹿来使", "河鹿首领芦岚派使者邀请燧火前往渡口。"},
    {EventId::WhiteFeatherSign, "whitefeather_sign", "白羽踪迹", "沼泽边留下白羽药师的记号和一束干草药。"},
    {EventId::RockfangRaid, "rockfang_raid", "岩牙来袭", "岩牙战士越过古老山隘，燧火必须选择迎敌方法。"},
    {EventId::FinalCouncil, "final_council", "终岁议事", "第四年最后的火光下，族人开始讨论部落的未来。"},
}};

bool equalsAny(const std::string_view value, const std::initializer_list<std::string_view> aliases) {
    return std::find(aliases.begin(), aliases.end(), value) != aliases.end();
}

} // namespace

const std::array<LocationDefinition, kLocationCount>& locations() { return kLocations; }
const std::array<BuildingDefinition, kBuildingCount>& buildings() { return kBuildings; }
const std::array<TechnologyDefinition, kTechnologyCount>& technologies() { return kTechnologies; }
const std::array<EventDefinition, kEventCount>& events() { return kEvents; }

const LocationDefinition& location(const LocationId id) { return kLocations.at(indexOf(id)); }
const BuildingDefinition& building(const BuildingId id) { return kBuildings.at(indexOf(id)); }
const TechnologyDefinition& technology(const TechnologyId id) { return kTechnologies.at(indexOf(id)); }
const EventDefinition& event(const EventId id) { return kEvents.at(indexOf(id)); }

bool areAdjacent(const LocationId left, const LocationId right) {
    const auto& neighbors = location(left).neighbors;
    return std::find(neighbors.begin(), neighbors.end(), right) != neighbors.end();
}

std::optional<LocationId> findLocation(const std::string_view name) {
    for (const auto& item : kLocations) {
        if (name == item.key || name == item.chineseName) return item.id;
    }
    if (equalsAny(name, {"redplain", "红土", "平原"})) return LocationId::RedPlain;
    if (equalsAny(name, {"river", "riverdeer", "河鹿", "渡口"})) return LocationId::RiverFord;
    if (equalsAny(name, {"white", "白羽"})) return LocationId::WhiteFeatherCamp;
    if (equalsAny(name, {"oldpass", "山隘"})) return LocationId::OldPass;
    if (equalsAny(name, {"rockfang", "岩牙", "要塞"})) return LocationId::RockfangFort;
    return std::nullopt;
}

std::optional<BuildingId> findBuilding(const std::string_view name) {
    for (const auto& item : kBuildings) {
        if (name == item.key || name == item.chineseName) return item.id;
    }
    if (name == "木墙") return BuildingId::Wall;
    if (equalsAny(name, {"healerhut", "医者", "医馆"})) return BuildingId::HealerHut;
    if (equalsAny(name, {"tower", "塔"})) return BuildingId::Watchtower;
    if (equalsAny(name, {"fire", "火坛"})) return BuildingId::CouncilFire;
    return std::nullopt;
}

std::optional<TechnologyId> findTechnology(const std::string_view name) {
    for (const auto& item : kTechnologies) {
        if (name == item.key || name == item.chineseName) return item.id;
    }
    if (name == "长矛") return TechnologyId::FlintSpear;
    if (name == "盾墙") return TechnologyId::ShieldWall;
    if (name == "伏击") return TechnologyId::AmbushTraining;
    if (name == "赠礼") return TechnologyId::GiftCustoms;
    if (name == "语言") return TechnologyId::SharedLanguage;
    if (name == "联盟") return TechnologyId::Confederation;
    return std::nullopt;
}

std::optional<FactionId> findFaction(const std::string_view name) {
    if (equalsAny(name, {"riverdeer", "river", "河鹿", "河鹿部落"})) return FactionId::RiverDeer;
    if (equalsAny(name, {"whitefeather", "white", "白羽", "白羽部落"})) return FactionId::WhiteFeather;
    if (equalsAny(name, {"rockfang", "rock", "岩牙", "岩牙部落"})) return FactionId::Rockfang;
    return std::nullopt;
}

std::optional<Tactic> findTactic(const std::string_view name) {
    if (equalsAny(name, {"assault", "正面", "强攻", "进攻"})) return Tactic::Assault;
    if (equalsAny(name, {"ambush", "伏击"})) return Tactic::Ambush;
    if (equalsAny(name, {"defend", "防守", "固守"})) return Tactic::Defend;
    if (equalsAny(name, {"retreat", "撤退"})) return Tactic::Retreat;
    return std::nullopt;
}

std::optional<Ending> findEnding(const std::string_view name) {
    if (equalsAny(name, {"alliance", "联盟", "联盟共主"})) return Ending::Alliance;
    if (equalsAny(name, {"conquest", "征服", "山河征服者"})) return Ending::Conquest;
    if (equalsAny(name, {"prosperity", "繁荣", "燧火繁荣"})) return Ending::Prosperity;
    if (equalsAny(name, {"migration", "迁徙", "迁徙新生"})) return Ending::Migration;
    return std::nullopt;
}

std::string factionName(const FactionId faction) {
    switch (faction) {
    case FactionId::RiverDeer: return "河鹿部落";
    case FactionId::WhiteFeather: return "白羽部落";
    case FactionId::Rockfang: return "岩牙部落";
    case FactionId::Count: break;
    }
    return "未知部落";
}

std::string tacticName(const Tactic tactic) {
    switch (tactic) {
    case Tactic::Assault: return "正面进攻";
    case Tactic::Ambush: return "伏击";
    case Tactic::Defend: return "防守";
    case Tactic::Retreat: return "撤退";
    }
    return "未知战术";
}

} // namespace tribe
