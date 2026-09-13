#pragma once

#include "tribe/game_engine.hpp"

#include <string>
#include <string_view>

namespace tribe::state_safety {

/// 用途：检查会显示到终端或写回存档的字符串。输入：文本。输出：是否安全。
/// 状态影响：无。失败：非法 UTF-8、控制字符或 ANSI 序列返回 false。不变量：不修改文本。
bool isSafeDisplayText(std::string_view text);

/// 用途：集中检查 GameState 中所有持久化文本字段。输入：候选状态与错误输出。输出：是否安全。
/// 状态影响：无。失败：首个非法字段写 error。不变量：失败时不修改 state。
bool validatePersistentText(const GameState& state, std::string& error);

} // namespace tribe::state_safety
