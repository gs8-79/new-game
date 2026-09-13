#include "war_rules.hpp"

#include <utility>

namespace tribe::war_rules {

void releaseLockedEquipment(GameState& state, const bool damaged) {
    for (Item item : state.war.lockedEquipment) {
        if (damaged && item.condition == ItemCondition::Intact) item.condition = ItemCondition::Damaged;
        state.stockpile.push_back(std::move(item));
    }
    state.war.lockedEquipment.clear();
}

} // namespace tribe::war_rules
