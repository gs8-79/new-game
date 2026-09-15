#pragma once

#include "tribe/game_state.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe::world_map {

/// 用途：表示终端道路图中一行节点的顺序和缩进。输入/输出：由 ConsoleUI 只读渲染；无状态修改。
struct RoadRow {
    std::size_t indent = 0U;
    std::vector<WorldLocationId> nodes;
};

/// 用途：一个地点的现场档案：风险等级、可期收益与危险说明，用于拉开十六地点的探索差异。
/// 输入/输出：由任务文本与地图视图只读展示；无状态修改。
/// 失败：非法地点返回统一的未知档案。不变量：risk 恒为 0 至 3，等级越高说明现场越危险。
/// 说明：本档案是只读展示层数据，不参与存档，也不改变任何数值规则；调整文案不影响 v6 存档兼容。
struct LocationProfile {
    int risk = 0;
    std::string_view riskName;
    std::string_view reward;
    std::string_view hazard;
};

/// 用途：取得地点的风险与收益档案。输入：合法地点。输出：只读档案引用；无状态修改。
/// 失败：非法地点返回"未知"档案，不抛异常。不变量：返回值始终有效，调用方无需判空。
const LocationProfile& profile(WorldLocationId location);

/// 用途：计算两地点之间的最短道路步数。输入：两个合法地点。输出：步数；同点 0。
/// 失败：任一点非法或两地在道路图上不连通时返回 -1。不变量：结果只由目录邻接表决定，与任务状态无关。
int roadDistance(WorldLocationId from, WorldLocationId to);
/// 用途：给出连接两地的最短道路。输入：两个合法地点。输出：含起点与终点的地点序列。
/// 失败：非法地点或不可达时返回空表；同点返回只含该点的单元素表。
/// 不变量：返回序列中任意相邻两项必定由目录道路相邻，可直接作为移动指令序列使用。
std::vector<WorldLocationId> roadPath(WorldLocationId from, WorldLocationId to);
/// 用途：给出沿最短道路前进的第一步。输入：两个合法地点。输出：下一处相邻地点。
/// 失败：同点、非法地点或不可达时返回空值。不变量：返回值必定与起点相邻。
std::optional<WorldLocationId> nextStepToward(WorldLocationId from, WorldLocationId to);

/// 用途：取得唯一的十六地点目录。输出：GameEngine 公开目录的只读引用；无状态修改。
const std::array<WorldLocationInfo, kWorldLocationCount>& locations();
/// 用途：按稳定编号取得地点。输入：0 至 15 的任务地点编号。输出：地点或空值；无状态修改。
std::optional<WorldLocationId> fromMissionIndex(int index);
/// 用途：解析地点编号或中英文别名。输入：已由命令解析器规范化的文本。输出：地点或空值；无状态修改。
std::optional<WorldLocationId> parse(std::string_view text);
/// 用途：判断两地点是否由目录道路相邻。输入：两个合法地点。输出：布尔值；无状态修改。
bool adjacent(WorldLocationId from, WorldLocationId to);
/// 用途：生成起点到终点的方位词。输入：两个合法地点。输出：中文方位或空文本；无状态修改。
std::string direction(WorldLocationId from, WorldLocationId to);
/// 用途：取得道路图中的短地点名。输入：合法地点。输出：稳定短名称；无状态修改。
std::string_view shortName(WorldLocationId location);
/// 用途：判断地点是否允许采集指定资源。输入：合法地点和资源类别。输出：布尔值；无状态修改。
/// 失败：非法地点返回 false。不变量：资源分布只在地图目录登记，任务层不重复维护地点编号表。
bool supportsResource(WorldLocationId location, ResourceKind resource);
/// 用途：取得终端道路图的四行布局。输出：只读行定义；无状态修改。
const std::array<RoadRow, 4>& roadRows();

} // namespace tribe::world_map
