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

/// 用途：保存十六地点的风险等级、可期收益与危险说明，供任务文本与地图视图展示。
/// 输入/输出：只读查询；状态影响：静态目录，首次调用时初始化。失败：无。
/// 不变量：数组下标与 WorldLocationId 稳定编号一致；收益文案必须与 supportsResource 的实际资源分布一致。
const std::array<LocationProfile, kWorldLocationCount> kProfiles{{
    {0, "安全", "部落管理、建设、任务结算与全部载货入库", "营地为绝对安全区；这里不存在遭遇与采集时段消耗。"},
    {1, "低", "食物、木材、草药、兽皮（十六地点中唯一五项占其四的采集地）",
     "林间野兽与密林迷路；采集疲惫，注意采集时段有限。"},
    {1, "低", "食物、兽皮；通往河鹿渡口与燧石矿场", "开阔红土无遮蔽，容易被游荡者提前发现。"},
    {2, "中", "木材、草药；连接白羽营地与盐风海岸", "沼泽陷足，行军与采集的疲劳消耗高于平路。"},
    {1, "低", "食物采集；河鹿部落交谈、贸易与开通商路", "渡口水流湍急，商队会盘查过路小队。"},
    {0, "安全", "草药采集；白羽部落交谈、贸易与缔结联盟", "友邦营地，无战斗风险，但位置偏远、往返步数长。"},
    {2, "中", "石料；通往玄石谷", "落石与深坑，且石料沉重、极易占满载货上限。"},
    {3, "高", "石料；岩牙部落接触点，交涉或劫掠的唯一入口",
     "岩牙巡逻与山隘落石；这里也是从岩牙要塞撤退时的固定退回点。"},
    {3, "高", "击退巡逻可得岩牙巡逻徽记（饰品，意志+1）",
     "敌对巡逻挡路：未击退前不能在要塞建造前哨，且遭遇期间道路封锁、不能移动或采集。"},
    {1, "低", "远程食物采集；通往贝壳滩与潮盐港", "潮汐与海风；距离营地最远，返程步数是主要成本。"},
    {1, "低", "潮盐部落交谈、贸易与开通商路；无采集资源", "港口盘查与关税谈判；赶路顺路可接触。"},
    {1, "低", "无采集资源，纯通道", "涨潮封路，只连接盐风海岸与潮盐港，走错要原路返回。"},
    {2, "中", "石料；通往玄石工坊", "峡谷伏击与落石；狭窄地形，撤退代价高。"},
    {1, "低", "玄石部落交谈、贸易与缔结联盟；无采集资源", "工坊守卫盘问来意；顺路可完成外交与商路。"},
    {1, "低", "三路交汇；开通商路的交通节点与物资集散地；无采集资源", "集市拥挤、扒手出没；适合作为长路线中转。"},
    {2, "中", "无采集资源；连接玄石工坊、山前集市与古老山隘", "断崖落石，狭窄路段只能单列通过。"},
}};

/// 用途：非法地点使用的统一兜底档案。输入/输出：只读常量；无状态修改。
/// 不变量：所有文案非空，保证调用方不需要判空即可拼接。
const LocationProfile kUnknownProfile{0, "未知", "未知地点没有登记收益。", "未知地点没有登记风险；请先核对地图编号。"};

/// 用途：按广度优先顺序计算单源最短道路的前置节点表。输入：起点。
/// 输出：下标为地点编号的数组，值为最短路上的上一处地点；起点指向自身，不可达为 -1。
/// 失败：非法起点返回全 -1 的表。不变量：结果只依赖目录邻接表，同一输入必定得到同一路径。
std::array<int, kWorldLocationCount> roadPredecessors(const WorldLocationId start) {
    std::array<int, kWorldLocationCount> previous{};
    previous.fill(-1);
    if (!validLocation(start)) return previous;
    std::array<bool, kWorldLocationCount> seen{};
    std::vector<WorldLocationId> frontier;
    const std::size_t origin = indexOf(start);
    seen[origin] = true;
    previous[origin] = static_cast<int>(origin);
    frontier.push_back(start);
    // 逐层扩展：邻居按目录顺序入队，因此最短路在并列时总是取编号更小的地点。
    for (std::size_t head = 0U; head < frontier.size(); ++head) {
        const std::size_t current = indexOf(frontier[head]);
        for (const WorldLocationId neighbor : locations()[current].neighbors) {
            if (!validLocation(neighbor)) continue;
            const std::size_t next = indexOf(neighbor);
            if (seen[next]) continue;
            seen[next] = true;
            previous[next] = static_cast<int>(current);
            frontier.push_back(neighbor);
        }
    }
    return previous;
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

/// 用途：取得地点的风险与收益档案。输入：地点。输出：只读档案；无状态修改。
/// 失败：非法地点返回统一兜底档案，不抛异常。不变量：返回值引用始终有效。
const LocationProfile& profile(const WorldLocationId location) {
    return validLocation(location) ? kProfiles[indexOf(location)] : kUnknownProfile;
}

/// 用途：计算两地点之间的最短道路步数。输入：两个地点。输出：步数；同点 0。
/// 失败：任一点非法或不可达返回 -1。不变量：结果与任务状态无关，只由目录邻接表决定。
int roadDistance(const WorldLocationId from, const WorldLocationId to) {
    if (!validLocation(from) || !validLocation(to)) return -1;
    const std::vector<WorldLocationId> path = roadPath(from, to);
    if (path.empty()) return -1;
    return static_cast<int>(path.size()) - 1;
}

std::vector<WorldLocationId> roadPath(const WorldLocationId from, const WorldLocationId to) {
    std::vector<WorldLocationId> path;
    if (!validLocation(from) || !validLocation(to)) return path;
    const std::array<int, kWorldLocationCount> previous = roadPredecessors(from);
    // 从终点沿前置表回溯到起点；任何一环缺失都说明两地不连通。
    int cursor = static_cast<int>(indexOf(to));
    while (cursor >= 0) {
        path.push_back(static_cast<WorldLocationId>(cursor));
        if (cursor == static_cast<int>(indexOf(from))) break;
        cursor = previous[static_cast<std::size_t>(cursor)];
    }
    if (path.empty() || path.back() != from) return {};
    std::reverse(path.begin(), path.end());
    return path;
}

std::optional<WorldLocationId> nextStepToward(const WorldLocationId from, const WorldLocationId to) {
    const std::vector<WorldLocationId> path = roadPath(from, to);
    if (path.size() < 2U) return std::nullopt;
    return path[1];
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
