#include "tribe/console_ui.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
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

tribe::ExpansionCommandResult requireSuccess(tribe::ExpansionGame& game, const std::string& command) {
    const tribe::ExpansionCommandResult result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

// 通过正式序列化格式取得二进制快照，用于证明失败命令和拒绝校验均未改变任何持久化字节。
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

TEST_CASE("command catalog keeps whitespace Chinese English and numeric query contracts identical") {
    tribe::GameEngine english{{tribe::GameMode::Quick, 141U}};
    tribe::GameEngine chinese{{tribe::GameMode::Quick, 141U}};
    tribe::GameEngine numeric{{tribe::GameMode::Quick, 141U}};

    const tribe::ActionResult englishStatus = english.execute("  status  ");
    const tribe::ActionResult chineseStatus = chinese.execute("状态");
    const tribe::ActionResult numericStatus = numeric.execute("1");
    REQUIRE(englishStatus.success);
    REQUIRE(chineseStatus.success);
    REQUIRE(numericStatus.success);
    REQUIRE(englishStatus.message == chineseStatus.message);
    REQUIRE(englishStatus.message == numericStatus.message);
    REQUIRE(stateSnapshot(english.state()) == stateSnapshot(chinese.state()));
    REQUIRE(stateSnapshot(english.state()) == stateSnapshot(numeric.state()));

    REQUIRE(english.execute("  assign   wood  2 ").success);
    REQUIRE(chinese.execute("分配 木材 2").success);
    REQUIRE(stateSnapshot(english.state()) == stateSnapshot(chinese.state()));
    REQUIRE(english.execute("mission wood").success);
    REQUIRE(chinese.execute("出任务 木材").success);
    REQUIRE(stateSnapshot(english.state()) == stateSnapshot(chinese.state()));

    requireRejectedWithoutChange(english, "build unknown-building");
    const std::string beforeWhitespace = stateSnapshot(english.state());
    const tribe::ActionResult blank = english.execute(" \t \r\n");
    REQUIRE(!blank.recognized);
    REQUIRE(stateSnapshot(english.state()) == beforeWhitespace);
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

TEST_CASE("crafted equipment keeps a global serial after being equipped and recreated in the same season") {
    tribe::GameEngine game = preparedWorkshopGame();
    requireSuccess(game, "craft spear");
    REQUIRE(game.state().stockpile.size() == 1U);
    const std::string firstId = game.state().stockpile.front().id;
    requireSuccess(game, "equip 石刃 主手 " + firstId);
    REQUIRE(game.state().stockpile.empty());

    requireSuccess(game, "craft spear");
    REQUIRE(game.state().stockpile.size() == 1U);
    const std::string secondId = game.state().stockpile.front().id;
    REQUIRE(firstId != secondId);
    REQUIRE(game.state().nextItemSerial == 3U);
    std::string error;
    REQUIRE(tribe::GameEngine::validateState(game.state(), error));
}

TEST_CASE("using expedition herbs advances exactly one turn while applying recovery and fatigue consistently") {
    tribe::ExpansionGame seeded{301U, 4U};
    tribe::ExpansionState state = seeded.state();
    state.cargoHerbs = 1;
    state.squad.members[state.squad.leaderIndex].life = 40;
    state.squad.members[state.squad.leaderIndex].fatigue = 50;
    tribe::ExpansionGame mission{std::move(state)};
    const int beforeTurn = mission.state().turn;
    const int beforeLife = mission.state().squad.members[mission.state().squad.leaderIndex].life;
    const int beforeFatigue = mission.state().squad.members[mission.state().squad.leaderIndex].fatigue;

    const tribe::ExpansionCommandResult result = requireSuccess(mission, "use herb");
    REQUIRE(result.turnAdvanced);
    REQUIRE(mission.state().turn == beforeTurn + 1);
    REQUIRE(mission.state().cargoHerbs == 0);
    REQUIRE(mission.state().squad.members[mission.state().squad.leaderIndex].life == beforeLife + 18);
    REQUIRE(mission.state().squad.members[mission.state().squad.leaderIndex].fatigue == beforeFatigue - 20);
}

TEST_CASE("state validation rejects unsafe text invalid items and incoherent mission states atomically") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 302U}};
    const std::string before = stateSnapshot(game.state());
    std::string error;

    tribe::GameState unsafeText = game.state();
    unsafeText.tribeName = "\x1b[31m危险";
    REQUIRE(!game.replaceState(unsafeText, error));
    REQUIRE(error.find("控制字符") != std::string::npos);
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState unsafeUtf8 = game.state();
    unsafeUtf8.leaderName = std::string{"\xC3\x28", 2U};
    REQUIRE(!game.replaceState(unsafeUtf8, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    unsafeUtf8.leaderName = std::string{"\xC2\x80", 2U};
    REQUIRE(!game.replaceState(unsafeUtf8, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState invalidItem = game.state();
    tribe::Item malformed;
    malformed.id = "malformed";
    malformed.name = "损坏物品";
    malformed.weight = 1;
    malformed.equipmentSlot = tribe::EquipmentSlot::MainHand;
    malformed.quality = static_cast<tribe::ItemQuality>(99);
    invalidItem.stockpile.push_back(malformed);
    REQUIRE(!game.replaceState(invalidItem, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState wrongEquipmentSlot = game.state();
    auto& equipped = wrongEquipmentSlot.roster.front().equipment[tribe::indexOf(tribe::EquipmentSlot::MainHand)];
    REQUIRE(equipped.has_value());
    if (equipped) equipped->equipmentSlot = tribe::EquipmentSlot::Body;
    REQUIRE(!game.replaceState(wrongEquipmentSlot, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState invalidCharacter = game.state();
    invalidCharacter.roster.front().occupation = static_cast<tribe::Occupation>(99);
    REQUIRE(!game.replaceState(invalidCharacter, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState invalidFaction = game.state();
    invalidFaction.tribes[tribe::indexOf(tribe::TribeId::RiverDeer)].factions.front().crisis =
        static_cast<tribe::FactionCrisis>(99);
    REQUIRE(!game.replaceState(invalidFaction, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState invalidWar = game.state();
    invalidWar.war.order = static_cast<tribe::WarOrder>(99);
    REQUIRE(!game.replaceState(invalidWar, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState overGarrisoned = game.state();
    overGarrisoned.occupations[tribe::indexOf(tribe::TribeId::Rockfang)] = {true, overGarrisoned.warriors + 1, 0};
    REQUIRE(!game.replaceState(overGarrisoned, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    tribe::GameState unknownCommander = game.state();
    unknownCommander.war.commander = "不存在的统帅";
    unknownCommander.war.warriors = 1;
    REQUIRE(!game.replaceState(unknownCommander, error));
    REQUIRE(stateSnapshot(game.state()) == before);

    requireSuccess(game, "assign wood 2");
    requireSuccess(game, "mission wood");
    tribe::GameState invalidMission = game.state();
    REQUIRE(invalidMission.activeMission.has_value());
    if (invalidMission.activeMission) {
        invalidMission.activeMission->encounterLife = 3;
        invalidMission.activeMission->worldLocation = 0;
    }
    REQUIRE(!game.replaceState(invalidMission, error));
    const auto& activeMission = game.state().activeMission;
    REQUIRE(activeMission.has_value());
    if (activeMission) REQUIRE(activeMission->encounterLife == 0);

    invalidMission = game.state();
    REQUIRE(invalidMission.activeMission.has_value());
    if (invalidMission.activeMission) invalidMission.activeMission->squad.members.front().name = "不在名单的人";
    REQUIRE(!game.replaceState(invalidMission, error));
    const auto& unchangedMission = game.state().activeMission;
    REQUIRE(unchangedMission.has_value());
    if (unchangedMission) REQUIRE(unchangedMission->squad.members.front().name != "不在名单的人");

    invalidMission = game.state();
    REQUIRE(invalidMission.activeMission.has_value());
    if (invalidMission.activeMission) {
        auto& missionEquipment = invalidMission.activeMission->squad.members.front()
                                     .equipment[tribe::indexOf(tribe::EquipmentSlot::MainHand)];
        REQUIRE(missionEquipment.has_value());
        if (missionEquipment) missionEquipment->id += "_tampered";
    }
    REQUIRE(!game.replaceState(invalidMission, error));
    const auto& unchangedEquipmentMission = game.state().activeMission;
    REQUIRE(unchangedEquipmentMission.has_value());
    if (unchangedEquipmentMission) {
        const auto& unchangedEquipment =
            unchangedEquipmentMission->squad.members.front().equipment[tribe::indexOf(tribe::EquipmentSlot::MainHand)];
        REQUIRE(unchangedEquipment.has_value());
        if (unchangedEquipment) REQUIRE(unchangedEquipment->id == "leader_bow");
    }
}

TEST_CASE("fixed-seed command sequences preserve valid state and make rejected commands byte-identical no-ops") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 303U}};
    const std::array<std::string, 15> commands{
        {"   ", "unknown", "assign wood 2", "assign wood 1", "mission wood", "move forest", "gather wood", "move camp",
         "settle", "endturn", "status", "craft spear", "equip 石刃 主手 missing", "war rock", "event 1"}};
    // 固定种子让随机命令覆盖可复现，任一次拒绝都与循环前的二进制快照比较。
    std::mt19937 generator{0x5EEDU};
    for (int step = 0; step < 160; ++step) {
        const std::string before = stateSnapshot(game.state());
        const tribe::ActionResult result = game.execute(commands[generator() % commands.size()]);
        std::string error;
        REQUIRE(tribe::GameEngine::validateState(game.state(), error));
        if (!result.success || !result.stateChanged) REQUIRE(stateSnapshot(game.state()) == before);
    }
}

TEST_CASE("last season is announced and an early ending choice is answered instead of being unrecognized") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 7U}};
    // 结算阶段之前输入 choose 必须被识别并说明下一步，而不是落回“无法识别该命令”。
    const tribe::ActionResult early = game.execute("choose migration");
    REQUIRE(early.recognized);
    REQUIRE(!early.success);
    REQUIRE(!early.stateChanged);
    REQUIRE(early.message.find("结束回合") != std::string::npos);

    // 非最后一季的状态文本不应出现引导行。
    REQUIRE(game.statusText().find("这是最后一季") == std::string::npos);

    // 快速模式共 8 季；推进到最后一季后状态文本必须给出结束回合引导。
    tribe::GameState finalSeason = game.state();
    finalSeason.season = finalSeason.seasonLimit;
    tribe::GameEngine lastSeason{finalSeason};
    REQUIRE(lastSeason.statusText().find("这是最后一季") != std::string::npos);

    // 引导只出现在经营阶段，进入结局议事后由阶段自身给出提示。
    tribe::GameState choosing = game.state();
    choosing.phase = tribe::GamePhase::EndingChoice;
    choosing.season = choosing.seasonLimit;
    tribe::GameEngine endingChoice{choosing};
    REQUIRE(endingChoice.statusText().find("这是最后一季") == std::string::npos);
}
