#include "population_rules.hpp"

#include <algorithm>
#include <array>
#include <numeric>

namespace tribe::population_rules {

int workforceAllocation(const WorkforceState& workforce) {
    // 资源采集不再绑定常驻资源队；四个旧字段只为旧存档/旧命令兼容保留，不应继续占用人口。
    int allocated = workforce.crafters + workforce.healers + workforce.scouts + workforce.envoys + workforce.campGuards +
                    workforce.housing;
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

int availablePopulation(const GameState& state) {
    return std::max(0, populationCapacity(state) - committedPopulation(state));
}

int actionCapacity(const GameState& state) { return std::min(7, availablePopulation(state)); }

int populationOverage(const GameState& state) {
    return std::max(0, committedPopulation(state) - populationCapacity(state));
}

void refreshWorkforceReassignment(GameState& state) {
    // 此标志是统一人口池的派生值，不能由命令各自维护；每次提交前均由超额人数重新计算。
    state.workforceReassignmentRequired = populationOverage(state) > 0;
}

bool validateWorkforce(const WorkforceState& workforce, std::string& error) {
    const std::array<int, 9> workValues{{workforce.foodCrew, workforce.woodCrew, workforce.stoneCrew,
                                         workforce.herbCrew, workforce.crafters, workforce.healers, workforce.scouts,
                                         workforce.envoys, workforce.campGuards}};
    if (std::any_of(workValues.begin(), workValues.end(), [](const int value) { return value < 0 || value > 6; }) ||
        workforce.housing < 0 || workforce.housing > 6 || workforce.crafters > 1 || workforce.healers > 1 ||
        workforce.scouts > 1 || workforce.envoys > 1 || workforce.campGuards > 1 || workforce.housing > 1) {
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
