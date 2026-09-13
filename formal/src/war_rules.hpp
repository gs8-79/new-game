#pragma once

#include "tribe/game_engine.hpp"

namespace tribe::war_rules {

/// 用途：将军队锁定装备归还库存或标记损坏。状态影响：修改 state 的库存与锁定列表。
/// 失败：无；不变量：每件装备离开锁定列表后仅在库存出现一次。
void releaseLockedEquipment(GameState& state, bool damaged);

} // namespace tribe::war_rules
