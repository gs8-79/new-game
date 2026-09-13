#include "war_rules.hpp"

#include <utility>

namespace tribe::war_rules {

void releaseLockedEquipment(GameState& state, const bool damaged) {
    // 归还即搬回共享仓库并清空锁定列表，保证同一件装备不会同时存在于军队和仓库两处。
    for (Item item : state.war.lockedEquipment) {
        if (damaged && item.condition == ItemCondition::Intact) item.condition = ItemCondition::Damaged;
        state.stockpile.push_back(std::move(item));
    }
    state.war.lockedEquipment.clear();
}

} // namespace tribe::war_rules
