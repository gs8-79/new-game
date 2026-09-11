#include "tribe/console_ui.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>

namespace {

tribe::ActionResult requireSuccess(tribe::GameEngine& game, const std::string& command) {
    const tribe::ActionResult result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

std::string stateSnapshot(const tribe::GameState& state) {
    static unsigned int snapshotNumber = 0;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("tribe-current-state-snapshot-" + std::to_string(++snapshotNumber));
    tribe::SaveRepository saves{root};
    std::string error;
    if (!saves.save(state, tribe::SaveSlot::Slot1, error))
        throw std::runtime_error("could not snapshot game state: " + error);

    const std::filesystem::path path = saves.pathFor(tribe::SaveSlot::Slot1);
    std::ifstream input(path, std::ios::binary);
    const std::string snapshot{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    return snapshot;
}

void requireRejectedWithoutChange(tribe::GameEngine& game, const std::string& command) {
    const std::string before = stateSnapshot(game.state());
    const tribe::ActionResult result = game.execute(command);
    REQUIRE(!result.success);
    REQUIRE(!result.stateChanged);
    REQUIRE(stateSnapshot(game.state()) == before);
}

tribe::GameEngine preparedWorkshopGame() {
    tribe::GameEngine base{{tribe::GameMode::Quick, 23U}};
    tribe::GameState state = base.state();
    state.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
    state.workforce.crafters = 1;
    state.wood = 30;
    state.stone = 30;
    state.hides = 10;
    state.herbs = 10;
    state.actionsLeft = 7;
    return tribe::GameEngine{std::move(state)};
}

} // namespace

TEST_CASE("direct gathering and scouting commands are absent outside the map") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 11U}};
    for (const std::string& command : {"gather wood", "采集 木材", "scout forest", "侦察 苍林"}) {
        const std::string before = stateSnapshot(game.state());
        const tribe::ActionResult result = game.execute(command);
        REQUIRE(!result.success);
        REQUIRE(!result.stateChanged);
        REQUIRE(stateSnapshot(game.state()) == before);
    }
}

TEST_CASE("help and workforce commands expose the current simplified rules") {
    std::ostringstream output;
    tribe::ConsoleUI ui{output, false, false, 80U};
    ui.renderHelpPage(1);
    REQUIRE(output.str().find("快速8季") != std::string::npos);
    REQUIRE(output.str().find("第9季") == std::string::npos);

    tribe::GameEngine game{{tribe::GameMode::Quick, 13U}};
    ui.renderGame(game, {});
    REQUIRE(output.str().find("3劳力") != std::string::npos);
    REQUIRE(output.str().find("4仓库") != std::string::npos);

    const tribe::ActionResult workforce = game.execute("3");
    REQUIRE(workforce.success);
    REQUIRE(!workforce.stateChanged);
    REQUIRE(workforce.message.find("劳力分工") != std::string::npos);

    const tribe::ActionResult inventory = game.execute("4");
    REQUIRE(inventory.success);
    REQUIRE(!inventory.stateChanged);
    REQUIRE(inventory.message.find("共享装备仓库") != std::string::npos);

    requireRejectedWithoutChange(game, "assign crafters 2");
    requireSuccess(game, "assign crafters 1");
    REQUIRE(game.state().workforce.crafters == 1);
}

TEST_CASE("hides can be offered for trade without requiring shells") {
    tribe::GameEngine base{{tribe::GameMode::Quick, 17U}};
    tribe::GameState state = base.state();
    state.hides = 4;
    state.shells = 0;
    state.discovered[tribe::indexOf(tribe::WorldLocationId::RiverFord)] = true;
    tribe::GameEngine game{std::move(state)};

    const int woodBefore = game.state().wood;
    requireSuccess(game, "trade river hides wood");
    REQUIRE(game.state().hides == 0);
    REQUIRE(game.state().wood > woodBefore);
}

TEST_CASE("each new mode starts at the first season with its advertised length") {
    const std::array<std::pair<tribe::GameMode, int>, 3> modes{{
        {tribe::GameMode::Quick, 8},
        {tribe::GameMode::Standard, 16},
        {tribe::GameMode::Long, 32},
    }};
    for (const auto& [mode, seasonLimit] : modes) {
        const tribe::GameEngine game{{mode, 47U}};
        REQUIRE(game.state().season == 1);
        REQUIRE(game.state().seasonLimit == seasonLimit);
    }
}

TEST_CASE("map movement, specialised gathering, and safe settlement are atomic") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 19U}};
    const int initialWood = game.state().wood;
    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "mission wood");
    requireRejectedWithoutChange(game, "move quarry");
    requireSuccess(game, "move forest");
    const int location = game.state().activeMission->worldLocation;
    requireRejectedWithoutChange(game, "gather food");
    REQUIRE(game.state().activeMission->worldLocation == location);
    requireSuccess(game, "gather wood");
    REQUIRE(game.state().activeMission->cargoWood > 0);
    requireRejectedWithoutChange(game, "settle");
    requireSuccess(game, "move camp");
    requireSuccess(game, "settle");
    REQUIRE(!game.state().activeMission.has_value());
    REQUIRE(game.state().wood > initialWood);
}

TEST_CASE("outpost construction has a reachable material route and guard assignment") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 29U}};
    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "assign stone 2");
    const int woodBefore = game.state().wood;
    const int stoneBefore = game.state().stone;
    requireSuccess(game, "mission outpost");
    REQUIRE(game.state().activeMission->cargoWood == 6);
    REQUIRE(game.state().activeMission->cargoStone == 4);
    requireSuccess(game, "move 2");
    requireSuccess(game, "build outpost");
    requireSuccess(game, "settle");
    REQUIRE(game.state().outposts[tribe::indexOf(tribe::WorldLocationId::Forest)]);
    REQUIRE(game.state().wood == woodBefore - 6);
    REQUIRE(game.state().stone == stoneBefore - 4);
    requireSuccess(game, "assign outpost forest 1");
    REQUIRE(game.state().workforce.outpostGuards[tribe::indexOf(tribe::WorldLocationId::Forest)] == 1);
    requireRejectedWithoutChange(game, "assign outpost camp 1");

    tribe::GameState unattended = game.state();
    unattended.squads.front().station = tribe::WorldLocationId::Camp;
    unattended.workforce.outpostGuards[tribe::indexOf(tribe::WorldLocationId::Forest)] = 0;
    tribe::GameEngine decay{std::move(unattended)};
    requireSuccess(decay, "endturn");
    requireSuccess(decay, "event 1");
    requireSuccess(decay, "endturn");
    REQUIRE(!decay.state().outposts[tribe::indexOf(tribe::WorldLocationId::Forest)]);
}

TEST_CASE("workshop transfers equipment through one owner and damaged equipment can be repaired") {
    tribe::GameEngine game = preparedWorkshopGame();
    requireSuccess(game, "craft spear");
    const std::string itemId = game.state().stockpile.front().id;
    requireSuccess(game, "equip 石刃 主手 " + itemId);
    REQUIRE(game.state().stockpile.empty());
    REQUIRE(game.state().roster[1].equipment[tribe::indexOf(tribe::EquipmentSlot::MainHand)].has_value());
    requireSuccess(game, "unequip 石刃 主手");
    tribe::GameState damaged = game.state();
    damaged.stockpile.front().condition = tribe::ItemCondition::Damaged;
    tribe::GameEngine repairGame{std::move(damaged)};
    requireSuccess(repairGame, "repair " + itemId);
    REQUIRE(repairGame.state().stockpile.front().condition == tribe::ItemCondition::Intact);
}

TEST_CASE("army locks actual inventory equipment and victory creates a garrisonable occupation") {
    tribe::GameEngine game = preparedWorkshopGame();
    requireSuccess(game, "craft spear");
    const std::string itemId = game.state().stockpile.front().id;
    requireSuccess(game, "formarmy 石刃 3 0");
    REQUIRE(game.state().stockpile.empty());
    REQUIRE(game.state().war.lockedEquipment.front().id == itemId);

    tribe::GameState state = game.state();
    state.relations[tribe::indexOf(tribe::TribeId::Rockfang)].atWar = true;
    state.discovered[tribe::indexOf(tribe::WorldLocationId::RockfangFort)] = true;
    state.war.playerPower = 100;
    tribe::GameEngine war{std::move(state)};
    requireSuccess(war, "war rock");
    requireSuccess(war, "attack");
    REQUIRE(!war.state().war.active);
    REQUIRE(war.state().occupations[tribe::indexOf(tribe::TribeId::Rockfang)].occupied);
    REQUIRE(!war.state().stockpile.empty());
    requireSuccess(war, "garrison rock 4");
    requireRejectedWithoutChange(war, "garrison rock 6");
}

TEST_CASE("healer treatment consumes herbs only with its maintenance role") {
    tribe::GameEngine base{{tribe::GameMode::Quick, 37U}};
    tribe::GameState state = base.state();
    state.buildings[tribe::indexOf(tribe::BuildingId::HealerHut)] = true;
    state.workforce.healers = 1;
    state.herbs = 2;
    state.actionsLeft = 2;
    state.roster.front().life -= 20;
    state.roster.front().fatigue = 40;
    tribe::GameEngine game{std::move(state)};
    const int lifeBefore = game.state().roster.front().life;
    requireSuccess(game, "treat 晨火队");
    REQUIRE(game.state().herbs == 1);
    REQUIRE(game.state().roster.front().life > lifeBefore);
    REQUIRE(game.state().roster.front().fatigue < 40);
}

TEST_CASE("season events block actions until a choice is made") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 31U}};
    requireSuccess(game, "endturn");
    REQUIRE(game.state().pendingEvent.active);
    requireRejectedWithoutChange(game, "build wall");
    requireSuccess(game, "event 1");
    REQUIRE(!game.state().pendingEvent.active);
}

TEST_CASE("current-format saves preserve an active mission and reject old versions") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 41U}};
    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "mission wood");
    requireSuccess(game, "move forest");
    requireSuccess(game, "gather wood");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "tribe-current-format-test";
    tribe::SaveRepository saves{root};
    std::string error;
    REQUIRE(saves.save(game.state(), tribe::SaveSlot::Slot1, error));
    tribe::GameState loaded;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(loaded.activeMission.has_value());
    REQUIRE(loaded.activeMission->worldLocation == game.state().activeMission->worldLocation);
    REQUIRE(loaded.activeMission->cargoWood == game.state().activeMission->cargoWood);

    const std::filesystem::path path = saves.pathFor(tribe::SaveSlot::Slot1);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(static_cast<bool>(file));
    file.seekp(8);
    const char oldVersion[4]{3, 0, 0, 0};
    file.write(oldVersion, sizeof(oldVersion));
    file.close();
    tribe::GameState rejected;
    REQUIRE(!saves.load(tribe::SaveSlot::Slot1, rejected, error));
    REQUIRE(error.find("旧版本存档不支持") != std::string::npos);
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}
