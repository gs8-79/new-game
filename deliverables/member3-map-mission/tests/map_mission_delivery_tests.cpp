// 成员3交付测试：地图任务与小队系统（十六地点地图、移动、采集、前哨建设、结算、遭遇）。
//
// 覆盖 5.3 要求的三组验收：
//   1) 一张十六地点地图 + 一条完整试玩路线；
//   2) 地图任务说明表中的地点、资源、消耗、奖励、结算点规则；
//   3) 至少一组采集、移动、前哨建设和结算测试。
// 并按 5.2 的优化要求补齐：任务失败、行动点不足、错误地点操作、结算前不入账、路线提示。
//
// 复用约定：本文件与 tools/playthrough_main.cpp 共用 tools/delivery_route.hpp 里的唯一路线表，
// 因此文档、测试与试玩工具描述的一定是同一条路线。

#include "tribe/expansion_game.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "command_parser.hpp"
#include "test_harness.hpp"
#include "world_map_catalog.hpp"

#include "delivery_route.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using tribe::ExpansionCommandResult;
using tribe::ExpansionGame;
using tribe::ExpansionPhase;
using tribe::ExpansionState;
using tribe::GameEngine;
using tribe::GameMode;
using tribe::GameState;
using tribe::ResourceKind;
using tribe::WorldLocationId;

constexpr std::size_t kLocationCount = tribe::kExpeditionWorldLocationCount;

// ---------------------------------------------------------------- 通用断言辅助

tribe::ActionResult requireSuccess(GameEngine& game, const std::string& command) {
    const tribe::ActionResult result = game.execute(command);
    if (!result.success) throw std::runtime_error("游戏命令失败 " + command + "：" + result.message);
    return result;
}

ExpansionCommandResult requireSuccess(ExpansionGame& game, const std::string& command) {
    const ExpansionCommandResult result = game.execute(command);
    if (!result.success) throw std::runtime_error("地图命令失败 " + command + "：" + result.message);
    return result;
}

bool contains(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

/// 用途：失败命令的完整状态快照。输入/输出：只读复制；用于证明拒绝路径没有改动任何地图字段。
struct MissionSnapshot {
    int worldLocation = 0;
    int turn = 0;
    ExpansionPhase phase = ExpansionPhase::Exploring;
    int cargoFood = 0;
    int cargoWood = 0;
    int cargoStone = 0;
    int cargoHerbs = 0;
    int cargoHides = 0;
    int harvestActions = 0;
    bool settled = false;
    int encounterLife = 0;
    bool encounterDefeated = false;
    int cohesion = 0;
    std::size_t memberCount = 0;
    int leaderLife = 0;
    int leaderFatigue = 0;
    std::size_t backpackItems = 0;
    std::array<bool, kLocationCount> outposts{};
    std::array<bool, kLocationCount> discovered{};
};

MissionSnapshot snapshotOf(const ExpansionState& state) {
    MissionSnapshot snapshot;
    snapshot.worldLocation = state.worldLocation;
    snapshot.turn = state.turn;
    snapshot.phase = state.phase;
    snapshot.cargoFood = state.cargoFood;
    snapshot.cargoWood = state.cargoWood;
    snapshot.cargoStone = state.cargoStone;
    snapshot.cargoHerbs = state.cargoHerbs;
    snapshot.cargoHides = state.cargoHides;
    snapshot.harvestActions = state.harvestActions;
    snapshot.settled = state.settled;
    snapshot.encounterLife = state.encounterLife;
    snapshot.encounterDefeated = state.encounterDefeated;
    snapshot.cohesion = state.squad.cohesion;
    snapshot.memberCount = state.squad.members.size();
    snapshot.leaderLife = state.squad.members[state.squad.leaderIndex].life;
    snapshot.leaderFatigue = state.squad.members[state.squad.leaderIndex].fatigue;
    snapshot.backpackItems = state.backpack.items().size();
    snapshot.outposts = state.outposts;
    snapshot.discovered = state.worldDiscovered;
    return snapshot;
}

/// 用途：列出两份快照的差异字段。输入：前后快照。输出：差异说明；完全一致时为空。
std::string snapshotDifferences(const MissionSnapshot& before, const MissionSnapshot& after) {
    std::string report;
    const auto compare = [&report](const char* name, const auto& left, const auto& right) {
        if (!(left == right)) report += std::string(name) + " ";
    };
    compare("worldLocation", before.worldLocation, after.worldLocation);
    compare("turn", before.turn, after.turn);
    compare("phase", before.phase, after.phase);
    compare("cargoFood", before.cargoFood, after.cargoFood);
    compare("cargoWood", before.cargoWood, after.cargoWood);
    compare("cargoStone", before.cargoStone, after.cargoStone);
    compare("cargoHerbs", before.cargoHerbs, after.cargoHerbs);
    compare("cargoHides", before.cargoHides, after.cargoHides);
    compare("harvestActions", before.harvestActions, after.harvestActions);
    compare("settled", before.settled, after.settled);
    compare("encounterLife", before.encounterLife, after.encounterLife);
    compare("encounterDefeated", before.encounterDefeated, after.encounterDefeated);
    compare("cohesion", before.cohesion, after.cohesion);
    compare("memberCount", before.memberCount, after.memberCount);
    compare("leaderLife", before.leaderLife, after.leaderLife);
    compare("leaderFatigue", before.leaderFatigue, after.leaderFatigue);
    compare("backpackItems", before.backpackItems, after.backpackItems);
    compare("outposts", before.outposts, after.outposts);
    compare("discovered", before.discovered, after.discovered);
    return report;
}

/// 用途：要求一条地图命令被拒绝，且返回的候选状态一个字节都没改。
/// 输入：地图任务和命令。输出：无；失败：命令成功或状态被改动时抛出异常。
void requireRejected(ExpansionGame& game, const std::string& command) {
    const MissionSnapshot before = snapshotOf(game.state());
    const ExpansionCommandResult result = game.execute(command);
    if (result.success) throw std::runtime_error("命令本应被拒绝却成功了：" + command);
    if (!result.recognized) throw std::runtime_error("命令本应被识别后拒绝，但未被识别：" + command);
    if (result.stateChanged || result.turnAdvanced)
        throw std::runtime_error("失败命令报告了状态或回合改动：" + command);
    const std::string differences = snapshotDifferences(before, snapshotOf(game.state()));
    if (!differences.empty())
        throw std::runtime_error("失败命令改动了地图状态：" + command + "，差异字段：" + differences);
}

/// 用途：要求一条游戏命令被拒绝，且没有产生候选状态改动。
tribe::ActionResult requireRejected(GameEngine& game, const std::string& command) {
    const tribe::ActionResult result = game.execute(command);
    if (result.success) throw std::runtime_error("游戏命令本应被拒绝却成功了：" + command);
    if (result.stateChanged) throw std::runtime_error("失败的游戏命令报告了状态改动：" + command);
    return result;
}

// ---------------------------------------------------------------- 任务构造辅助

/// 用途：构造一支停在地图指定地点、带指定采集资源的地图任务。输入：地点、资源、种子与人数。
/// 输出：合法任务。失败：状态不合法时抛出异常（地点编号越界等）。不变量：营地始终已发现且是前哨。
ExpansionGame missionAt(const WorldLocationId location, const ResourceKind assigned, const std::uint32_t seed,
                        const std::size_t squadSize = 4U) {
    ExpansionGame base{seed, squadSize};
    ExpansionState state = base.state();
    const std::size_t index = tribe::indexOf(location);
    state.worldLocation = static_cast<int>(index);
    state.worldDiscovered[index] = true;
    state.assignedResource = assigned;
    state.crewSize = static_cast<int>(squadSize);
    return ExpansionGame{std::move(state)};
}

const ExpansionState& missionOf(const GameEngine& game) {
    if (!game.state().activeMission.has_value()) throw std::runtime_error("期望存在进行中的地图任务");
    return game.state().activeMission.value();
}

/// 用途：构造“已经沿唯一道路推进到岩牙要塞”的地图任务。输入：种子与人数。
/// 输出：位于要塞、且来路古老山隘已发现的任务。不变量：这是现实中到达要塞的唯一合法前置状态。
ExpansionGame fortMission(const std::uint32_t seed, const std::size_t squadSize = 4U) {
    ExpansionGame base{seed, squadSize};
    ExpansionState state = base.state();
    state.worldDiscovered[tribe::indexOf(WorldLocationId::OldPass)] = true;
    state.worldDiscovered[tribe::indexOf(WorldLocationId::RockfangFort)] = true;
    state.worldLocation = static_cast<int>(tribe::indexOf(WorldLocationId::RockfangFort));
    state.assignedResource = ResourceKind::Wood;
    state.crewSize = static_cast<int>(squadSize);
    return ExpansionGame{std::move(state)};
}

/// 用途：逐步执行交付路线表。输入：真实游戏引擎与路线。输出：无。
/// 失败：任何一步被拒绝、或击退巡逻超过安全次数时抛出异常。不变量：除“击退遭遇”外每步只输入一次命令。
void runDeliveryRoute(GameEngine& game, const std::vector<delivery::RouteStep>& route) {
    for (const delivery::RouteStep& step : route) {
        if (step.kind == delivery::RouteStepKind::Command) {
            requireSuccess(game, step.command);
            continue;
        }
        int attempts = 0;
        while (!missionOf(game).encounterDefeated) {
            if (++attempts > delivery::kMaximumAssaultAttempts)
                throw std::runtime_error("攻击次数超过安全上限，岩牙巡逻仍未击退");
            requireSuccess(game, "attack");
            if (missionOf(game).squad.members[missionOf(game).squad.leaderIndex].life <= 0)
                throw std::runtime_error("攻击过程中队长生命归零，地图任务的钳制规则被破坏");
        }
    }
}

} // namespace

// ---------------------------------------------------------------- 验收一：十六地点地图与试玩路线

TEST_CASE("交付试玩路线每一步都沿相邻道路前进，覆盖十六地点并回到结算点") {
    // 1. 采集路线：静态校验每一步 move 都沿目录登记的相邻道路前进。
    std::array<bool, kLocationCount> visited{};
    WorldLocationId current = WorldLocationId::Camp;
    visited[tribe::indexOf(current)] = true;
    std::size_t moveSteps = 0;
    bool hasGather = false;
    bool hasSettle = false;
    bool hasAssault = false;
    for (const delivery::RouteStep& step : delivery::gatherMissionRoute()) {
        if (step.kind == delivery::RouteStepKind::AttackUntilDefeated) {
            hasAssault = true;
            continue;
        }
        const auto parsed = tribe::command_parser::parse(step.command);
        if (parsed.verb == "gather") hasGather = true;
        if (parsed.verb == "settle") hasSettle = true;
        // 撤退不是普通移动：地图规则把它固定落回岩牙要塞唯一相邻的古老山隘，静态校验也必须照此推进。
        if (parsed.verb == "retreat") {
            REQUIRE(current == WorldLocationId::RockfangFort);
            current = WorldLocationId::OldPass;
            ++moveSteps;
            continue;
        }
        if (parsed.verb != "move") continue;
        REQUIRE(parsed.args.size() == 1U);
        const auto target = tribe::world_map::parse(parsed.args.front());
        REQUIRE(target.has_value());
        REQUIRE(tribe::world_map::adjacent(current, *target));
        REQUIRE(tribe::world_map::roadDistance(current, *target) == 1);
        current = *target;
        visited[tribe::indexOf(current)] = true;
        ++moveSteps;
    }
    REQUIRE(moveSteps > 0U);
    REQUIRE(hasGather);
    REQUIRE(hasSettle);
    REQUIRE(hasAssault);
    // 营地是唯一起点与终点：路线必须闭环，否则玩家会在半路耗尽采集时段。
    REQUIRE(current == WorldLocationId::Camp);
    for (std::size_t index = 0U; index < kLocationCount; ++index)
        REQUIRE(visited[index]);

    // 2. 前哨路线：同样逐步相邻，且最后停在非营地的偏远地点用于建造与结算。
    current = WorldLocationId::Camp;
    bool hasOutpost = false;
    for (const delivery::RouteStep& step : delivery::outpostMissionRoute()) {
        const auto parsed = tribe::command_parser::parse(step.command);
        if (parsed.verb == "buildoutpost" || (parsed.verb == "build" && !parsed.args.empty()))
            hasOutpost = true;
        if (parsed.verb != "move") continue;
        const auto target = tribe::world_map::parse(parsed.args.front());
        REQUIRE(target.has_value());
        REQUIRE(tribe::world_map::adjacent(current, *target));
        current = *target;
    }
    REQUIRE(hasOutpost);
    REQUIRE(current == WorldLocationId::OldPass);
}

TEST_CASE("十六地点道路图双向连通，最短路、方位与地点档案可复现") {
    const auto& locations = tribe::world_map::locations();
    REQUIRE(locations.size() == kLocationCount);

    // 1. 连通性：从营地出发，十六个地点全部可达。
    for (std::size_t index = 0U; index < kLocationCount; ++index) {
        const auto location = static_cast<WorldLocationId>(index);
        REQUIRE(tribe::world_map::roadDistance(WorldLocationId::Camp, location) >= 0);
        REQUIRE(tribe::world_map::roadDistance(location, location) == 0);
        REQUIRE(tribe::world_map::roadPath(location, location).size() == 1U);
        REQUIRE(!tribe::world_map::nextStepToward(location, location).has_value());
    }

    // 2. 最短路性质：路径首尾正确、逐段相邻、长度等于 roadDistance、第一步必定减少剩余距离。
    for (std::size_t from = 0U; from < kLocationCount; ++from) {
        for (std::size_t to = 0U; to < kLocationCount; ++to) {
            const auto start = static_cast<WorldLocationId>(from);
            const auto finish = static_cast<WorldLocationId>(to);
            const std::vector<WorldLocationId> path = tribe::world_map::roadPath(start, finish);
            REQUIRE(!path.empty());
            REQUIRE(path.front() == start);
            REQUIRE(path.back() == finish);
            REQUIRE(static_cast<int>(path.size()) - 1 == tribe::world_map::roadDistance(start, finish));
            for (std::size_t index = 1U; index < path.size(); ++index) {
                REQUIRE(tribe::world_map::adjacent(path[index - 1U], path[index]));
                REQUIRE(!tribe::world_map::direction(path[index - 1U], path[index]).empty());
            }
            if (start != finish) {
                const auto step = tribe::world_map::nextStepToward(start, finish);
                REQUIRE(step.has_value());
                REQUIRE(tribe::world_map::adjacent(start, *step));
                REQUIRE(tribe::world_map::roadDistance(*step, finish) == tribe::world_map::roadDistance(start, finish) - 1);
            }
        }
    }

    // 3. 关键里程：营地到岩牙要塞必须走通 6 步（交付文档中的“远程行军”基准）。
    REQUIRE(tribe::world_map::roadDistance(WorldLocationId::Camp, WorldLocationId::RockfangFort) == 6);
    REQUIRE(tribe::world_map::roadPath(WorldLocationId::Camp, WorldLocationId::RockfangFort).size() == 7U);
    // 4. 岩牙要塞只有一条道路，撤退落点必须唯一且相邻。
    REQUIRE(locations[tribe::indexOf(WorldLocationId::RockfangFort)].neighbors.size() == 1U);
    REQUIRE(tribe::world_map::adjacent(WorldLocationId::RockfangFort, WorldLocationId::OldPass));

    // 5. 地点档案：风险等级覆盖 0 至 3，营地最安全、要塞最危险，非法地点有兜底档案而不抛异常。
    REQUIRE(tribe::world_map::profile(WorldLocationId::Camp).risk == 0);
    REQUIRE(tribe::world_map::profile(WorldLocationId::RockfangFort).risk == 3);
    REQUIRE(tribe::world_map::profile(WorldLocationId::OldPass).risk == 3);
    for (std::size_t index = 0U; index < kLocationCount; ++index) {
        const auto& profile = tribe::world_map::profile(static_cast<WorldLocationId>(index));
        REQUIRE(profile.risk >= 0);
        REQUIRE(profile.risk <= 3);
        REQUIRE(!profile.riskName.empty());
        REQUIRE(!profile.reward.empty());
        REQUIRE(!profile.hazard.empty());
    }
    const auto& unknown = tribe::world_map::profile(static_cast<WorldLocationId>(99));
    REQUIRE(!unknown.riskName.empty());
    REQUIRE(unknown.reward == tribe::world_map::profile(static_cast<WorldLocationId>(99)).reward);
    REQUIRE(tribe::world_map::roadPath(static_cast<WorldLocationId>(99), WorldLocationId::Camp).empty());
    REQUIRE(tribe::world_map::roadDistance(WorldLocationId::Camp, static_cast<WorldLocationId>(99)) == -1);
    REQUIRE(!tribe::world_map::nextStepToward(WorldLocationId::Camp, static_cast<WorldLocationId>(99)).has_value());
}

// ---------------------------------------------------------------- 验收二：路线提示（5.2 优化）

TEST_CASE("路线提示给出最近结算点、最近未探索地点与指定资源产地，且建议指令可直接执行") {
    // 1. 营地初始状态：本身是结算点，最近未探索地点应是相邻的 2.苍林（并列时取编号更小者）。
    ExpansionGame atCamp{201U, 4U};
    ExpansionState campState = atCamp.state();
    campState.assignedResource = ResourceKind::Wood;
    ExpansionGame camp{std::move(campState)};
    const std::string campHint = camp.routeHintText();
    REQUIRE(contains(campHint, "路线提示：当前地点即为结算点"));
    REQUIRE(contains(campHint, "探索提示：最近未探索地点 2.苍林"));
    REQUIRE(contains(campHint, "1步"));
    REQUIRE(contains(campHint, "输入 move 2"));

    // 2. 提示里的行动建议必须真的能执行：跟着提示走一步，距离与方向也随之更新。
    requireSuccess(camp, "move 2");
    REQUIRE(camp.state().worldLocation == tribe::indexOf(WorldLocationId::Forest));
    const std::string forestHint = camp.routeHintText();
    REQUIRE(contains(forestHint, "路线提示：最近结算点 1.燧火营地"));
    REQUIRE(contains(forestHint, "探索提示：最近未探索地点 4.芦苇沼泽"));
    // 苍林产木材，因此不应出现“此地不产”的资源错配提示。
    REQUIRE(!contains(forestHint, "此地不产"));

    // 3. 去到不产指定资源的地点：必须给出最近产地与可执行指令。
    ExpansionGame quarry = missionAt(WorldLocationId::Quarry, ResourceKind::Wood, 202U);
    const std::string quarryHint = quarry.routeHintText();
    REQUIRE(contains(quarryHint, "本次指定资源是木材，此地不产"));
    REQUIRE(contains(quarryHint, "最近产地 2.苍林"));
    // 燧石矿场到苍林的最短路是 3 步：矿场→红土原→营地→苍林（苍林只与营地、沼泽相邻）。
    REQUIRE(contains(quarryHint, "3步"));
    REQUIRE(tribe::world_map::roadDistance(WorldLocationId::Quarry, WorldLocationId::Forest) == 3);
    requireSuccess(quarry, "move 3");
    requireSuccess(quarry, "move 1");
    requireSuccess(quarry, "move 2");
    REQUIRE(quarry.state().worldLocation == tribe::indexOf(WorldLocationId::Forest));

    // 4. 食物任务在同时产兽皮的地点：提示允许顺带装载兽皮，而不是误导玩家另开任务。
    ExpansionGame hunting = missionAt(WorldLocationId::Forest, ResourceKind::Food, 203U);
    REQUIRE(contains(hunting.routeHintText(), "顺带装载兽皮"));
    // 提示与规则一致：食物任务确实能在苍林一次采集同时拿到两种资源。
    requireSuccess(hunting, "gather food");
    requireSuccess(hunting, "gather hides");
    REQUIRE(hunting.state().cargoFood > 0);
    REQUIRE(hunting.state().cargoHides > 0);

    // 5. 全部发现后不再提示探索目标；采集时段用尽后提示改为“只能结算”。
    ExpansionGame whole = missionAt(WorldLocationId::SaltwindCoast, ResourceKind::Food, 204U);
    ExpansionState explored = whole.state();
    explored.worldDiscovered.fill(true);
    explored.harvestActions = tribe::kMaximumHarvestActions;
    ExpansionGame completed{std::move(explored)};
    const std::string completedHint = completed.routeHintText();
    REQUIRE(contains(completedHint, "探索提示：十六地点已全部发现"));
    REQUIRE(contains(completedHint, "采集时段剩余0/4"));
    REQUIRE(contains(completedHint, "当前地点不是结算点"));
    // 提示文本只读取状态：连续生成两次必须完全一致，证明渲染路径没有任何副作用。
    REQUIRE(completed.routeHintText() == completed.routeHintText());
}

// ---------------------------------------------------------------- 验收三：采集、移动、前哨建设、结算

TEST_CASE("采集只在允许的地点与指定资源上成功，错误地点与混采一律被拒绝") {
    ExpansionGame mission = missionAt(WorldLocationId::Camp, ResourceKind::Wood, 301U);
    // 1. 营地产木材吗？不产：错误地点操作必须被拒绝，并且不推进回合。
    requireRejected(mission, "gather wood");
    REQUIRE(mission.state().harvestActions == 0);
    REQUIRE(mission.state().turn == 0);

    // 2. 未知资源、错误资源、错误地点三种失败路径。
    requireSuccess(mission, "move 2");
    requireRejected(mission, "gather stone");
    requireRejected(mission, "gather 不存在的资源");
    requireRejected(mission, "gather food");
    // 指定资源是木材，兽皮也不能混采（兽皮豁免只对食物任务生效）。
    requireRejected(mission, "gather hides");

    // 3. 正确地点 + 正确资源：装载、消耗采集时段、推进任务回合、队长疲劳上升。
    const int turnBefore = mission.state().turn;
    const int fatigueBefore = mission.state().squad.members[mission.state().squad.leaderIndex].fatigue;
    requireSuccess(mission, "gather wood");
    REQUIRE(mission.state().cargoWood == 6 + 2);
    REQUIRE(mission.state().harvestActions == 1);
    REQUIRE(mission.state().turn == turnBefore + 1);
    REQUIRE(mission.state().squad.members[mission.state().squad.leaderIndex].fatigue == fatigueBefore + 4);

    // 4. 中英文别名与多空格输入必须等价。
    ExpansionGame chinese = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 302U);
    ExpansionGame english = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 302U);
    requireSuccess(english, "  gather   wood  ");
    requireSuccess(chinese, "采集 木材");
    REQUIRE(english.state().cargoWood == chinese.state().cargoWood);
    REQUIRE(english.state().turn == chinese.state().turn);

    // 5. 食物任务可以顺带取得兽皮，这是唯一被允许的“混采”例外。
    ExpansionGame hunting = missionAt(WorldLocationId::Forest, ResourceKind::Food, 303U);
    requireSuccess(hunting, "gather food");
    requireSuccess(hunting, "gather hides");
    REQUIRE(hunting.state().cargoFood > 0);
    REQUIRE(hunting.state().cargoHides > 0);
}

TEST_CASE("采集时段与载货上限按常量截断，满载与时段耗尽都被拒绝") {
    // 1. 时段上限：一次任务最多 4 次采集，第 5 次必须被拒绝且不再消耗任何状态。
    ExpansionGame seeded = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 311U);
    ExpansionState roomy = seeded.state();
    roomy.cargoCapacity = 64;
    ExpansionGame mission{std::move(roomy)};
    for (int attempt = 0; attempt < tribe::kMaximumHarvestActions; ++attempt) requireSuccess(mission, "gather wood");
    REQUIRE(mission.state().harvestActions == tribe::kMaximumHarvestActions);
    REQUIRE(mission.state().cargoWood == 8 * tribe::kMaximumHarvestActions);
    requireRejected(mission, "gather wood");
    REQUIRE(mission.state().harvestActions == tribe::kMaximumHarvestActions);

    // 2. 载货上限：容量小的队伍会在用满时段前就装满，剩余采集请求被拒绝。
    ExpansionGame tiny = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 312U, 2U);
    ExpansionState tight = tiny.state();
    tight.cargoCapacity = 10;
    ExpansionGame full{std::move(tight)};
    requireSuccess(full, "gather wood");
    REQUIRE(full.state().cargoWood == 6);
    requireSuccess(full, "gather wood");
    REQUIRE(full.state().cargoWood == full.state().cargoCapacity);
    REQUIRE(full.state().harvestActions == 2);
    requireRejected(full, "gather wood");
    REQUIRE(full.state().harvestActions == 2);

    // 3. 常量与状态校验保持一致：harvestActions 超过上限的状态必须被判为非法。
    ExpansionState illegal = full.state();
    illegal.harvestActions = tribe::kMaximumHarvestActions + 1;
    const tribe::OperationResult checked = ExpansionGame::validateState(illegal);
    REQUIRE(!checked);
    REQUIRE(contains(checked.message, "载货"));

    // 4. 载货超出容量的状态同样非法，避免存档里出现“超载小队”。
    ExpansionState overloaded = full.state();
    overloaded.cargoWood = overloaded.cargoCapacity + 1;
    REQUIRE(!ExpansionGame::validateState(overloaded));
}

TEST_CASE("移动只沿相邻道路前进，跨越地图与原地打转都被拒绝") {
    ExpansionGame mission = missionAt(WorldLocationId::Camp, ResourceKind::Wood, 321U);
    // 1. 不相邻：营地到燧石矿场必须经过红土原，不能跳格。
    requireRejected(mission, "move 7");
    requireRejected(mission, "move quarry");
    // 2. 未知地点与原地停留。
    requireRejected(mission, "move 17");
    requireRejected(mission, "move 不存在的地点");
    requireRejected(mission, "move 1");
    // 3. 参数不足：move 缺少目标时必须被识别后拒绝。
    requireRejected(mission, "move");

    // 4. 正常移动：发现新地点、推进回合、队长疲劳 +2、队员 +1。
    const int turnBefore = mission.state().turn;
    requireSuccess(mission, "move 2");
    REQUIRE(mission.state().worldLocation == tribe::indexOf(WorldLocationId::Forest));
    REQUIRE(mission.state().worldDiscovered[tribe::indexOf(WorldLocationId::Forest)]);
    REQUIRE(mission.state().turn == turnBefore + 1);
    const auto& members = mission.state().squad.members;
    REQUIRE(members[mission.state().squad.leaderIndex].fatigue == 2);
    REQUIRE(members[(mission.state().squad.leaderIndex + 1U) % members.size()].fatigue == 1);

    // 5. 返回已发现的地点不会重复报告“新地点已发现”。
    const ExpansionCommandResult again = requireSuccess(mission, "move 1");
    REQUIRE(!contains(again.message, "新地点已发现"));
    REQUIRE(mission.state().worldLocation == tribe::indexOf(WorldLocationId::Camp));

    // 6. 已结算的任务不再接受任何移动。
    requireSuccess(mission, "settle");
    requireRejected(mission, "move 2");
}

TEST_CASE("前哨建设只消耗现场携带材料，并在错误地点与材料不足时被拒绝") {
    const std::size_t marsh = tribe::indexOf(WorldLocationId::Marsh);
    // 1. 材料不足：木材 5 / 石料 4 必须被拒绝，且不消耗任何材料。
    ExpansionGame poor = missionAt(WorldLocationId::Marsh, ResourceKind::Wood, 331U);
    ExpansionState poorState = poor.state();
    poorState.missionKind = tribe::MissionKind::OutpostConstruction;
    poorState.cargoWood = 5;
    poorState.cargoStone = 4;
    ExpansionGame lacking{std::move(poorState)};
    requireRejected(lacking, "build outpost");
    REQUIRE(lacking.state().cargoWood == 5);
    REQUIRE(!lacking.state().outposts[marsh]);

    // 2. 营地里不能建造前哨：营地本身已经是结算点。
    ExpansionGame atCamp = missionAt(WorldLocationId::Camp, ResourceKind::Wood, 332U);
    ExpansionState campState = atCamp.state();
    campState.missionKind = tribe::MissionKind::OutpostConstruction;
    campState.cargoWood = 6;
    campState.cargoStone = 4;
    ExpansionGame campBuild{std::move(campState)};
    requireRejected(campBuild, "build outpost");

    // 3. 正常建造：消耗木材 6、石料 4，地点变成结算点，可以就地结算。
    ExpansionGame builder = missionAt(WorldLocationId::Marsh, ResourceKind::Wood, 333U);
    ExpansionState builderState = builder.state();
    builderState.missionKind = tribe::MissionKind::OutpostConstruction;
    builderState.cargoWood = 6;
    builderState.cargoStone = 4;
    ExpansionGame building{std::move(builderState)};
    requireSuccess(building, "build outpost");
    REQUIRE(building.state().outposts[marsh]);
    REQUIRE(building.state().cargoWood == 0);
    REQUIRE(building.state().cargoStone == 0);
    // 4. 重复建造必须被拒绝。
    requireRejected(building, "build outpost");
    requireSuccess(building, "settle");
    REQUIRE(building.state().phase == ExpansionPhase::Settled);
    REQUIRE(building.state().settled);

    // 5. 岩牙要塞：未击退巡逻前不能建造前哨，击退后才能建立前进基地。
    const std::size_t fort = tribe::indexOf(WorldLocationId::RockfangFort);
    ExpansionGame fortBase = fortMission(334U);
    ExpansionState fortState = fortBase.state();
    fortState.missionKind = tribe::MissionKind::OutpostConstruction;
    fortState.cargoWood = 6;
    fortState.cargoStone = 4;
    ExpansionGame besieged{std::move(fortState)};
    requireRejected(besieged, "build outpost");
    REQUIRE(!besieged.state().outposts[fort]);
    // 击退巡逻（默认队长对 14 点巡逻生命每次造成 4 点伤害）。
    int assaults = 0;
    while (!besieged.state().encounterDefeated) {
        REQUIRE(++assaults <= delivery::kMaximumAssaultAttempts);
        requireSuccess(besieged, "attack");
    }
    requireSuccess(besieged, "build outpost");
    REQUIRE(besieged.state().outposts[fort]);
    requireSuccess(besieged, "settle");
    REQUIRE(besieged.state().phase == ExpansionPhase::Settled);
}

TEST_CASE("结算只能在营地或已建前哨完成，并且是载货入账的唯一入口") {
    // 1. 普通地点不能结算。
    ExpansionGame field = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 341U);
    requireSuccess(field, "gather wood");
    const int carried = field.state().cargoWood;
    REQUIRE(carried > 0);
    requireRejected(field, "settle");
    REQUIRE(field.state().phase == ExpansionPhase::Exploring);
    REQUIRE(!field.state().settled);

    // 2. 回到营地可以结算：阶段变为 Settled，载货保留给上层入账。
    requireSuccess(field, "move 1");
    requireSuccess(field, "settle");
    REQUIRE(field.state().phase == ExpansionPhase::Settled);
    REQUIRE(field.state().settled);
    REQUIRE(field.state().cargoWood == carried);

    // 3. 结算后地图命令全部关闭（含草药与遭遇指令，见 5.2 修复的 phase 门禁）。
    requireRejected(field, "settle");
    requireRejected(field, "move 2");
    requireRejected(field, "gather wood");
    requireRejected(field, "build outpost");
    requireRejected(field, "使用 草药");
    requireRejected(field, "attack");
    requireRejected(field, "defend");
    requireRejected(field, "retreat");

    // 4. 结算会给队员经验与凝聚力：证明结算是一个真实的进度事件而不是纯标记。
    ExpansionGame reward = missionAt(WorldLocationId::Forest, ResourceKind::Wood, 342U);
    const int experienceBefore = reward.state().squad.members.front().experience;
    const int cohesionBefore = reward.state().squad.cohesion;
    requireSuccess(reward, "gather wood");
    requireSuccess(reward, "move 1");
    requireSuccess(reward, "settle");
    REQUIRE(reward.state().squad.members.front().experience > experienceBefore);
    REQUIRE(reward.state().squad.cohesion > cohesionBefore);

    // 5. 上层引擎：只有 settle 才把载货写进部落仓库，且只写一次。
    GameEngine game{{GameMode::Quick, 343U}};
    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "mission wood");
    const int woodInStore = game.state().wood;
    requireSuccess(game, "move 2");
    requireSuccess(game, "gather wood");
    requireSuccess(game, "gather wood");
    REQUIRE(game.state().wood == woodInStore);
    const int loaded = missionOf(game).cargoWood;
    REQUIRE(loaded == 12);
    requireSuccess(game, "move 1");
    requireSuccess(game, "settle");
    REQUIRE(game.state().wood == woodInStore + loaded);
    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!game.state().activeMission.has_value());
    REQUIRE(game.state().missionCount == 1);
}

TEST_CASE("未结算的载货不进入部落仓库：放弃任务与存档往返都不入账") {
    // 1. 放弃任务：载货被丢弃、任务不计数、稳定度下降，但仓库一个资源都不增加。
    GameEngine game{{GameMode::Quick, 351U}};
    requireSuccess(game, "assign wood 2");
    const int woodInStore = game.state().wood;
    const int stabilityBefore = game.state().stability;
    requireSuccess(game, "mission wood");
    requireSuccess(game, "move 2");
    requireSuccess(game, "gather wood");
    requireSuccess(game, "gather wood");
    REQUIRE(missionOf(game).cargoWood > 0);
    const int missionCountBefore = game.state().missionCount;
    requireSuccess(game, "abort");
    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!game.state().activeMission.has_value());
    REQUIRE(game.state().wood == woodInStore);
    REQUIRE(game.state().missionCount == missionCountBefore);
    REQUIRE(game.state().stability == stabilityBefore - 2);
    REQUIRE(game.state().squads.front().station == WorldLocationId::Camp);

    // 2. 存档往返：任务中的载货被完整保存，但读取后依然没有进入部落仓库。
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "tribe-member3-delivery-saves";
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    const tribe::SaveRepository saves{root};

    GameEngine roundTrip{{GameMode::Quick, 352U}};
    requireSuccess(roundTrip, "assign wood 2");
    requireSuccess(roundTrip, "mission wood");
    requireSuccess(roundTrip, "move 2");
    requireSuccess(roundTrip, "gather wood");
    const int storeBefore = roundTrip.state().wood;
    const int cargoBefore = missionOf(roundTrip).cargoWood;
    const int turnBefore = missionOf(roundTrip).turn;
    REQUIRE(cargoBefore > 0);

    std::string error;
    REQUIRE(saves.save(roundTrip.state(), tribe::SaveSlot::Slot1, error));
    GameState loaded;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(loaded.wood == storeBefore);
    REQUIRE(loaded.activeMission.has_value());
    REQUIRE(loaded.activeMission->cargoWood == cargoBefore);
    REQUIRE(loaded.activeMission->turn == turnBefore);
    REQUIRE(loaded.activeMission->phase == ExpansionPhase::Exploring);

    // 3. 读取出的任务可以继续结算，并且刚好入账一次。
    GameEngine resumed{std::move(loaded)};
    requireSuccess(resumed, "move 1");
    requireSuccess(resumed, "settle");
    REQUIRE(resumed.state().wood == storeBefore + cargoBefore);
    std::filesystem::remove_all(root, cleanup);
}

TEST_CASE("行动点不足或疲劳过高时任务无法出发，而地图命令不额外消耗季节行动点") {
    // 1. 行动点用尽：任务无法出发，状态保持经营阶段。
    GameEngine noActions{{GameMode::Quick, 361U}};
    GameState exhausted = noActions.state();
    exhausted.actionsLeft = 0;
    exhausted.workforce.woodCrew = 2;
    GameEngine blocked{std::move(exhausted)};
    const tribe::ActionResult noAction = requireRejected(blocked, "mission wood");
    REQUIRE(contains(noAction.message, "行动点"));
    REQUIRE(blocked.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!blocked.state().activeMission.has_value());

    // 2. 资源队人数不足：必须先分配劳力。
    GameEngine noCrew{{GameMode::Quick, 362U}};
    const tribe::ActionResult thin = requireRejected(noCrew, "mission wood");
    REQUIRE(contains(thin.message, "劳力"));
    REQUIRE(!noCrew.state().activeMission.has_value());

    // 3. 前哨建设材料不足：仓库木材只有 5 时不能出发。
    GameEngine shortStore{{GameMode::Quick, 363U}};
    GameState scarce = shortStore.state();
    scarce.workforce.woodCrew = 2;
    scarce.workforce.stoneCrew = 2;
    scarce.wood = 5;
    scarce.stone = 4;
    GameEngine lacking{std::move(scarce)};
    const tribe::ActionResult materials = requireRejected(lacking, "mission outpost");
    REQUIRE(contains(materials.message, "木材6"));
    REQUIRE(!lacking.state().activeMission.has_value());

    // 4. 小队疲劳过高：必须先休整。
    GameEngine tired{{GameMode::Quick, 364U}};
    GameState weary = tired.state();
    weary.workforce.woodCrew = 2;
    weary.squads.front().fatigue = 85;
    GameEngine exhaustedSquad{std::move(weary)};
    const tribe::ActionResult fatigue = requireRejected(exhaustedSquad, "mission wood");
    REQUIRE(contains(fatigue.message, "疲劳"));
    REQUIRE(!exhaustedSquad.state().activeMission.has_value());

    // 5. 成功出发消耗 1 点行动点，之后整个任务过程都不再消耗季节行动点。
    GameEngine game{{GameMode::Quick, 365U}};
    requireSuccess(game, "assign wood 2");
    const int actionsBefore = game.state().actionsLeft;
    requireSuccess(game, "mission wood");
    REQUIRE(game.state().actionsLeft == actionsBefore - 1);
    const int actionsOnMap = game.state().actionsLeft;
    requireSuccess(game, "move 2");
    requireSuccess(game, "gather wood");
    requireSuccess(game, "move 1");
    requireSuccess(game, "settle");
    REQUIRE(game.state().actionsLeft == actionsOnMap);
    // 地图上的每次行动改为推进任务回合，而不是季节回合。
    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
}

// ---------------------------------------------------------------- 验收四：遭遇与状态校验

TEST_CASE("岩牙要塞遭遇：攻击、防御、撤退与遭遇期间的移动封锁") {
    ExpansionGame mission = fortMission(371U);
    // 1. 没有遭遇时防御和撤退都必须被拒绝；要塞只允许主动开战。
    requireRejected(mission, "defend");
    requireRejected(mission, "retreat");
    requireRejected(mission, "attack 多余参数");

    // 2. 主动攻击开启遭遇：敌军生命从 14 开始下降，队长生命下降但不低于 1。
    requireSuccess(mission, "attack");
    REQUIRE(mission.state().encounterLife > 0);
    REQUIRE(mission.state().encounterLife < 14);
    REQUIRE(!mission.state().encounterDefeated);
    const int leaderLife = mission.state().squad.members[mission.state().squad.leaderIndex].life;
    REQUIRE(leaderLife >= 1);

    // 3. 遭遇期间道路被封锁：移动、采集、建造、结算全部被拒绝，状态保持不变。
    requireRejected(mission, "move 8");
    requireRejected(mission, "gather wood");
    requireRejected(mission, "build outpost");
    requireRejected(mission, "settle");
    requireRejected(mission, "使用 草药");

    // 4. 防御是可用选项，并且疲劳增量低于攻击。
    const int fatigueBefore = mission.state().squad.members[mission.state().squad.leaderIndex].fatigue;
    requireSuccess(mission, "defend");
    REQUIRE(mission.state().encounterLife > 0);
    REQUIRE(mission.state().squad.members[mission.state().squad.leaderIndex].fatigue == fatigueBefore + 2);

    // 5. 撤退：回到唯一相邻的古老山隘，遭遇清零，未击退状态保留。
    requireSuccess(mission, "retreat");
    REQUIRE(mission.state().worldLocation == tribe::indexOf(WorldLocationId::OldPass));
    REQUIRE(mission.state().encounterLife == 0);
    REQUIRE(!mission.state().encounterDefeated);

    // 6. 再次进入并彻底击退：获得任务背包中的徽记，并且能在要塞建造前哨。
    requireSuccess(mission, "move 9");
    int assaults = 0;
    while (!mission.state().encounterDefeated) {
        REQUIRE(++assaults <= delivery::kMaximumAssaultAttempts);
        requireSuccess(mission, "attack");
        REQUIRE(mission.state().squad.members[mission.state().squad.leaderIndex].life >= 1);
    }
    REQUIRE(mission.state().encounterLife == 0);
    REQUIRE(mission.state().outposts[tribe::indexOf(WorldLocationId::Camp)]);
    REQUIRE(!mission.state().backpack.items().empty());
    requireRejected(mission, "attack");
    requireRejected(mission, "defend");
    requireRejected(mission, "retreat");
    // 击退后道路恢复通行。
    requireSuccess(mission, "move 8");
    REQUIRE(mission.state().worldLocation == tribe::indexOf(WorldLocationId::OldPass));

    // 7. 撤退落点必须始终合法：即使候选状态里古老山隘尚未登记为已发现，
    //    撤退也必须成功落地并顺手把它标记为已发现，而不是被状态校验整体取消后把小队困在遭遇里。
    ExpansionGame artificial = missionAt(WorldLocationId::RockfangFort, ResourceKind::Wood, 372U);
    REQUIRE(!artificial.state().worldDiscovered[tribe::indexOf(WorldLocationId::OldPass)]);
    requireSuccess(artificial, "attack");
    requireSuccess(artificial, "retreat");
    REQUIRE(artificial.state().worldLocation == tribe::indexOf(WorldLocationId::OldPass));
    REQUIRE(artificial.state().worldDiscovered[tribe::indexOf(WorldLocationId::OldPass)]);
}

TEST_CASE("地图任务状态校验拒绝越权地点、越权结算与不一致的遭遇字段") {
    ExpansionGame base{381U, 4U};
    const ExpansionState good = base.state();
    REQUIRE(ExpansionGame::validateState(good));

    const auto requireInvalid = [](const ExpansionState& state) {
        const tribe::OperationResult result = ExpansionGame::validateState(state);
        REQUIRE(!result);
        REQUIRE(!result.message.empty());
    };

    // 1. 地点相关：越界编号、未发现的地点、营地必须已发现且是前哨。
    ExpansionState outOfRange = good;
    outOfRange.worldLocation = 16;
    requireInvalid(outOfRange);
    ExpansionState negative = good;
    negative.worldLocation = -1;
    requireInvalid(negative);
    ExpansionState undiscovered = good;
    undiscovered.worldLocation = static_cast<int>(tribe::indexOf(WorldLocationId::RockfangFort));
    requireInvalid(undiscovered);
    ExpansionState campLost = good;
    campLost.worldDiscovered[0] = false;
    requireInvalid(campLost);
    ExpansionState campOutpostLost = good;
    campOutpostLost.outposts[0] = false;
    requireInvalid(campOutpostLost);

    // 2. 越权结算：结算标记与阶段必须一致，并且只能停在已建前哨的地点。
    ExpansionState settledInField = good;
    settledInField.settled = true;
    requireInvalid(settledInField);
    ExpansionState settledWithoutOutpost = good;
    settledWithoutOutpost.worldLocation = static_cast<int>(tribe::indexOf(WorldLocationId::Forest));
    settledWithoutOutpost.worldDiscovered[tribe::indexOf(WorldLocationId::Forest)] = true;
    settledWithoutOutpost.phase = ExpansionPhase::Settled;
    settledWithoutOutpost.settled = true;
    requireInvalid(settledWithoutOutpost);
    ExpansionState settledFlagMissing = good;
    settledFlagMissing.phase = ExpansionPhase::Settled;
    requireInvalid(settledFlagMissing);

    // 3. 遭遇字段组合：巡逻只存在于岩牙要塞，且击退后不能残留敌军生命。
    ExpansionState encounterElsewhere = good;
    encounterElsewhere.encounterLife = 5;
    requireInvalid(encounterElsewhere);
    ExpansionState defeatedWithLife = good;
    defeatedWithLife.worldLocation = static_cast<int>(tribe::indexOf(WorldLocationId::RockfangFort));
    defeatedWithLife.worldDiscovered[tribe::indexOf(WorldLocationId::RockfangFort)] = true;
    defeatedWithLife.encounterDefeated = true;
    defeatedWithLife.encounterLife = 3;
    requireInvalid(defeatedWithLife);
    ExpansionState validEncounter = good;
    validEncounter.worldLocation = static_cast<int>(tribe::indexOf(WorldLocationId::RockfangFort));
    validEncounter.worldDiscovered[tribe::indexOf(WorldLocationId::RockfangFort)] = true;
    validEncounter.encounterLife = 7;
    REQUIRE(ExpansionGame::validateState(validEncounter));

    // 4. 数值与枚举边界。
    ExpansionState negativeCargo = good;
    negativeCargo.cargoStone = -1;
    requireInvalid(negativeCargo);
    ExpansionState zeroCapacity = good;
    zeroCapacity.cargoCapacity = 0;
    requireInvalid(zeroCapacity);
    ExpansionState tinyCrew = good;
    tinyCrew.crewSize = 1;
    requireInvalid(tinyCrew);
    ExpansionState badResource = good;
    badResource.assignedResource = static_cast<ResourceKind>(99);
    requireInvalid(badResource);
    ExpansionState badKind = good;
    badKind.missionKind = static_cast<tribe::MissionKind>(9);
    requireInvalid(badKind);

    // 5. 非法状态不能被接管为任务对象：构造函数必须抛异常而不是带着错误状态运行。
    bool threw = false;
    try {
        const ExpansionGame rejected{outOfRange};
        (void)rejected;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    REQUIRE(threw);
}

// ---------------------------------------------------------------- 端到端：真实引擎跑完整条交付路线

TEST_CASE("交付试玩路线在真实引擎上跑通：采集、全图探索、遭遇、前哨建设与前哨结算") {
    GameEngine game{{GameMode::Standard, 20250915U}};
    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "assign stone 2");
    const int woodInStore = game.state().wood;
    const int stoneInStore = game.state().stone;
    const int actionsBefore = game.state().actionsLeft;
    REQUIRE(std::count(game.state().discovered.begin(), game.state().discovered.end(), true) == 3);

    // 第一段：木材采集任务，走遍十六地点后回营结算。
    requireSuccess(game, "mission wood");
    REQUIRE(game.state().phase == tribe::GamePhase::Mission);
    REQUIRE(missionOf(game).cargoCapacity == 16 + 2 * 4);
    runDeliveryRoute(game, delivery::gatherMissionRoute());

    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!game.state().activeMission.has_value());
    REQUIRE(game.state().missionCount == 1);
    REQUIRE(std::count(game.state().discovered.begin(), game.state().discovered.end(), true) ==
            static_cast<long>(kLocationCount));
    REQUIRE(game.state().wood > woodInStore);
    REQUIRE(game.state().stone == stoneInStore);
    const int woodAfterGather = game.state().wood;

    // 第二段：前哨建设任务，在古老山隘建立前进基地并在前哨结算。
    requireSuccess(game, "mission outpost");
    REQUIRE(missionOf(game).missionKind == tribe::MissionKind::OutpostConstruction);
    REQUIRE(missionOf(game).cargoWood == 6);
    REQUIRE(missionOf(game).cargoStone == 4);
    runDeliveryRoute(game, delivery::outpostMissionRoute());

    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(game.state().missionCount == 2);
    REQUIRE(game.state().outposts[tribe::indexOf(WorldLocationId::OldPass)]);
    REQUIRE(game.state().wood == woodAfterGather - 6);
    REQUIRE(game.state().stone == stoneInStore - 4);
    REQUIRE(game.state().squads.front().station == WorldLocationId::OldPass);
    // 两次任务各消耗 1 点行动点，地图内部的移动与采集不再额外消耗。
    REQUIRE(game.state().actionsLeft == actionsBefore - 2);
    REQUIRE(game.squadText().find("古老山隘") != std::string::npos);
}
