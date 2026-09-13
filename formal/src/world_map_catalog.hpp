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
