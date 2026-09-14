#include "population_rules.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace tribe::population_rules {
namespace {

/// 用途：将人口统计的宽位中间结果安全收束为公开 int 返回值。输入：非受信任状态可能给出的总数。
/// 输出：位于 int 范围内的计数。状态影响：无；不变量：校验前的损坏数值也不得触发有符号溢出。
int boundedPopulationCount(const long long value) {
    if (value > static_cast<long long>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (value < static_cast<long long>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
}

} // namespace

int workforceAllocation(const WorkforceState& workforce) {
    long long allocated = static_cast<long long>(workforce.foodCrew) + workforce.woodCrew + workforce.stoneCrew +
                          workforce.herbCrew + workforce.crafters + workforce.healers + workforce.scouts +
                          workforce.envoys + workforce.campGuards;
    for (const int guard : workforce.outpostGuards) allocated += guard;
    return boundedPopulationCount(allocated);
}

int garrisonAllocation(const GameState& state) {
    long long allocated = 0;
    // 下标 0 是玩家部落自身，只有被占领的外部据点才计算驻军占用。
    for (auto site = state.occupations.begin() + 1; site != state.occupations.end(); ++site)
        allocated += site->garrison;
    return boundedPopulationCount(allocated);
}

// 军队只有完成组建（已指定统帅）后才开始占用人口；此前战士只计入 state.warriors。
bool hasPreparedArmy(const GameState& state) { return !state.war.commander.empty(); }

int committedPopulation(const GameState& state) {
    const long long army = hasPreparedArmy(state) ? static_cast<long long>(state.war.warriors) + state.war.militia : 0;
    const long long missionMembers =
        state.activeMission ? static_cast<long long>(state.activeMission->squad.members.size()) : 0;
    // 统一人口池由四类占用相加：劳力岗位、据点驻军、已组建军队、出任务小队。
    return boundedPopulationCount(static_cast<long long>(workforceAllocation(state.workforce)) +
                                  garrisonAllocation(state) + army + missionMembers);
}

// 首领与两名基础留守不参与分配，因此可分配容量是人口减二。
int populationCapacity(const GameState& state) { return state.population > 2 ? state.population - 2 : 0; }

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
        // 资源队要么不设，要么满 2 至 6 人；支持岗位只能是未配置或已配置。
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
