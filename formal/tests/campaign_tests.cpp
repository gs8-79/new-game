#include "tribe/campaign.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace {

std::size_t tribeIndex(const tribe::TribeIdV2 value) {
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

tribe::CampaignActionResult requireSuccess(tribe::CampaignGame& game, const std::string& command) {
    const auto result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

tribe::CampaignState editableInitial(tribe::CampaignMode mode = tribe::CampaignMode::Course) {
    return tribe::CampaignGame{{mode, 17U, "燧火", "炎角", "生存"}}.state();
}

tribe::CampaignGame gameFrom(tribe::CampaignState state) {
    return tribe::CampaignGame{std::move(state)};
}

const tribe::Character& rosterCharacter(const tribe::CampaignState& state, const std::string& name) {
    const auto found = std::find_if(state.roster.begin(), state.roster.end(),
        [&](const tribe::Character& character) { return character.name == name; });
    if (found == state.roster.end()) throw std::runtime_error("missing roster character: " + name);
    return *found;
}

bool inventoryHas(const tribe::Inventory& inventory, const std::string& itemId) {
    return std::any_of(inventory.items().begin(), inventory.items().end(),
        [&](const tribe::Item& item) { return item.id == itemId; });
}

void prepareEndingChoice(tribe::CampaignState& state) {
    state.phase = tribe::CampaignPhase::EndingChoice;
    state.season = state.seasonLimit;
    state.actionsLeft = 0;
    state.activeMission.reset();
    state.war.active = false;
    state.ending = tribe::CampaignEnding::None;
}

} // namespace

TEST_CASE("campaign initializes three modes, sixteen locations and six tribes") {
    tribe::CampaignGame quick{{tribe::CampaignMode::Quick, 3U, "新火", "星鹿", "外交"}};
    tribe::CampaignGame course{{tribe::CampaignMode::Course, 3U, "燧火", "炎角", "生存"}};
    tribe::CampaignGame longGame{{tribe::CampaignMode::Long, 3U, "燧火", "炎角", "战争"}};

    REQUIRE(quick.state().season == 9);
    REQUIRE(quick.state().seasonLimit == 16);
    REQUIRE(course.state().season == 1);
    REQUIRE(course.state().seasonLimit == 16);
    REQUIRE(longGame.state().seasonLimit == 32);
    REQUIRE(quick.state().tribeName == "新火");
    REQUIRE(quick.state().leaderName == "星鹿");
    REQUIRE(tribe::CampaignGame::worldLocations().size() == 16U);
    REQUIRE(course.state().tribes.size() == 6U);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::Camp)]);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::Forest)]);
    REQUIRE(course.state().discovered[locationIndex(tribe::WorldLocationId::RedPlain)]);
    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(course.state(), error));
}

TEST_CASE("campaign scouting follows adjacency and failed actions are atomic") {
    tribe::CampaignGame game;
    const auto before = game.state();
    const auto remote = game.execute("scout harbor");
    REQUIRE(remote.recognized);
    REQUIRE(!remote.success);
    REQUIRE(game.state().actionsLeft == before.actionsLeft);
    REQUIRE(!game.state().discovered[locationIndex(tribe::WorldLocationId::TidesaltHarbor)]);

    const auto marsh = requireSuccess(game, "侦察 芦苇沼泽");
    REQUIRE(marsh.consumesAction);
    REQUIRE(game.state().actionsLeft == before.actionsLeft - 1);
    REQUIRE(game.state().discovered[locationIndex(tribe::WorldLocationId::Marsh)]);

    const int food = game.state().food;
    const int wood = game.state().wood;
    const int actions = game.state().actionsLeft;
    const auto impossible = game.execute("build workshop");
    REQUIRE(!impossible.success);
    REQUIRE(game.state().food == food);
    REQUIRE(game.state().wood == wood);
    REQUIRE(game.state().actionsLeft == actions);

    const auto undiscoveredTrade = game.execute("trade tide food wood");
    REQUIRE(undiscoveredTrade.recognized);
    REQUIRE(!undiscoveredTrade.success);
    REQUIRE(game.state().actionsLeft == actions);

    auto blockedWar = editableInitial();
    blockedWar.relations[tribeIndex(tribe::TribeIdV2::Rockfang)].atWar = true;
    blockedWar.war = {false, tribe::TribeIdV2::Rockfang, "石刃", 1, 0, 8, 0, 0,
        tribe::WarOrder::Hold, false};
    tribe::CampaignGame army = gameFrom(blockedWar);
    const auto remoteWar = army.execute("war rockfang");
    REQUIRE(remoteWar.recognized);
    REQUIRE(!remoteWar.success);
    REQUIRE(army.state().phase == tribe::CampaignPhase::Managing);
}

TEST_CASE("campaign embeds the controllable forest mission into seasonal resources") {
    tribe::CampaignGame game{{tribe::CampaignMode::Course, 73U, "燧火", "炎角", "生存"}};
    const int actionBefore = game.state().actionsLeft;
    requireSuccess(game, "mission forest");
    REQUIRE(game.state().phase == tribe::CampaignPhase::Mission);
    REQUIRE(game.state().actionsLeft == actionBefore - 1);
    requireSuccess(game, "move forest");
    requireSuccess(game, "move deep");
    requireSuccess(game, "move clearing");
    requireSuccess(game, "talk");
    requireSuccess(game, "trade");
    requireSuccess(game, "return");
    REQUIRE(game.state().phase == tribe::CampaignPhase::Managing);
    REQUIRE(!game.state().activeMission);
    REQUIRE(game.state().missionCount == 1);
    REQUIRE(game.state().squads.front().eliteExperience >= 20);
    REQUIRE(game.state().relations[tribeIndex(tribe::TribeIdV2::WhiteFeather)].relation > 5);

    tribe::CampaignGame gathering{{tribe::CampaignMode::Course, 73U, "燧火", "炎角", "生存"}};
    const int foodBefore = gathering.state().food;
    requireSuccess(gathering, "mission forest");
    requireSuccess(gathering, "move forest");
    requireSuccess(gathering, "move deep");
    requireSuccess(gathering, "move hunting");
    int gathers = 0;
    while (gathering.execute("gather food").success) ++gathers;
    REQUIRE(gathers == 3);
    const std::string boundedMission = tribe::ExpansionGame{*gathering.state().activeMission}.stateFingerprint();
    const auto exhausted = gathering.execute("采集 食物");
    REQUIRE(exhausted.recognized);
    REQUIRE(!exhausted.success);
    REQUIRE(tribe::ExpansionGame{*gathering.state().activeMission}.stateFingerprint() == boundedMission);
    requireSuccess(gathering, "return");
    REQUIRE(gathering.state().food > foodBefore);
    REQUIRE(gathering.state().food <= foodBefore + 30);
}

TEST_CASE("campaign forest missions preserve long term characters equipment and backpack") {
    tribe::CampaignGame game{{tribe::CampaignMode::Course, 73U, "燧火", "炎角", "生存"}};
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
    requireSuccess(game, "return");

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

TEST_CASE("campaign mission casualties leave the roster and are replaced by living reserves") {
    tribe::CampaignGame game{{tribe::CampaignMode::Course, 89U, "燧火", "炎角", "生存"}};
    requireSuccess(game, "mission forest");
    auto dangerous = game.state();
    tribe::ExpansionState& mission = *dangerous.activeMission;
    std::vector<std::string> deployedNames;
    for (tribe::Character& member : mission.squad.members) {
        deployedNames.push_back(member.name);
        member.life = 0;
    }
    mission.squad.members[mission.squad.leaderIndex].life = 1;
    mission.phase = tribe::ExpansionPhase::FrontlineCombat;
    mission.location = tribe::ExpansionLocation::StrangerClearing;
    mission.foreignStance = tribe::ForeignStance::Hostile;
    mission.frontline = 1;
    mission.enemyLife = 1000;
    mission.enemySpeed = 1000;

    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(dangerous, error));
    tribe::CampaignGame fatal = gameFrom(std::move(dangerous));
    const int populationBefore = fatal.state().population;
    requireSuccess(fatal, "attack");

    REQUIRE(fatal.state().phase == tribe::CampaignPhase::Managing);
    REQUIRE(!fatal.state().activeMission);
    REQUIRE(fatal.state().missionDeaths == static_cast<int>(deployedNames.size()));
    REQUIRE(fatal.state().population == populationBefore - static_cast<int>(deployedNames.size()));
    REQUIRE(fatal.state().roster.size() == 4U);
    for (const std::string& name : deployedNames) {
        REQUIRE(std::none_of(fatal.state().roster.begin(), fatal.state().roster.end(),
            [&](const tribe::Character& character) { return character.name == name; }));
    }
    REQUIRE(fatal.state().squads.front().members.size() == 4U);
    REQUIRE(fatal.state().squads.front().captain == fatal.state().squads.front().members.front());
    REQUIRE(tribe::CampaignGame::validateState(fatal.state(), error));
}

TEST_CASE("campaign mission losses cannot be erased when no reserve can rebuild the squad") {
    tribe::CampaignGame game{{tribe::CampaignMode::Course, 97U, "燧火", "炎角", "生存"}};
    auto reduced = game.state();
    const auto deployed = reduced.squads.front().members;
    reduced.roster.erase(std::remove_if(reduced.roster.begin(), reduced.roster.end(),
        [&](const tribe::Character& character) {
            return std::find(deployed.begin(), deployed.end(), character.name) == deployed.end();
        }), reduced.roster.end());

    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(reduced, error));
    tribe::CampaignGame fatal = gameFrom(std::move(reduced));
    requireSuccess(fatal, "mission forest");

    auto dangerous = fatal.state();
    tribe::ExpansionState& mission = *dangerous.activeMission;
    for (tribe::Character& member : mission.squad.members) member.life = 0;
    mission.squad.members[mission.squad.leaderIndex].life = 1;
    mission.phase = tribe::ExpansionPhase::FrontlineCombat;
    mission.location = tribe::ExpansionLocation::StrangerClearing;
    mission.foreignStance = tribe::ForeignStance::Hostile;
    mission.frontline = 1;
    mission.enemyLife = 1000;
    mission.enemySpeed = 1000;
    REQUIRE(tribe::CampaignGame::validateState(dangerous, error));

    tribe::CampaignGame isolated = gameFrom(std::move(dangerous));
    const int populationBefore = isolated.state().population;
    const auto result = requireSuccess(isolated, "attack");
    REQUIRE(result.endingReached);
    REQUIRE(isolated.state().phase == tribe::CampaignPhase::Finished);
    REQUIRE(isolated.state().ending == tribe::CampaignEnding::Extinction);
    REQUIRE(isolated.state().missionDeaths == static_cast<int>(deployed.size()));
    REQUIRE(isolated.state().population == populationBefore - static_cast<int>(deployed.size()));
    REQUIRE(isolated.state().roster.empty());
    REQUIRE(isolated.state().squads.empty());
    REQUIRE(!isolated.state().activeMission);
    REQUIRE(tribe::CampaignGame::validateState(isolated.state(), error));

    const auto after = isolated.state();
    const auto abort = isolated.execute("abort");
    REQUIRE(!abort.stateChanged);
    REQUIRE(isolated.state().missionDeaths == after.missionDeaths);
    REQUIRE(isolated.state().population == after.population);
}

TEST_CASE("campaign diplomacy supports marriage tribute alliance war and truce") {
    auto diplomatic = editableInitial();
    auto& river = diplomatic.relations[tribeIndex(tribe::TribeIdV2::RiverDeer)];
    river.relation = 80;
    river.trust = 70;
    diplomatic.technologies[technologyIndex(tribe::TechnologyId::Confederation)] = true;
    diplomatic.food = 100;
    tribe::CampaignGame diplomacy = gameFrom(diplomatic);
    requireSuccess(diplomacy, "marry river");
    requireSuccess(diplomacy, "ally river");
    requireSuccess(diplomacy, "tribute river");
    REQUIRE(diplomacy.state().relations[tribeIndex(tribe::TribeIdV2::RiverDeer)].marriage);
    REQUIRE(diplomacy.state().relations[tribeIndex(tribe::TribeIdV2::RiverDeer)].alliance);
    REQUIRE(diplomacy.state().relations[tribeIndex(tribe::TribeIdV2::RiverDeer)].playerPaysTribute);

    auto coercive = editableInitial();
    coercive.warriors = 8;
    auto& blackstone = coercive.relations[tribeIndex(tribe::TribeIdV2::Blackstone)];
    blackstone.fear = 70;
    coercive.food = 100;
    tribe::CampaignGame pressure = gameFrom(coercive);
    requireSuccess(pressure, "demand blackstone");
    requireSuccess(pressure, "declare rockfang");
    requireSuccess(pressure, "truce rockfang");
    REQUIRE(pressure.state().relations[tribeIndex(tribe::TribeIdV2::Blackstone)].otherPaysTribute);
    REQUIRE(!pressure.state().relations[tribeIndex(tribe::TribeIdV2::Rockfang)].atWar);
    REQUIRE(pressure.state().relations[tribeIndex(tribe::TribeIdV2::Rockfang)].truce);
}

TEST_CASE("campaign trade uses supply prices and unlocks shell currency") {
    auto state = editableInitial();
    state.food = 100;
    state.wood = 2;
    state.tradeCount = 7;
    state.technologies[technologyIndex(tribe::TechnologyId::SharedLanguage)] = true;
    state.tradePartners[tribeIndex(tribe::TribeIdV2::RiverDeer)] = true;
    state.tradePartners[tribeIndex(tribe::TribeIdV2::WhiteFeather)] = true;
    state.tradePartners[tribeIndex(tribe::TribeIdV2::Blackstone)] = true;
    state.discovered[locationIndex(tribe::WorldLocationId::RiverFord)] = true;
    tribe::CampaignGame game = gameFrom(state);
    const int woodBefore = game.state().wood;
    const auto result = requireSuccess(game, "trade river food wood");
    REQUIRE(result.message.find("价格受稀缺") != std::string::npos);
    REQUIRE(game.state().wood > woodBefore);
    REQUIRE(game.state().tradeCount == 8);
    REQUIRE(game.state().currencyUnlocked);
    REQUIRE(game.state().shells == 20);
}

TEST_CASE("campaign resident squads gain resources experience and fatigue") {
    auto state = editableInitial();
    state.food = 100;
    state.squads.front().residentMission = tribe::ResidentMission::Gather;
    const int eliteBefore = state.squads.front().eliteExperience;
    tribe::CampaignGame game = gameFrom(state);
    requireSuccess(game, "endturn");
    REQUIRE(game.state().season == 2);
    REQUIRE(game.state().squads.front().eliteExperience == eliteBefore + 8);
    REQUIRE(game.state().squads.front().fatigue == 15);
    const int fatigue = game.state().squads.front().fatigue;
    requireSuccess(game, "squadrest");
    REQUIRE(game.state().squads.front().fatigue < fatigue);

    auto capped = editableInitial();
    capped.population = 5;
    capped.warriors = 5;
    capped.food = 100;
    capped.squads.front().residentMission = tribe::ResidentMission::Train;
    tribe::CampaignGame cappedTraining = gameFrom(capped);
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
    tribe::CampaignGame game = gameFrom(state);
    const auto result = requireSuccess(game, "结束回合");
    REQUIRE(result.message.find("政变") != std::string::npos);
    REQUIRE(game.state().leaderName == successor);
    REQUIRE(game.state().actingLeaderName == successor);
    REQUIRE(game.state().leadershipHistory.size() == 2U);
}

TEST_CASE("campaign war defense cannot conquer and militia deaths reduce population") {
    auto defending = editableInitial();
    defending.phase = tribe::CampaignPhase::War;
    defending.relations[tribeIndex(tribe::TribeIdV2::Rockfang)].atWar = true;
    defending.war = {true, tribe::TribeIdV2::Rockfang, "石刃", 3, 0, 12, 1, 1,
        tribe::WarOrder::Hold, true};
    tribe::CampaignGame defense = gameFrom(defending);
    requireSuccess(defense, "defend");
    REQUIRE(defense.state().phase == tribe::CampaignPhase::War);
    REQUIRE(defense.state().war.front == 1);
    REQUIRE(defense.state().war.enemyPower == 1);
    REQUIRE(defense.state().warsWon == 0);
    requireSuccess(defense, "attack");
    REQUIRE(defense.state().war.front == 2);
    REQUIRE(defense.state().war.enemyPower > 0);

    auto finalFront = defense.state();
    finalFront.war.front = 3;
    finalFront.war.enemyPower = 1;
    tribe::CampaignGame victory = gameFrom(finalFront);
    requireSuccess(victory, "防御");
    REQUIRE(victory.state().phase == tribe::CampaignPhase::War);
    REQUIRE(victory.state().warsWon == 0);
    requireSuccess(victory, "攻击");
    REQUIRE(victory.state().phase == tribe::CampaignPhase::Managing);
    REQUIRE(victory.state().warsWon == 1);
    REQUIRE(victory.state().rockfangFortCaptured);

    auto dangerous = editableInitial();
    dangerous.phase = tribe::CampaignPhase::War;
    dangerous.relations[tribeIndex(tribe::TribeIdV2::Rockfang)].atWar = true;
    dangerous.war = {true, tribe::TribeIdV2::Rockfang, "石刃", 1, 2, 4, 30, 1,
        tribe::WarOrder::Advance, true};
    const int populationBefore = dangerous.population;
    tribe::CampaignGame battle = gameFrom(dangerous);
    requireSuccess(battle, "attack");
    REQUIRE(battle.state().population < populationBefore);
    REQUIRE(battle.state().warsLost == 1);
}

TEST_CASE("campaign modes reach their season limits deterministically") {
    const std::array<std::pair<tribe::CampaignMode, int>, 3> modes{{
        {tribe::CampaignMode::Quick, 8},
        {tribe::CampaignMode::Course, 16},
        {tribe::CampaignMode::Long, 32},
    }};
    for (const auto& [mode, turns] : modes) {
        auto state = editableInitial(mode);
        state.food = 5000;
        state.campDurability = 100;
        state.buildings[buildingIndex(tribe::BuildingId::Wall)] = true;
        tribe::CampaignGame game = gameFrom(state);
        for (int turn = 0; turn < turns; ++turn) requireSuccess(game, "endturn");
        REQUIRE(game.state().phase == tribe::CampaignPhase::EndingChoice);
        REQUIRE(game.state().season == game.state().seasonLimit);
    }

    tribe::CampaignGame first{{tribe::CampaignMode::Course, 211U, "燧火", "炎角", "生存"}};
    tribe::CampaignGame repeat{{tribe::CampaignMode::Course, 211U, "燧火", "炎角", "生存"}};
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
    for (const auto id : {tribe::TribeIdV2::RiverDeer, tribe::TribeIdV2::WhiteFeather}) {
        allianceState.relations[tribeIndex(id)].relation = 80;
        allianceState.relations[tribeIndex(id)].trust = 70;
        allianceState.relations[tribeIndex(id)].alliance = true;
    }
    tribe::CampaignGame alliance = gameFrom(allianceState);
    requireSuccess(alliance, "choose alliance");
    REQUIRE(alliance.state().ending == tribe::CampaignEnding::Alliance);
    REQUIRE(!alliance.endingSummary().epilogue.empty());

    auto conquestState = editableInitial();
    prepareEndingChoice(conquestState);
    conquestState.rockfangFortCaptured = true;
    conquestState.rockfangStrength = 0;
    conquestState.warriors = 6;
    conquestState.morale = 70;
    tribe::CampaignGame conquest = gameFrom(conquestState);
    requireSuccess(conquest, "choose conquest");
    REQUIRE(conquest.state().ending == tribe::CampaignEnding::Conquest);

    auto prosperityState = editableInitial();
    prepareEndingChoice(prosperityState);
    prosperityState.population = 24;
    prosperityState.food = 80;
    for (std::size_t index = 0; index < 4U; ++index) {
        prosperityState.buildings[index] = true;
        prosperityState.technologies[index] = true;
    }
    tribe::CampaignGame prosperity = gameFrom(prosperityState);
    requireSuccess(prosperity, "choose prosperity");
    REQUIRE(prosperity.state().ending == tribe::CampaignEnding::Prosperity);

    auto migrationState = editableInitial(tribe::CampaignMode::Long);
    prepareEndingChoice(migrationState);
    tribe::CampaignGame migration = gameFrom(migrationState);
    requireSuccess(migration, "choose migration");
    REQUIRE(migration.state().ending == tribe::CampaignEnding::Migration);
    requireSuccess(migration, "sandbox");
    REQUIRE(migration.state().phase == tribe::CampaignPhase::Sandbox);

    auto extinctionState = editableInitial();
    extinctionState.population = 0;
    extinctionState.warriors = 0;
    extinctionState.phase = tribe::CampaignPhase::Finished;
    extinctionState.ending = tribe::CampaignEnding::Extinction;
    tribe::CampaignGame extinction = gameFrom(extinctionState);
    REQUIRE(extinction.endingSummary().ending == tribe::CampaignEnding::Extinction);
    REQUIRE(extinction.endingSummary().title == "部落覆灭");
}
