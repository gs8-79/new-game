#pragma once

#include "tribe/game_engine.hpp"

#include <string>
#include <string_view>

namespace tribe::state_safety {

// 检查会显示到终端或写回存档的字符串：必须是无控制符、无 ANSI 序列的合法 UTF-8。
bool isSafeDisplayText(std::string_view text);

// 集中检查 GameState 中所有持久化文本字段；失败时不修改 state。
bool validatePersistentText(const GameState& state, std::string& error);

} // namespace tribe::state_safety
