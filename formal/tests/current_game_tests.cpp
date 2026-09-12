#include "tribe/console_ui.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
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
        std::filesystem::temp_directory_path() / ("tribe-v5-snapshot-" + std::to_string(++snapshotNumber));
    tribe::SaveRepository saves{root};
    std::string error;
    if (!saves.save(state, tribe::SaveSlot::Slot1, error)) {
        throw std::runtime_error("could not snapshot game state: " + error);
    }
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

tribe::GameEngine preparedWorkshopGame(const int craftsmanship = 7) {
    tribe::GameEngine base{{tribe::GameMode::Quick, 23U}};
    tribe::GameState state = base.state();
    state.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
    state.workforce.crafters = 1;
    state.wood = 40;
    state.stone = 40;
    state.hides = 12;
    state.herbs = 12;
    state.food = 100;
    state.actionsLeft = 7;
    for (tribe::Character& person : state.roster) {
        if (person.name == "岩槌") {
            person.attributes[tribe::Attribute::Endurance] = craftsmanship;
            person.attributes[tribe::Attribute::Perception] = craftsmanship;
        }
    }
    return tribe::GameEngine{std::move(state)};
}

tribe::GameEngine eventGame(const unsigned int seed) {
    tribe::GameEngine base{{tribe::GameMode::Quick, seed}};
    tribe::GameState state = base.state();
    state.food = 100;
    state.herbs = 10;
    state.actionsLeft = 7;
    return tribe::GameEngine{std::move(state)};
}

const tribe::ExpansionState& requireMission(const tribe::GameState& state) {
    if (!state.activeMission.has_value()) throw std::runtime_error("expected an active mission");
    return state.activeMission.value();
}

} // namespace

TEST_CASE("the v5 interface exposes five resources, map roles, and road progress") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 13U}};
    REQUIRE(game.statusText().find("贝币") == std::string::npos);
    REQUIRE(game.helpText().find("贝币") == std::string::npos);
    tribe::GameState mapState = game.state();
    mapState.discovered.fill(true);
    tribe::GameEngine mapGame{std::move(mapState)};
    REQUIRE(mapGame.worldText().find("[资源]") != std::string::npos);
    REQUIRE(mapGame.worldText().find("[外交]") != std::string::npos);
    REQUIRE(mapGame.worldText().find("[路线]") != std::string::npos);
    REQUIRE(mapGame.worldText().find("[战争]") != std::string::npos);

    std::ostringstream output;
    tribe::ConsoleUI ui{output, false, false, 100U};
    ui.renderGame(game, {});
    REQUIRE(output.str().find("首季目标") != std::string::npos);

    tribe::GameState completeMission = game.state();
    completeMission.missionCount = 1;
    tribe::GameEngine postMission{std::move(completeMission)};
    std::ostringstream progressOutput;
    tribe::ConsoleUI progressUi{progressOutput, false, false, 100U};
    progressUi.renderGame(postMission, {});
    REQUIRE(progressOutput.str().find("道路进展") != std::string::npos);
    REQUIRE(progressOutput.str().find("联盟：") != std::string::npos);
}

TEST_CASE("five-resource barter remains available and currency commands are absent") {
    tribe::GameEngine base{{tribe::GameMode::Quick, 17U}};
    tribe::GameState state = base.state();
    state.hides = 4;
    state.discovered[tribe::indexOf(tribe::WorldLocationId::RiverFord)] = true;
    tribe::GameEngine game{std::move(state)};

    const int woodBefore = game.state().wood;
    requireSuccess(game, "trade river hides wood");
    REQUIRE(game.state().hides == 0);
    REQUIRE(game.state().wood > woodBefore);
    requireRejectedWithoutChange(game, "trade river shells food");
    requireRejectedWithoutChange(game, "mission shells");

    for (const std::string& command : {"trade river food wood", "trade river wood stone", "trade river stone herbs",
                                       "trade river herbs hides", "trade river hides food"}) {
        tribe::GameEngine tradeBase{{tribe::GameMode::Quick, 18U}};
        tribe::GameState tradeState = tradeBase.state();
        tradeState.herbs = 4;
        tradeState.hides = 4;
        tradeState.discovered[tribe::indexOf(tribe::WorldLocationId::RiverFord)] = true;
        tribe::GameEngine tradeGame{std::move(tradeState)};
        requireSuccess(tradeGame, command);
    }
}

TEST_CASE("gathering and outpost missions keep distinct v5 mission kinds through saves") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "tribe-v5-mission-save";
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    tribe::SaveRepository saves{root};
    std::string error;

    tribe::GameEngine gather{{tribe::GameMode::Quick, 41U}};
    requireSuccess(gather, "assign wood 2");
    requireSuccess(gather, "mission wood");
    requireSuccess(gather, "move forest");
    requireSuccess(gather, "gather wood");
    REQUIRE(requireMission(gather.state()).missionKind == tribe::MissionKind::Gather);
    REQUIRE(saves.save(gather.state(), tribe::SaveSlot::Slot1, error));

    tribe::GameState loadedGather;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loadedGather, error));
    REQUIRE(requireMission(loadedGather).missionKind == tribe::MissionKind::Gather);
    REQUIRE(requireMission(loadedGather).assignedResource == tribe::ResourceKind::Wood);

    tribe::GameEngine outpost{{tribe::GameMode::Quick, 42U}};
    requireSuccess(outpost, "assign wood 2");
    requireSuccess(outpost, "assign stone 2");
    requireSuccess(outpost, "mission outpost");
    REQUIRE(requireMission(outpost.state()).missionKind == tribe::MissionKind::OutpostConstruction);
    REQUIRE(saves.save(outpost.state(), tribe::SaveSlot::Slot2, error));
    tribe::GameState loadedOutpost;
    REQUIRE(saves.load(tribe::SaveSlot::Slot2, loadedOutpost, error));
    REQUIRE(requireMission(loadedOutpost).missionKind == tribe::MissionKind::OutpostConstruction);
    REQUIRE(requireMission(loadedOutpost).cargoWood == 6);
    REQUIRE(requireMission(loadedOutpost).cargoStone == 4);

    std::fstream file(saves.pathFor(tribe::SaveSlot::Slot1), std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(static_cast<bool>(file));
    file.seekp(8);
    const char v4[4]{4, 0, 0, 0};
    file.write(v4, sizeof(v4));
    file.close();
    tribe::GameState rejected;
    REQUIRE(!saves.load(tribe::SaveSlot::Slot1, rejected, error));
    REQUIRE(error.find("需要新开局") != std::string::npos);
    std::filesystem::remove_all(root, cleanup);
}

TEST_CASE("uniform population pool blocks army and mission over-allocation") {
    tribe::GameEngine armyBase{{tribe::GameMode::Quick, 51U}};
    tribe::GameState armyState = armyBase.state();
    armyState.workforce.foodCrew = 6;
    armyState.workforce.woodCrew = 6;
    armyState.workforce.stoneCrew = 2;
    armyState.actionsLeft = 7;
    tribe::GameEngine army{std::move(armyState)};
    requireRejectedWithoutChange(army, "formarmy 石刃 1 0");

    tribe::GameEngine missionBase{{tribe::GameMode::Quick, 52U}};
    tribe::GameState missionState = missionBase.state();
    missionState.population = 6;
    missionState.actionsLeft = 7;
    tribe::GameEngine mission{std::move(missionState)};
    requireRejectedWithoutChange(mission, "mission food");
}

TEST_CASE("formed armies and garrisons consume trained warriors and can be released") {
    tribe::GameEngine game = preparedWorkshopGame();
    requireSuccess(game, "craft spear");
    requireSuccess(game, "formarmy 石刃 3 0");
    REQUIRE(game.state().war.warriors == 3);
    REQUIRE(!game.state().war.lockedEquipment.empty());
    requireSuccess(game, "disbandarmy");
    REQUIRE(game.state().war.commander.empty());
    REQUIRE(!game.state().stockpile.empty());

    requireSuccess(game, "formarmy 石刃 3 0");
    tribe::GameState warState = game.state();
    warState.relations[tribe::indexOf(tribe::TribeId::Rockfang)].atWar = true;
    warState.discovered[tribe::indexOf(tribe::WorldLocationId::RockfangFort)] = true;
    warState.war.playerPower = 100;
    tribe::GameEngine war{std::move(warState)};
    requireSuccess(war, "war rock");
    requireSuccess(war, "attack");
    REQUIRE(war.state().occupations[tribe::indexOf(tribe::TribeId::Rockfang)].occupied);
    REQUIRE(war.state().war.commander.empty());
    requireSuccess(war, "garrison rock 4");
    requireRejectedWithoutChange(war, "garrison rock 6");
}

TEST_CASE("population loss requires manual reassignment before any further action") {
    tribe::GameEngine base{{tribe::GameMode::Quick, 61U}};
    tribe::GameState state = base.state();
    state.food = 0;
    state.workforce.foodCrew = 6;
    state.workforce.woodCrew = 6;
    state.workforce.stoneCrew = 2;
    state.actionsLeft = 7;
    tribe::GameEngine game{std::move(state)};
    requireSuccess(game, "endturn");
    REQUIRE(game.state().workforceReassignmentRequired);
    requireRejectedWithoutChange(game, "build wall");
    requireSuccess(game, "assign food 4");
    REQUIRE(game.state().workforceReassignmentRequired);
    requireSuccess(game, "assign wood 4");
    REQUIRE(!game.state().workforceReassignmentRequired);
}

TEST_CASE("responsible crafter controls quality, effective attributes, and capped war power") {
    tribe::GameEngine game = preparedWorkshopGame(20);
    requireRejectedWithoutChange(game, "appoint 白榆 workshop");
    requireSuccess(game, "appoint 岩槌 workshop");
    requireRejectedWithoutChange(game, "squad configure 岩槌 青枝 石刃");
    requireSuccess(game, "craft spear");
    requireSuccess(game, "craft spear");
    REQUIRE(game.state().stockpile.front().quality == tribe::ItemQuality::Legendary);
    REQUIRE(game.inventoryText().find("品质") != std::string::npos);
    tribe::Character equipped{"测试", tribe::Occupation::Warrior};
    equipped.attributes = tribe::Attributes{5};
    REQUIRE(tribe::equipItem(equipped, tribe::EquipmentSlot::MainHand, game.state().stockpile.front()));
    REQUIRE(tribe::effectiveAttributes(equipped)[tribe::Attribute::Strength] == 10);
    requireSuccess(game, "formarmy 石刃 3 0");
    REQUIRE(game.state().war.craftsmanshipPower == 4);
    REQUIRE(game.powerText().find("品质战力+4") != std::string::npos);
}

TEST_CASE("responsible healer improves treatment and rest, and can be dismissed") {
    tribe::GameEngine base{{tribe::GameMode::Quick, 71U}};
    tribe::GameState state = base.state();
    state.buildings[tribe::indexOf(tribe::BuildingId::HealerHut)] = true;
    state.workforce.healers = 1;
    state.herbs = 4;
    state.actionsLeft = 7;
    state.roster.front().life -= 30;
    state.roster.front().fatigue = 80;
    tribe::GameEngine game{std::move(state)};
    requireRejectedWithoutChange(game, "appoint 岩槌 healer");
    requireSuccess(game, "appoint 白榆 healer");
    const int lifeBefore = game.state().roster.front().life;
    requireSuccess(game, "treat 晨火队");
    REQUIRE(game.state().roster.front().life == lifeBefore + 25);
    REQUIRE(game.state().roster.front().fatigue == 50);
    requireSuccess(game, "squadrest");
    REQUIRE(game.state().roster.front().fatigue == 5);
    requireSuccess(game, "unappoint healer");
    REQUIRE(game.state().healerSupervisor.empty());
}

TEST_CASE("fixed refugees and disease events apply their two choices") {
    tribe::GameEngine refugees = eventGame(3U);
    requireSuccess(refugees, "endturn");
    const int refugeeFood = refugees.state().food;
    const int refugeePopulation = refugees.state().population;
    const int refugeeStability = refugees.state().stability;
    requireSuccess(refugees, "event 1");
    REQUIRE(refugees.state().food == refugeeFood - 4);
    REQUIRE(refugees.state().population == refugeePopulation + 1);
    REQUIRE(refugees.state().stability == refugeeStability + 2);

    tribe::GameEngine refugeeRefusal = eventGame(3U);
    requireSuccess(refugeeRefusal, "endturn");
    const int refusalStability = refugeeRefusal.state().stability;
    requireSuccess(refugeeRefusal, "event 2");
    REQUIRE(refugeeRefusal.state().stability == refusalStability - 3);

    tribe::GameEngine disease = eventGame(0U);
    requireSuccess(disease, "endturn");
    const int diseaseHerbs = disease.state().herbs;
    requireSuccess(disease, "event 1");
    REQUIRE(disease.state().herbs == diseaseHerbs - 2);

    tribe::GameEngine diseaseFailure = eventGame(0U);
    requireSuccess(diseaseFailure, "endturn");
    const int population = diseaseFailure.state().population;
    const int stability = diseaseFailure.state().stability;
    requireSuccess(diseaseFailure, "event 2");
    REQUIRE(diseaseFailure.state().population == population - 1);
    REQUIRE(diseaseFailure.state().stability == stability - 4);
}

TEST_CASE("fixed extortion and faction events include building effects and atomic failures") {
    tribe::GameEngine extortion = eventGame(1U);
    tribe::GameState protectedState = extortion.state();
    protectedState.buildings[tribe::indexOf(tribe::BuildingId::Watchtower)] = true;
    protectedState.buildings[tribe::indexOf(tribe::BuildingId::Wall)] = true;
    protectedState.workforce.scouts = 1;
    protectedState.workforce.campGuards = 1;
    extortion = tribe::GameEngine{std::move(protectedState)};
    requireSuccess(extortion, "endturn");
    const int durability = extortion.state().campDurability;
    requireSuccess(extortion, "event 2");
    REQUIRE(extortion.state().campDurability == durability - 1);

    tribe::GameEngine payment = eventGame(1U);
    requireSuccess(payment, "endturn");
    const int paymentFood = payment.state().food;
    const int paymentStability = payment.state().stability;
    requireSuccess(payment, "event 1");
    REQUIRE(payment.state().food == paymentFood - 4);
    REQUIRE(payment.state().stability == paymentStability + 3);

    tribe::GameEngine noFood = eventGame(1U);
    tribe::GameState noFoodState = noFood.state();
    noFoodState.food = 0;
    noFood = tribe::GameEngine{std::move(noFoodState)};
    requireSuccess(noFood, "endturn");
    requireRejectedWithoutChange(noFood, "event 1");

    tribe::GameEngine factions = eventGame(2U);
    tribe::GameState councilState = factions.state();
    councilState.buildings[tribe::indexOf(tribe::BuildingId::CouncilFire)] = true;
    councilState.workforce.envoys = 1;
    factions = tribe::GameEngine{std::move(councilState)};
    requireSuccess(factions, "endturn");
    const int satisfaction = factions.state().playerFactions.front().satisfaction;
    requireSuccess(factions, "event 1");
    REQUIRE(factions.state().playerFactions.front().satisfaction == satisfaction + 8);

    tribe::GameEngine refusal = eventGame(2U);
    requireSuccess(refusal, "endturn");
    const int refusalStability = refusal.state().stability;
    const int refusalSatisfaction = refusal.state().playerFactions.front().satisfaction;
    requireSuccess(refusal, "event 2");
    REQUIRE(refusal.state().stability == refusalStability - 2);
    REQUIRE(refusal.state().playerFactions.front().satisfaction == refusalSatisfaction - 6);
}
