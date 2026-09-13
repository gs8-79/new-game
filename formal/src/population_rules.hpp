#pragma once

#include "tribe/game_engine.hpp"

#include <string>

namespace tribe::population_rules {

int workforceAllocation(const WorkforceState& workforce);
int garrisonAllocation(const GameState& state);
bool hasPreparedArmy(const GameState& state);
int committedPopulation(const GameState& state);
int populationCapacity(const GameState& state);
int populationOverage(const GameState& state);
void refreshWorkforceReassignment(GameState& state);
bool validateWorkforce(const WorkforceState& workforce, std::string& error);

} // namespace tribe::population_rules
