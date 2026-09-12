#include "tribe/expansion_game.hpp"
#include "tribe/game_engine.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <string>

namespace {

tribe::ExpansionCommandResult requireSuccess(tribe::ExpansionGame& game, const std::string& command) {
    const tribe::ExpansionCommandResult result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

tribe::ActionResult requireSuccess(tribe::GameEngine& game, const std::string& command) {
    const tribe::ActionResult result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

void requireRejectedWithoutMove(tribe::ExpansionGame& game, const std::string& command) {
    const tribe::ExpansionState before = game.state();
    const tribe::ExpansionCommandResult result = game.execute(command);
    REQUIRE(!result.success);
    REQUIRE(!result.stateChanged);
    REQUIRE(game.state().worldLocation == before.worldLocation);
    REQUIRE(game.state().turn == before.turn);
    REQUIRE(game.state().cargoFood == before.cargoFood);
    REQUIRE(game.state().cargoWood == before.cargoWood);
    REQUIRE(game.state().cargoStone == before.cargoStone);
    REQUIRE(game.state().harvestActions == before.harvestActions);
}

} // namespace

TEST_CASE("map missions reject nonadjacent roads and wrong local resources without changing state") {
    tribe::ExpansionGame seeded{101U, 4U};
    tribe::ExpansionState state = seeded.state();
    state.assignedResource = tribe::ResourceKind::Wood;
    tribe::ExpansionGame mission{std::move(state)};
    requireRejectedWithoutMove(mission, "move quarry");
    requireSuccess(mission, "move forest");
    requireRejectedWithoutMove(mission, "move quarry");
    requireRejectedWithoutMove(mission, "gather stone");
    requireSuccess(mission, "gather wood");
    REQUIRE(mission.state().cargoWood > 0);
    REQUIRE(mission.state().harvestActions == 1);
}

TEST_CASE("map mission cargo capacity caps harvesting and preserves a full load") {
    tribe::ExpansionGame seeded{102U, 6U};
    tribe::ExpansionState state = seeded.state();
    state.crewSize = 6;
    state.assignedResource = tribe::ResourceKind::Wood;
    tribe::ExpansionGame mission{std::move(state)};

    requireSuccess(mission, "move forest");
    requireSuccess(mission, "gather wood");
    requireSuccess(mission, "gather wood");
    requireSuccess(mission, "gather wood");
    REQUIRE(mission.state().cargoWood == mission.state().cargoCapacity);
    requireRejectedWithoutMove(mission, "gather wood");
}

TEST_CASE("outpost construction spends only carried materials and creates a settlement point") {
    tribe::ExpansionGame seeded{103U, 4U};
    tribe::ExpansionState state = seeded.state();
    state.worldLocation = 1;
    state.worldDiscovered[1] = true;
    state.missionKind = tribe::MissionKind::OutpostConstruction;
    state.cargoWood = 6;
    state.cargoStone = 4;
    tribe::ExpansionGame mission{std::move(state)};

    requireSuccess(mission, "build outpost");
    REQUIRE(mission.state().outposts[1]);
    REQUIRE(mission.state().cargoWood == 0);
    REQUIRE(mission.state().cargoStone == 0);
    requireSuccess(mission, "settle");
    REQUIRE(mission.state().phase == tribe::ExpansionPhase::Settled);
    REQUIRE(mission.state().settled);
}

TEST_CASE("fort encounter blocks travel until retreat and keeps the route state coherent") {
    tribe::ExpansionGame seeded{104U, 4U};
    tribe::ExpansionState state = seeded.state();
    state.worldLocation = 8;
    state.worldDiscovered[7] = true;
    state.worldDiscovered[8] = true;
    tribe::ExpansionGame mission{std::move(state)};

    requireSuccess(mission, "attack");
    REQUIRE(mission.state().encounterLife > 0);
    requireRejectedWithoutMove(mission, "move pass");
    requireSuccess(mission, "retreat");
    REQUIRE(mission.state().encounterLife == 0);
    REQUIRE(mission.state().worldLocation == 7);
}

TEST_CASE("settling a gathered map load is the only path that credits the tribe store") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 105U}};
    const int woodBefore = game.state().wood;

    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "mission wood");
    requireSuccess(game, "move forest");
    requireSuccess(game, "gather wood");
    REQUIRE(game.state().wood == woodBefore);
    requireSuccess(game, "move camp");
    requireSuccess(game, "settle");

    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!game.state().activeMission.has_value());
    REQUIRE(game.state().wood > woodBefore);
}
