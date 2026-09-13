#include "world_map_catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace tribe::world_map {
namespace {

/// 用途：保存十六地点的唯一名称、职能和双向道路目录。输入/输出：供规则与文本只读查询。
/// 状态影响：首次调用时初始化静态目录。失败：无；不变量：数组下标与 WorldLocationId 稳定编号一致。
const std::array<WorldLocationInfo, kWorldLocationCount> kLocations{{
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
    {WorldLocationId::RockfangFort, "岩牙要塞", "岩牙巡逻与征服目标", LocationRole::War, {WorldLocationId::OldPass}},
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

/// 用途：地点在终端道路图上的列行坐标，仅供绘制与方位判断。输入/输出：只读查找表项。
/// 不变量：数组下标必须与 WorldLocationId 的持久化编号一致，位置本身不参与存档。
struct Coordinate {
    int column = 0;
    int row = 0;
};

constexpr std::array<Coordinate, kWorldLocationCount> kCoordinates{{
    {3, 3},
    {2, 3},
    {4, 3},
    {2, 2},
    {5, 3},
    {1, 2},
    {4, 4},
    {5, 1},
    {6, 1},
    {3, 2},
    {7, 2},
    {5, 2},
    {5, 4},
    {6, 4},
    {6, 3},
    {7, 3},
}};

const std::array<std::vector<std::string_view>, kWorldLocationCount> kAliases{{
    {"camp", "营地", "燧火营地"},
    {"forest", "苍林"},
    {"plain", "redplain", "红土原"},
    {"marsh", "沼泽", "芦苇沼泽"},
    {"ford", "riverford", "渡口", "河鹿渡口"},
    {"whitecamp", "白羽营地"},
    {"quarry", "矿场", "燧石矿场"},
    {"pass", "oldpass", "山隘", "古老山隘"},
    {"fort", "rockfort", "rockfang", "岩牙要塞"},
    {"coast", "saltwind", "盐风海岸"},
    {"harbor", "tidesaltharbor", "潮盐港"},
    {"beach", "shellbeach", "贝壳滩"},
    {"valley", "blackstonevalley", "玄石谷"},
    {"workshop", "blackstoneworkshop", "玄石工坊"},
    {"market", "mountainmarket", "山前集市"},
    {"road", "cliffroad", "断崖商道"},
}};

constexpr std::array<std::string_view, kWorldLocationCount> kShortNames{{
    "营地",
    "苍林",
    "红土",
    "芦苇",
    "河鹿",
    "白羽",
    "矿场",
    "古老山隘",
    "岩牙要塞",
    "盐风",
    "潮盐",
    "贝壳",
    "玄石谷",
    "玄石工坊",
    "山前",
    "断崖",
}};

/// 用途：判断地点枚举能否安全索引目录。输入：地点枚举。输出：是否在 0 至 15；无状态修改。
/// 失败：Count 或外部非法值返回 false。不变量：目录查询不得因非法输入越界。
bool validLocation(const WorldLocationId location) {
    const int raw = static_cast<int>(location);
    return raw >= 0 && raw < static_cast<int>(WorldLocationId::Count);
}

} // namespace

const std::array<WorldLocationInfo, kWorldLocationCount>& locations() { return kLocations; }

std::optional<WorldLocationId> fromMissionIndex(const int index) {
    if (index < 0 || index >= static_cast<int>(kWorldLocationCount)) return std::nullopt;
    return static_cast<WorldLocationId>(index);
}

std::optional<WorldLocationId> parse(const std::string_view text) {
    int numeric = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), numeric);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && numeric >= 1 &&
        numeric <= static_cast<int>(kWorldLocationCount)) {
        return static_cast<WorldLocationId>(numeric - 1);
    }
    for (std::size_t index = 0U; index < kAliases.size(); ++index) {
        const auto& aliases = kAliases[index];
        if (std::find(aliases.begin(), aliases.end(), text) != aliases.end())
            return static_cast<WorldLocationId>(index);
    }
    return std::nullopt;
}

bool adjacent(const WorldLocationId from, const WorldLocationId to) {
    if (!validLocation(from) || !validLocation(to)) return false;
    const auto& neighbors = locations()[indexOf(from)].neighbors;
    return std::find(neighbors.begin(), neighbors.end(), to) != neighbors.end();
}

std::string direction(const WorldLocationId from, const WorldLocationId to) {
    if (!validLocation(from) || !validLocation(to)) return {};
    const Coordinate start = kCoordinates[indexOf(from)];
    const Coordinate end = kCoordinates[indexOf(to)];
    return std::string(end.row < start.row   ? "北"
                       : end.row > start.row ? "南"
                                             : "") +
           (end.column < start.column   ? "西"
            : end.column > start.column ? "东"
                                        : "");
}

std::string_view shortName(const WorldLocationId location) {
    return validLocation(location) ? kShortNames[indexOf(location)] : std::string_view{};
}

/// 用途：判断地点是否拥有指定资源。输入：地点和资源枚举。输出：布尔值；无状态修改。
/// 失败：非法地点或未知资源返回 false。不变量：这里是地图资源分布的唯一规则表。
bool supportsResource(const WorldLocationId location, const ResourceKind resource) {
    if (!validLocation(location)) return false;
    switch (resource) {
        case ResourceKind::Food:
            return location == WorldLocationId::Forest || location == WorldLocationId::RedPlain ||
                   location == WorldLocationId::RiverFord || location == WorldLocationId::SaltwindCoast;
        case ResourceKind::Wood:
            return location == WorldLocationId::Forest || location == WorldLocationId::Marsh;
        case ResourceKind::Stone:
            return location == WorldLocationId::Quarry || location == WorldLocationId::OldPass ||
                   location == WorldLocationId::BlackstoneValley;
        case ResourceKind::Herbs:
            return location == WorldLocationId::Forest || location == WorldLocationId::Marsh ||
                   location == WorldLocationId::WhiteFeatherCamp;
        case ResourceKind::Hides:
            return location == WorldLocationId::Forest || location == WorldLocationId::RedPlain;
    }
    return false;
}

const std::array<RoadRow, 4>& roadRows() {
    static const std::array<RoadRow, 4> rows{{
        {25U, {WorldLocationId::OldPass, WorldLocationId::RockfangFort}},
        {2U,
         {WorldLocationId::WhiteFeatherCamp, WorldLocationId::Marsh, WorldLocationId::SaltwindCoast,
          WorldLocationId::ShellBeach, WorldLocationId::TidesaltHarbor}},
        {6U,
         {WorldLocationId::Forest, WorldLocationId::Camp, WorldLocationId::RedPlain, WorldLocationId::RiverFord,
          WorldLocationId::MountainMarket, WorldLocationId::CliffTradeRoad}},
        {25U, {WorldLocationId::Quarry, WorldLocationId::BlackstoneValley, WorldLocationId::BlackstoneWorkshop}},
    }};
    return rows;
}

} // namespace tribe::world_map
