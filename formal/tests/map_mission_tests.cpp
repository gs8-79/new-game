#include "tribe/expansion_game.hpp"
#include "tribe/game_engine.hpp"
#include "test_harness.hpp"
#include "world_map_catalog.hpp"

#include <array>
#include <string_view>
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

TEST_CASE("the sixteen-location catalog keeps stable ids aliases bidirectional roads and road-map coverage") {
    const auto& locations = tribe::world_map::locations();
    REQUIRE(locations.size() == tribe::kWorldLocationCount);

    std::array<bool, tribe::kWorldLocationCount> drawn{};
    for (const tribe::world_map::RoadRow& row : tribe::world_map::roadRows()) {
        REQUIRE(!row.nodes.empty());
        for (const tribe::WorldLocationId location : row.nodes) {
            const std::size_t index = tribe::indexOf(location);
            REQUIRE(index < locations.size());
            REQUIRE(!drawn[index]);
            REQUIRE(!tribe::world_map::shortName(location).empty());
            drawn[index] = true;
        }
    }

    for (std::size_t index = 0U; index < locations.size(); ++index) {
        const tribe::WorldLocationId location = static_cast<tribe::WorldLocationId>(index);
        REQUIRE(tribe::world_map::fromMissionIndex(static_cast<int>(index)).has_value());
        REQUIRE(*tribe::world_map::fromMissionIndex(static_cast<int>(index)) == location);
        REQUIRE(tribe::world_map::parse(std::to_string(index + 1U)).has_value());
        REQUIRE(*tribe::world_map::parse(std::to_string(index + 1U)) == location);
        REQUIRE(tribe::world_map::parse(locations[index].name).has_value());
        REQUIRE(*tribe::world_map::parse(locations[index].name) == location);
        REQUIRE(drawn[index]);

        for (const tribe::WorldLocationId neighbor : locations[index].neighbors) {
            REQUIRE(tribe::world_map::adjacent(location, neighbor));
            REQUIRE(tribe::world_map::adjacent(neighbor, location));
            REQUIRE(!tribe::world_map::direction(location, neighbor).empty());
        }
    }
    REQUIRE(!tribe::world_map::fromMissionIndex(-1).has_value());
    REQUIRE(!tribe::world_map::fromMissionIndex(16).has_value());
    REQUIRE(!tribe::world_map::parse("不存在地点").has_value());
    REQUIRE(tribe::world_map::supportsResource(tribe::WorldLocationId::Forest, tribe::ResourceKind::Food));
    REQUIRE(tribe::world_map::supportsResource(tribe::WorldLocationId::RiverFord, tribe::ResourceKind::Food));
    REQUIRE(!tribe::world_map::supportsResource(tribe::WorldLocationId::Camp, tribe::ResourceKind::Food));
    REQUIRE(!tribe::world_map::adjacent(static_cast<tribe::WorldLocationId>(99), tribe::WorldLocationId::Camp));
    REQUIRE(tribe::world_map::shortName(static_cast<tribe::WorldLocationId>(99)).empty());
}

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

TEST_CASE("map commands share Chinese English and direct-outpost aliases without changing their contracts") {
    tribe::ExpansionGame english{106U, 4U};
    tribe::ExpansionState englishState = english.state();
    englishState.assignedResource = tribe::ResourceKind::Wood;
    english = tribe::ExpansionGame{englishState};
    tribe::ExpansionGame chinese{std::move(englishState)};

    requireSuccess(english, "  move   forest  ");
    requireSuccess(chinese, "移动 苍林");
    REQUIRE(english.state().worldLocation == chinese.state().worldLocation);
    requireSuccess(english, "gather wood");
    requireSuccess(chinese, "采集 木材");
    REQUIRE(english.state().cargoWood == chinese.state().cargoWood);
    REQUIRE(english.state().turn == chinese.state().turn);

    tribe::ExpansionGame seeded{107U, 4U};
    tribe::ExpansionState state = seeded.state();
    state.worldLocation = 1;
    state.worldDiscovered[1] = true;
    state.missionKind = tribe::MissionKind::OutpostConstruction;
    state.cargoWood = 6;
    state.cargoStone = 4;
    tribe::ExpansionGame directAlias{std::move(state)};
    requireSuccess(directAlias, "建造前哨 多余参数");
    REQUIRE(directAlias.state().outposts[1]);
}
