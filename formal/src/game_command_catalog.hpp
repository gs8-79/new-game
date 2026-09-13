#pragma once

#include "command_parser.hpp"

#include <cstddef>

namespace tribe::game_command_catalog {

/// 用途：为主游戏和地图任务提供稳定的动词分类。输入/输出：内部调度使用；无游戏状态修改。
enum class CommandId : int {
    Unknown,
    Status,
    Look,
    Map,
    Diplomacy,
    Factions,
    Squad,
    Squads,
    Objectives,
    Chronicle,
    Help,
    Sandbox,
    Choose,
    Assign,
    Garrison,
    DisbandArmy,
    Event,
    Build,
    BuildOutpost,
    Research,
    Mission,
    Workforce,
    Inventory,
    People,
    Person,
    Buildings,
    Technologies,
    Craft,
    Repair,
    Scrap,
    Equip,
    Unequip,
    Appoint,
    Unappoint,
    Treat,
    War,
    WarTargets,
    Power,
    SquadRest,
    Talk,
    Gift,
    Trade,
    OpenRoute,
    Marry,
    Tribute,
    Demand,
    Ally,
    Declare,
    Truce,
    Raid,
    Appease,
    FormArmy,
    EndTurn,
    Abort,
    Order,
    Attack,
    Defend,
    Retreat,
    Move,
    Gather,
    Use,
    Settle,
};

/// 用途：将共享解析器产出的动词映射为唯一命令类别。输入：标准化命令。输出：类别；无状态修改。
/// 失败：未知动词返回 Unknown；不变量：中英文和数字别名只在此处登记。
CommandId classify(const command_parser::Command& command);
/// 用途：判断命令参数数量是否精确匹配。输入：命令和期望数量。输出：布尔值；无状态修改。
bool hasArity(const command_parser::Command& command, std::size_t expected);

} // namespace tribe::game_command_catalog
