#pragma once

#include "tribe/game_engine.hpp"

#include <string>

namespace tribe::seasonal_event_rules {

struct EventResolution {
    bool success = false;
    std::string message;
};

int extortionDamage(const GameState& state);
EventResolution resolveChoice(GameState& state, int option);

} // namespace tribe::seasonal_event_rules
