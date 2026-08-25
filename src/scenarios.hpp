#pragma once

#include "core.hpp"

#include <memory>

namespace mud {

std::unique_ptr<Scenario> makeStarportScenario();
std::unique_ptr<Scenario> makeIslandScenario();
std::unique_ptr<Scenario> makeTribeScenario();
std::unique_ptr<Scenario> makeScenarioForMenu(const ParsedCommand& command);

} // namespace mud
