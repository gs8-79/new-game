#pragma once

#include "tribe/game_engine.hpp"

#include <string>

namespace tribe::seasonal_event_rules {

struct EventResolution {
    bool success = false;
    std::string message;
};

/// 用途：计算勒索事件的营地损伤。输出：非负损伤；无状态修改。
int extortionDamage(const GameState& state);
/// 用途：按选项结算待决季节事件。状态影响：仅修改传入候选状态；失败保持候选状态由调用方丢弃。
EventResolution resolveChoice(GameState& state, int option);

} // namespace tribe::seasonal_event_rules
