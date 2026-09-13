#pragma once

#include "tribe/game_engine.hpp"

#include <string>

namespace tribe::seasonal_event_rules {

/// 用途：季节事件选项的结算回执。输入/输出：由调用方读取 success 与 message；无状态修改。
/// 失败：success 为假时 message 说明原因，调用方必须整体丢弃候选状态。
struct EventResolution {
    bool success = false;
    std::string message;
};

/// 用途：计算勒索事件的营地损伤。输出：非负损伤；无状态修改。
int extortionDamage(const GameState& state);
/// 用途：按选项结算待决季节事件。状态影响：仅修改传入候选状态；失败保持候选状态由调用方丢弃。
EventResolution resolveChoice(GameState& state, int option);

} // namespace tribe::seasonal_event_rules
