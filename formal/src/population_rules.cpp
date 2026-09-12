#include "population_rules.hpp"

#include <algorithm>
#include <array>
#include <numeric>

namespace tribe::population_rules {

int workforceAllocation(const WorkforceState& workforce) {
    int allocated = workforce.foodCrew + workforce.woodCrew + workforce.stoneCrew + workforce.herbCrew +
                    workforce.crafters + workforce.healers + workforce.scouts + workforce.envoys + workforce.campGuards;
    for (const int guard : workforce.outpostGuards) allocated += guard;
    return allocated;
}

int garrisonAllocation(const GameState& state) {
    return std::accumulate(state.occupations.begin() + 1, state.occupations.end(), 0,
                           [](const int total, const OccupationState& site) { return total + site.garrison; });
}

bool hasPreparedArmy(const GameState& state) { return !state.war.commander.empty(); }

int committedPopulation(const GameState& state) {
    const int army = hasPreparedArmy(state) ? state.war.warriors + state.war.militia : 0;
    const int missionMembers = state.activeMission ? static_cast<int>(state.activeMission->squad.members.size()) : 0;
    return workforceAllocation(state.workforce) + garrisonAllocation(state) + army + missionMembers;
}

int populationCapacity(const GameState& state) { return std::max(0, state.population - 2); }

int populationOverage(const GameState& state) {
    return std::max(0, committedPopulation(state) - populationCapacity(state));
}

void refreshWorkforceReassignment(GameState& state) {
    state.workforceReassignmentRequired = populationOverage(state) > 0;
}

bool validateWorkforce(const WorkforceState& workforce, std::string& error) {
    const std::array<int, 9> workValues{{workforce.foodCrew, workforce.woodCrew, workforce.stoneCrew,
                                         workforce.herbCrew, workforce.crafters, workforce.healers, workforce.scouts,
                                         workforce.envoys, workforce.campGuards}};
    if (std::any_of(workValues.begin(), workValues.end(), [](const int value) { return value < 0 || value > 6; }) ||
        (workforce.foodCrew != 0 && workforce.foodCrew < 2) || (workforce.woodCrew != 0 && workforce.woodCrew < 2) ||
        (workforce.stoneCrew != 0 && workforce.stoneCrew < 2) || (workforce.herbCrew != 0 && workforce.herbCrew < 2) ||
        workforce.crafters > 1 || workforce.healers > 1 || workforce.scouts > 1 || workforce.envoys > 1 ||
        workforce.campGuards > 1) {
        error = "劳力岗位数量无效。";
        return false;
    }
    for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
        if (workforce.outpostGuards[index] < 0 || workforce.outpostGuards[index] > 1 ||
            workforce.outpostIdleSeasons[index] < 0 || workforce.outpostIdleSeasons[index] > 2) {
            error = "前哨守卫字段无效。";
            return false;
        }
    }
    return true;
}

} // namespace tribe::population_rules
