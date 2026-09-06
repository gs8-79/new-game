#include "tribe/game_engine.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace {

std::size_t tribeIndex(const tribe::TribeId value) {
    return static_cast<std::size_t>(value);
}

std::size_t locationIndex(const tribe::WorldLocationId value) {
    return static_cast<std::size_t>(value);
}

std::size_t technologyIndex(const tribe::TechnologyId value) {
    return static_cast<std::size_t>(value);
}

std::size_t buildingIndex(const tribe::BuildingId value) {
    return static_cast<std::size_t>(value);
}

tribe::ActionResult requireSuccess(tribe::GameEngine& game, const std::string& command) {
    const auto result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

tribe::GameState editableInitial(tribe::GameMode mode = tribe::GameMode::Standard) {
    return tribe::GameEngine{{mode, 17U, "燧火", "炎角", "生存"}}.state();
}

tribe::GameEngine gameFrom(tribe::GameState state) {
    return tribe::GameEngine{std::move(state)};
}

const tribe::Character& rosterCharacter(const tribe::GameState& state, const std::string& name) {
    const auto found = std::find_if(state.roster.begin(), state.roster.end(),
        [&](const tribe::Character& character) { return character.name == name; });
    if (found == state.roster.end()) throw std::runtime_error("missing roster character: " + name);
    return *found;
}

bool inventoryHas(const tribe::Inventory& inventory, const std::string& itemId) {
    return std::any_of(inventory.items().begin(), inventory.items().end(),
        [&](const tribe::Item& item) { return item.id == itemId; });
}

void prepareEndingChoice(tribe::GameState& state) {
    state.phase = tribe::GamePhase::EndingChoice;
    state.season = state.seasonLimit;
    state.actionsLeft = 0;
    state.activeMission.reset();
    state.war.active = false;
    state.ending = tribe::GameEnding::None;
}

} // namespace

TEST_CASE("campaign initializes three modes, sixteen locations and six tribes") {
    tribe::GameEngine quick{{tribe::GameMode::Quick, 3U, "新火", "星鹿", "外交"}};
    tribe::GameEngine course{{tribe::GameMode::Standard, 3U, "燧火", "炎角", "生存"}};
    tribe::GameEngine longGame{{tribe::GameMode::Long, 3U, "燧火", "炎角", "战争"}};

    REQUIRE(quick.state().season == 9);
    REQUIRE(quick.state().seasonLimit == 16);
    REQUIRE(course.state().season == 1);
    REQUIRE(course.state().seasonLimit == 16);
    REQUIRE(longGame.state().seasonLimit == 32);
    REQUIRE(quick.state().tribeName == "新火");
    REQUIRE(quick.state().leaderName == "星鹿");
    REQUIRE(tribe::GameEngine::worldLocations().size() == 16U);
    REQUIRE(course.state().tribes.size() == 6U);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::Camp)]);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::Forest)]);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::RedPlain)]);
    std::string error;
    REQUIRE(tribe::GameEngine::validateState(course.state(), error));
}

TEST_CASE("campaign has no direct gathering or scouting commands outside map missions") {
    const std::array<tribe::GameMode, 3> modes{{
        tribe::GameMode::Quick, tribe::GameMode::Standard, tribe::GameMode::Long}};
    for (const auto mode : modes) {
        tribe::GameEngine game{{mode, 17U, "燧火", "炎角", "生存"}};
        const auto before = game.state();
        for (const std::string command : {"gather food", "采集 木材", "scout marsh", "侦察 燧石矿场", "3", "4"}) {
            const auto result = game.execute(command);
            REQUIRE(!result.recognized);
            REQUIRE(!result.success);
            REQUIRE(!result.stateChanged);
            REQUIRE(game.state().food == before.food);
            REQUIRE(game.state().wood == before.wood);
            REQUIRE(game.state().stone == before.stone);
            REQUIRE(game.state().herbs == before.herbs);
            REQUIRE(game.state().actionsLeft == before.actionsLeft);
            REQUIRE(game.state().discovered == before.discovered);
        }
    }

    tribe::GameEngine game;
    const auto before = game.state();
    requireSuccess(game, "mission");
    const auto remote = game.execute("move harbor");
    REQUIRE(remote.recognized);
    REQUIRE(!remote.success);
    REQUIRE(game.state().actionsLeft == before.actionsLeft - 1);
    REQUIRE(!game.state().discovered[locationIndex(tribe::WorldLocationId::TidesaltHarbor)]);
    requireSuccess(game, "移动 苍林");
    requireSuccess(game, "移动 芦苇沼泽");
    REQUIRE(game.state().activeMission->worldDiscovered[locationIndex(tribe::WorldLocationId::Marsh)]);
    REQUIRE(!game.state().discovered[locationIndex(tribe::WorldLocationId::Marsh)]);
}

TEST_CASE("campaign map missions gather cargo and settle only at camp or outposts") {
    tribe::GameEngine game{{tribe::GameMode::Standard, 73U, "燧火", "炎角", "生存"}};
    const int actionBefore = game.state().actionsLeft;
    const int foodBefore = game.state().food;
    const int woodBefore = game.state().wood;
    requireSuccess(game, "mission");
    REQUIRE(game.state().phase == tribe::GamePhase::Mission);
    REQUIRE(game.state().actionsLeft == actionBefore - 1);
    requireSuccess(game, "move forest");
    requireSuccess(game, "gather food");
    requireSuccess(game, "采集 木材");
    const auto away = game.execute("settle");
    REQUIRE(!away.success);
    REQUIRE(game.state().food == foodBefore);
    REQUIRE(game.state().wood == woodBefore);
    requireSuccess(game, "move camp");
    requireSuccess(game, "settle");
    REQUIRE(game.state().phase == tribe::GamePhase::Managing);
    REQUIRE(!game.state().activeMission);
    REQUIRE(game.state().missionCount == 1);
    REQUIRE(game.state().squads.front().eliteExperience >= 20);
    REQUIRE(game.state().food > foodBefore);
    REQUIRE(game.state().wood > woodBefore);
    REQUIRE(game.state().discovered[locationIndex(tribe::WorldLocationId::Forest)]);
}

TEST_CASE("campaign map diplomacy shares seasonal limits with the tribe screen") {
    auto state = editableInitial();
    state.food = 100;
    state.discovered[locationIndex(tribe::WorldLocationId::RiverFord)] = true;
    tribe::GameEngine game = gameFrom(state);
    const int actionsBefore = game.state().actionsLeft;
    requireSuccess(game, "mission");
    requireSuccess(game, "move plain");
    requireSuccess(game, "move ford");
    const int relationBefore = game.state().relations[tribeIndex(tribe::TribeId::RiverDeer)].relation;
    requireSuccess(game, "talk");
    REQUIRE(game.state().actionsLeft == actionsBefore - 1);
    REQUIRE(game.state().relations[tribeIndex(tribe::TribeId::RiverDeer)].relation > relationBefore);
    REQUIRE(!game.execute("gift").success);
    requireSuccess(game, "move plain");
    requireSuccess(game, "move camp");
    requireSuccess(game, "settle");
    REQUIRE(!game.execute("gift river").success);
    requireSuccess(game, "endturn");
    requireSuccess(game, "gift river");
}

TEST_CASE("campaign rockfang encounter uses equipment but does not occupy the fort") {
    auto state = editableInitial();
    state.discovered[locationIndex(tribe::WorldLocationId::OldPass)] = true;
    tribe::GameEngine game = gameFrom(state);
    requireSuccess(game, "mission");
    for (const std::string command : {"move plain", "move quarry", "move valley", "move workshop", "move road", "move pass", "move fort"}) {
        requireSuccess(game, command);
    }
    requireSuccess(game, "equip mainhand spare_knife");
    requireSuccess(game, "attack");
    REQUIRE(game.state().activeMission->enemyLife < 14);
    while (game.state().activeMission->enemyLife > 0) requireSuccess(game, "attack");
    REQUIRE(game.state().activeMission->battleWon);
    REQUIRE(!game.state().rockfangFortCaptured);
    REQUIRE(!game.state().activeMission->rockfangFortCleared);
    REQUIRE(inventoryHas(game.state().activeMission->inventory, "rockfang_badge"));
    REQUIRE(!game.execute("build outpost").success);
}

TEST_CASE("campaign forest missions preserve long term characters equipment and backpack") {
    tribe::GameEngine game{{tribe::GameMode::Standard, 73U, "燧火", "炎角", "生存"}};
    const int experienceBefore = rosterCharacter(game.state(), "青枝").experience;
    const auto& initialWeapon = rosterCharacter(game.state(), "青枝")
        .equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)];
    REQUIRE(initialWeapon);
    REQUIRE(initialWeapon->id == "leader_bow");
    REQUIRE(inventoryHas(game.state().squads.front().backpack, "spare_knife"));

    requireSuccess(game, "mission forest");
    REQUIRE(game.state().activeMission);
    REQUIRE(game.state().activeMission->squad.members.front().name == game.state().squads.front().captain);
    REQUIRE(game.state().activeMission->squad.members.front().experience == experienceBefore);
    REQUIRE(inventoryHas(game.state().activeMission->inventory, "spare_knife"));
    requireSuccess(game, "equip mainhand spare_knife");
    requireSuccess(game, "move forest");
    requireSuccess(game, "move camp");
    requireSuccess(game, "settle");

    const tribe::Character& returned = rosterCharacter(game.state(), "青枝");
    REQUIRE(returned.experience > experienceBefore);
    REQUIRE(returned.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]);
    REQUIRE(returned.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]->id == "spare_knife");
    REQUIRE(inventoryHas(game.state().squads.front().backpack, "leader_bow"));

    const int experienceAfterFirstMission = returned.experience;
    requireSuccess(game, "mission forest");
    const tribe::ExpansionState& secondMission = *game.state().activeMission;
    const tribe::Character& secondLeader = secondMission.squad.members[secondMission.squad.leaderIndex];
    REQUIRE(secondLeader.name == "青枝");
    REQUIRE(secondLeader.experience == experienceAfterFirstMission);
    REQUIRE(secondLeader.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]);
    REQUIRE(secondLeader.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]->id == "spare_knife");
    REQUIRE(inventoryHas(secondMission.inventory, "leader_bow"));
}

TEST_CASE("campaign map covers all sixteen locations and enforces resource sites and cargo cap") {
    tribe::GameEngine game{{tribe::GameMode::Standard, 89U, "燧火", "炎角", "生存"}};
    requireSuccess(game, "mission");
    const std::array<std::string, 18> route{{
        "move forest", "move marsh", "move whitecamp", "move marsh", "move coast", "move beach",
        "move harbor", "move beach", "move coast", "move marsh", "move forest", "move camp",
        "move plain", "move quarry", "move valley", "move workshop", "move road", "move pass"}};
    for (const auto& command : route) requireSuccess(game, command);
    requireSuccess(game, "move fort");
    requireSuccess(game, "move pass");
    requireSuccess(game, "move road");
    requireSuccess(game, "move market");
    requireSuccess(game, "move ford");
    const auto& discovered = game.state().activeMission->worldDiscovered;
    for (std::size_t index = 0; index < discovered.size(); ++index) {
        if (!discovered[index]) throw std::runtime_error("world location not reached: " + std::to_string(index + 1));
    }

    const std::string before = tribe::ExpansionGame{*game.state().activeMission}.stateFingerprint();
    const auto wrongSite = game.execute("gather wood");
    REQUIRE(!wrongSite.success);
    REQUIRE(tribe::ExpansionGame{*game.state().activeMission}.stateFingerprint() == before);

    for (int count = 0; count < 4; ++count) requireSuccess(game, "gather food");
    REQUIRE(game.state().activeMission->cargoFood == game.state().activeMission->cargoCapacity);
    const auto full = game.execute("gather food");
    REQUIRE(!full.success);
    REQUIRE(game.state().activeMission->harvestActions == 4);
}

TEST_CASE("campaign outposts cost carried materials persist as stations and reject invalid ownership") {
    tribe::GameEngine game{{tribe::GameMode::Standard, 97U, "燧火", "炎角", "生存"}};
    requireSuccess(game, "mission");
    requireSuccess(game, "move forest");
    requireSuccess(game, "gather wood");
    requireSuccess(game, "move camp");
    requireSuccess(game, "move plain");
    requireSuccess(game, "move quarry");
    requireSuccess(game, "gather stone");
    requireSuccess(game, "move plain");
    requireSuccess(game, "build outpost");
    REQUIRE(game.state().activeMission->cargoWood == 0);
    REQUIRE(game.state().activeMission->cargoStone == 1);
    REQUIRE(!game.execute("build outpost").success);
    requireSuccess(game, "settle");
    REQUIRE(game.state().outposts[locationIndex(tribe::WorldLocationId::RedPlain)]);
    REQUIRE(game.state().squads.front().station == tribe::WorldLocationId::RedPlain);

    requireSuccess(game, "mission");
    REQUIRE(game.state().activeMission->worldLocation == static_cast<int>(tribe::WorldLocationId::RedPlain));
    requireSuccess(game, "move quarry");
    requireSuccess(game, "move valley");
    requireSuccess(game, "move workshop");
    requireSuccess(game, "move road");
    requireSuccess(game, "move pass");
    requireSuccess(game, "move fort");
    auto hostile = game.state();
    hostile.activeMission->cargoWood = 6;
    hostile.activeMission->cargoStone = 4;
    std::string error;
    REQUIRE(tribe::GameEngine::validateState(hostile, error));
    tribe::GameEngine blocked = gameFrom(std::move(hostile));
    const auto rejected = blocked.execute("build outpost");
    REQUIRE(!rejected.success);
    REQUIRE(!rejected.stateChanged);

    auto duplicate = editableInitial();
    duplicate.squads.push_back(duplicate.squads.front());
    REQUIRE(!tribe::GameEngine::validateState(duplicate, error));
}

TEST_CASE("campaign diplomacy supports marriage tribute alliance war and truce") {
    auto diplomatic = editableInitial();
    auto& river = diplomatic.relations[tribeIndex(tribe::TribeId::RiverDeer)];
    river.relation = 80;
    river.trust = 70;
    diplomatic.discovered[locationIndex(tribe::WorldLocationId::RiverFord)] = true;
    diplomatic.technologies[technologyIndex(tribe::TechnologyId::Confederation)] = true;
    diplomatic.food = 100;
    tribe::GameEngine diplomacy = gameFrom(diplomatic);
    requireSuccess(diplomacy, "marry river");
    requireSuccess(diplomacy, "endturn");
    requireSuccess(diplomacy, "ally river");
    REQUIRE(diplomacy.state().relations[tribeIndex(tribe::TribeId::RiverDeer)].marriage);
    REQUIRE(diplomacy.state().relations[tribeIndex(tribe::TribeId::RiverDeer)].alliance);

    auto coercive = editableInitial();
    coercive.warriors = 8;
    coercive.discovered[locationIndex(tribe::WorldLocationId::BlackstoneWorkshop)] = true;
    coercive.discovered[locationIndex(tribe::WorldLocationId::OldPass)] = true;
    auto& blackstone = coercive.relations[tribeIndex(tribe::TribeId::Blackstone)];
    blackstone.fear = 70;
    coercive.food = 100;
    tribe::GameEngine pressure = gameFrom(coercive);
    requireSuccess(pressure, "demand blackstone");
    requireSuccess(pressure, "declare rockfang");
    requireSuccess(pressure, "endturn");
    requireSuccess(pressure, "truce rockfang");
    REQUIRE(pressure.state().relations[tribeIndex(tribe::TribeId::Blackstone)].otherPaysTribute);
    REQUIRE(!pressure.state().relations[tribeIndex(tribe::TribeId::Rockfang)].atWar);
    REQUIRE(pressure.state().relations[tribeIndex(tribe::TribeId::Rockfang)].truce);
}

TEST_CASE("campaign trade uses supply prices and unlocks shell currency") {
    auto state = editableInitial();
    state.food = 100;
    state.wood = 2;
    state.tradeCount = 7;
    state.technologies[technologyIndex(tribe::TechnologyId::SharedLanguage)] = true;
    state.tradePartners[tribeIndex(tribe::TribeId::RiverDeer)] = true;
    state.tradePartners[tribeIndex(tribe::TribeId::WhiteFeather)] = true;
    state.tradePartners[tribeIndex(tribe::TribeId::Blackstone)] = true;
    state.discovered[locationIndex(tribe::WorldLocationId::RiverFord)] = true;
    tribe::GameEngine game = gameFrom(state);
    const int woodBefore = game.state().wood;
    const auto result = requireSuccess(game, "trade river food wood");
    REQUIRE(result.message.find("价格受稀缺") != std::string::npos);
    REQUIRE(game.state().wood > woodBefore);
    REQUIRE(game.state().tradeCount == 8);
    REQUIRE(game.state().currencyUnlocked);
    REQUIRE(game.state().shells == 20);
}

TEST_CASE("campaign obsolete resident gathering produces no passive resources") {
    auto state = editableInitial();
    state.food = 100;
    state.squads.front().residentMission = tribe::ResidentMission::Gather;
    const int eliteBefore = state.squads.front().eliteExperience;
    const int foodBefore = state.food;
    tribe::GameEngine game = gameFrom(state);
    requireSuccess(game, "endturn");
    REQUIRE(game.state().season == 2);
    REQUIRE(game.state().squads.front().eliteExperience == eliteBefore);
    REQUIRE(game.state().squads.front().fatigue == 0);
    REQUIRE(game.state().squads.front().residentMission == tribe::ResidentMission::None);
    REQUIRE(game.state().food <= foodBefore);

    auto capped = editableInitial();
    capped.population = 5;
    capped.warriors = 5;
    capped.food = 100;
    capped.squads.front().residentMission = tribe::ResidentMission::Train;
    tribe::GameEngine cappedTraining = gameFrom(capped);
    requireSuccess(cappedTraining, "endturn");
    REQUIRE(cappedTraining.state().warriors == cappedTraining.state().population);

    auto invalid = cappedTraining.state();
    invalid.warriors = invalid.population + 1;
    std::string error;
    REQUIRE(!cappedTraining.replaceState(invalid, error));
    REQUIRE(!error.empty());
}

TEST_CASE("campaign faction crisis can reach coup and appoint a new leader") {
    auto state = editableInitial();
    state.food = 100;
    state.stability = 10;
    state.playerFactions[0].satisfaction = 0;
    state.playerFactions[0].crisis = tribe::FactionCrisis::Deposition;
    const std::string successor = state.playerFactions[0].candidate;
    tribe::GameEngine game = gameFrom(state);
    const auto result = requireSuccess(game, "结束回合");
    REQUIRE(result.message.find("政变") != std::string::npos);
    REQUIRE(game.state().leaderName == successor);
    REQUIRE(game.state().actingLeaderName == successor);
    REQUIRE(game.state().leadershipHistory.size() == 2U);
}

TEST_CASE("campaign faction refusal is recomputed across all factions") {
    auto state = editableInitial();
    state.food = 100;
    state.playerFactions[0].crisis = tribe::FactionCrisis::Refusal;
    state.playerFactions[1].crisis = tribe::FactionCrisis::Refusal;
    state.squads.front().refusingOrders = true;
    tribe::GameEngine game = gameFrom(state);
    requireSuccess(game, "appease 1");
    REQUIRE(game.state().squads.front().refusingOrders);
    requireSuccess(game, "appease 2");
    REQUIRE(!game.state().squads.front().refusingOrders);
}

TEST_CASE("campaign diplomacy rejects conflicts and war clears incompatible relations") {
    auto state = editableInitial();
    const auto index = tribeIndex(tribe::TribeId::RiverDeer);
    state.food = 100;
    state.warriors = 8;
    state.discovered[locationIndex(tribe::WorldLocationId::RiverFord)] = true;
    state.relations[index].relation = 80;
    state.relations[index].trust = 70;
    state.relations[index].fear = 70;
    state.relations[index].alliance = true;
    state.relations[index].marriage = true;
    state.relations[index].tradeRoute = true;
    tribe::GameEngine game = gameFrom(state);
    REQUIRE(!game.execute("tribute river").success);
    REQUIRE(!game.execute("demand river").success);
    requireSuccess(game, "declare river");
    const auto& relation = game.state().relations[index];
    REQUIRE(relation.atWar);
    REQUIRE(!relation.alliance);
    REQUIRE(!relation.marriage);
    REQUIRE(!relation.tradeRoute);
    REQUIRE(!relation.playerPaysTribute);
    REQUIRE(!relation.otherPaysTribute);
}

TEST_CASE("campaign war defense cannot conquer and militia deaths reduce population") {
    auto defending = editableInitial();
    defending.phase = tribe::GamePhase::War;
    defending.relations[tribeIndex(tribe::TribeId::Rockfang)].atWar = true;
    defending.war = {true, tribe::TribeId::Rockfang, "石刃", 3, 0, 12, 2, 1,
        tribe::WarOrder::Hold, true};
    tribe::GameEngine defense = gameFrom(defending);
    requireSuccess(defense, "defend");
    REQUIRE(defense.state().phase == tribe::GamePhase::War);
    REQUIRE(defense.state().war.front == 1);
    REQUIRE(defense.state().war.enemyPower == 1);
    REQUIRE(defense.state().warsWon == 0);
    requireSuccess(defense, "attack");
    REQUIRE(defense.state().war.front == 2);
    REQUIRE(defense.state().war.enemyPower > 0);

    auto finalFront = defense.state();
    finalFront.war.front = 3;
    finalFront.war.enemyPower = 1;
    tribe::GameEngine victory = gameFrom(finalFront);
    const auto noOpDefense = victory.execute("防御");
    REQUIRE(!noOpDefense.success);
    REQUIRE(!noOpDefense.stateChanged);
    REQUIRE(victory.state().phase == tribe::GamePhase::War);
    REQUIRE(victory.state().warsWon == 0);
    requireSuccess(victory, "攻击");
    REQUIRE(victory.state().phase == tribe::GamePhase::Managing);
    REQUIRE(victory.state().warsWon == 1);
    REQUIRE(victory.state().rockfangFortCaptured);

    auto dangerous = editableInitial();
    dangerous.phase = tribe::GamePhase::War;
    dangerous.relations[tribeIndex(tribe::TribeId::Rockfang)].atWar = true;
    dangerous.war = {true, tribe::TribeId::Rockfang, "石刃", 1, 2, 4, 30, 1,
        tribe::WarOrder::Advance, true};
    const int populationBefore = dangerous.population;
    tribe::GameEngine battle = gameFrom(dangerous);
    requireSuccess(battle, "attack");
    REQUIRE(battle.state().population < populationBefore);
    REQUIRE(battle.state().warsLost == 1);

    auto formalLoss = editableInitial();
    formalLoss.phase = tribe::GamePhase::War;
    formalLoss.relations[tribeIndex(tribe::TribeId::Rockfang)].atWar = true;
    formalLoss.war = {true, tribe::TribeId::Rockfang, "石刃", 2, 0, 4, 30, 1,
        tribe::WarOrder::Advance, true};
    const int formalPopulation = formalLoss.population;
    const int formalWarriors = formalLoss.warriors;
    tribe::GameEngine formalBattle = gameFrom(formalLoss);
    const auto loss = requireSuccess(formalBattle, "attack");
    REQUIRE(formalBattle.state().population == formalPopulation - 2);
    REQUIRE(formalBattle.state().warriors == formalWarriors - 2);
    REQUIRE(loss.message.find("2人伤亡") != std::string::npos);
}

TEST_CASE("campaign modes reach their season limits deterministically") {
    const std::array<std::pair<tribe::GameMode, int>, 3> modes{{
        {tribe::GameMode::Quick, 8},
        {tribe::GameMode::Standard, 16},
        {tribe::GameMode::Long, 32},
    }};
    for (const auto& [mode, turns] : modes) {
        auto state = editableInitial(mode);
        state.food = 5000;
        state.campDurability = 100;
        state.buildings[buildingIndex(tribe::BuildingId::Wall)] = true;
        tribe::GameEngine game = gameFrom(state);
        for (int turn = 0; turn < turns; ++turn) requireSuccess(game, "endturn");
        REQUIRE(game.state().phase == tribe::GamePhase::EndingChoice);
        REQUIRE(game.state().season == game.state().seasonLimit);
    }

    tribe::GameEngine first{{tribe::GameMode::Standard, 211U, "燧火", "炎角", "生存"}};
    tribe::GameEngine repeat{{tribe::GameMode::Standard, 211U, "燧火", "炎角", "生存"}};
    for (int index = 0; index < 4; ++index) {
        const auto left = requireSuccess(first, "endturn");
        const auto right = requireSuccess(repeat, "结束回合");
        REQUIRE(left.message == right.message);
        REQUIRE(first.state().food == repeat.state().food);
        REQUIRE(first.state().population == repeat.state().population);
        REQUIRE(first.state().stability == repeat.state().stability);
    }
}

TEST_CASE("campaign exposes five endings and long mode can continue sandbox") {
    auto allianceState = editableInitial();
    prepareEndingChoice(allianceState);
    allianceState.technologies[technologyIndex(tribe::TechnologyId::Confederation)] = true;
    for (const auto id : {tribe::TribeId::RiverDeer, tribe::TribeId::WhiteFeather}) {
        allianceState.relations[tribeIndex(id)].relation = 80;
        allianceState.relations[tribeIndex(id)].trust = 70;
        allianceState.relations[tribeIndex(id)].alliance = true;
    }
    tribe::GameEngine alliance = gameFrom(allianceState);
    requireSuccess(alliance, "choose alliance");
    REQUIRE(alliance.state().ending == tribe::GameEnding::Alliance);
    REQUIRE(!alliance.endingSummary().epilogue.empty());

    auto conquestState = editableInitial();
    prepareEndingChoice(conquestState);
    conquestState.rockfangFortCaptured = true;
    conquestState.rockfangStrength = 0;
    conquestState.warriors = 6;
    conquestState.morale = 70;
    tribe::GameEngine conquest = gameFrom(conquestState);
    requireSuccess(conquest, "choose conquest");
    REQUIRE(conquest.state().ending == tribe::GameEnding::Conquest);

    auto prosperityState = editableInitial();
    prepareEndingChoice(prosperityState);
    prosperityState.population = 24;
    prosperityState.food = 80;
    for (std::size_t index = 0; index < 4U; ++index) {
        prosperityState.buildings[index] = true;
        prosperityState.technologies[index] = true;
    }
    tribe::GameEngine prosperity = gameFrom(prosperityState);
    requireSuccess(prosperity, "choose prosperity");
    REQUIRE(prosperity.state().ending == tribe::GameEnding::Prosperity);

    auto migrationState = editableInitial(tribe::GameMode::Long);
    prepareEndingChoice(migrationState);
    tribe::GameEngine migration = gameFrom(migrationState);
    requireSuccess(migration, "choose migration");
    REQUIRE(migration.state().ending == tribe::GameEnding::Migration);
    requireSuccess(migration, "sandbox");
    REQUIRE(migration.state().phase == tribe::GamePhase::Sandbox);

    auto extinctionState = editableInitial();
    extinctionState.population = 0;
    extinctionState.warriors = 0;
    extinctionState.phase = tribe::GamePhase::Finished;
    extinctionState.ending = tribe::GameEnding::Extinction;
    tribe::GameEngine extinction = gameFrom(extinctionState);
    REQUIRE(extinction.endingSummary().ending == tribe::GameEnding::Extinction);
    REQUIRE(extinction.endingSummary().title == "部落覆灭");
}
