#pragma once

#include "tribe/game_engine.hpp"

#include <string>

namespace tribe::population_rules {

/// 用途：统计劳力岗位占用。输出：人数；无状态修改；岗位数必须非负。
int workforceAllocation(const WorkforceState& workforce);
/// 用途：统计据点驻军占用。输出：人数；无状态修改。
int garrisonAllocation(const GameState& state);
/// 用途：判断是否存在已组建军队。输出：布尔值；无状态修改。
bool hasPreparedArmy(const GameState& state);
/// 用途：统计劳力、驻军、军队与任务的统一人口占用。输出：人数；无状态修改。
int committedPopulation(const GameState& state);
/// 用途：计算可投入岗位的人口容量。输出：非负人数；无状态修改。
int populationCapacity(const GameState& state);
/// 用途：计算超过容量的人口数。输出：非负人数；无状态修改。
int populationOverage(const GameState& state);
/// 用途：刷新待重分配标志。状态影响：仅修改 state 标志；不变量：标志等于 overage 是否大于零。
void refreshWorkforceReassignment(GameState& state);
/// 用途：验证岗位人数和守卫闲置季数。输出：是否有效；失败写 error 且不修改 workforce。
bool validateWorkforce(const WorkforceState& workforce, std::string& error);

} // namespace tribe::population_rules
