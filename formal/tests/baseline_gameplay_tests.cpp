#include "tribe/battle.hpp"
#include "tribe/game_engine.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

void setTurn(tribe::GameState& state, const int turn) {
    state.turn = turn;
    state.currentEvent = state.eventSchedule.at(static_cast<std::size_t>(turn - 1));
}

void requireValid(const tribe::GameEngine& engine) {
    std::string error;
    REQUIRE(tribe::GameEngine::validateState(engine.state(), error));
    REQUIRE(error.empty());
}

void replace(tribe::GameEngine& engine, tribe::GameState state) {
    std::string error;
    REQUIRE(engine.replaceState(state, error));
    REQUIRE(error.empty());
    requireValid(engine);
}

void revealRockfangRoute(tribe::GameState& state) {
    for (const tribe::LocationId id : {tribe::LocationId::RiverFord,
             tribe::LocationId::OldPass, tribe::LocationId::RockfangFort}) {
        state.discovered[tribe::indexOf(id)] = true;
        state.scouted[tribe::indexOf(id)] = true;
    }
}

} // namespace

TEST_CASE("baseline guard defense survives seasons until the raid and then clears") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 101U});
    auto state = engine.state();
    setTurn(state, 9);
    state.actionsLeft = 3;
    state.population = 16;
    state.food = 60;
    state.warriors = 8;
    state.morale = 80;
    replace(engine, state);

    const auto guard = engine.execute("guard");
    REQUIRE(guard.stateChanged);
    REQUIRE(engine.state().temporaryDefense == 4);
    requireValid(engine);

    REQUIRE(engine.execute("endturn").seasonAdvanced);
    REQUIRE(engine.state().turn == 10);
    REQUIRE(engine.state().temporaryDefense == 4);
    requireValid(engine);
    REQUIRE(engine.execute("endturn").seasonAdvanced);
    REQUIRE(engine.state().turn == 11);
    REQUIRE(engine.state().temporaryDefense == 4);
    requireValid(engine);
    REQUIRE(engine.execute("endturn").seasonAdvanced);
    REQUIRE(engine.state().turn == 12);
    REQUIRE(engine.state().phase == tribe::Phase::AwaitingRaid);
    REQUIRE(engine.state().temporaryDefense == 4);
    requireValid(engine);

    const auto battle = engine.execute("battle defend");
    REQUIRE(battle.stateChanged);
    REQUIRE(engine.state().phase == tribe::Phase::Playing);
    REQUIRE(engine.state().temporaryDefense == 0);
    requireValid(engine);
}

TEST_CASE("baseline raid defeat at zero strength can be followed by occupation and conquest") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 103U});
    auto state = engine.state();
    setTurn(state, 11);
    state.actionsLeft = 3;
    state.population = 16;
    state.food = 60;
    state.warriors = 8;
    state.morale = 80;
    state.rockfangStrength = 4;
    revealRockfangRoute(state);
    replace(engine, state);

    REQUIRE(engine.execute("endturn").seasonAdvanced);
    REQUIRE(engine.state().phase == tribe::Phase::AwaitingRaid);
    REQUIRE(engine.execute("battle assault").stateChanged);
    REQUIRE(engine.state().rockfangStrength == 0);
    REQUIRE(!engine.state().rockfangFortCaptured);
    requireValid(engine);

    const auto occupation = engine.execute("attack rockfang assault");
    REQUIRE(occupation.stateChanged);
    REQUIRE(engine.state().rockfangFortCaptured);
    REQUIRE(engine.state().actionsLeft == 2);
    requireValid(engine);

    state = engine.state();
    setTurn(state, 16);
    state.actionsLeft = 0;
    state.phase = tribe::Phase::FinalChoice;
    state.pendingRaid = false;
    state.ending = tribe::Ending::None;
    replace(engine, state);
    REQUIRE(engine.execute("choose conquest").stateChanged);
    REQUIRE(engine.state().ending == tribe::Ending::Conquest);
    requireValid(engine);
}

TEST_CASE("baseline battle system rejects an invalid tactic without dereferencing null") {
    tribe::BattleContext context;
    context.warriors = 6;
    context.morale = 60;
    context.enemyStrength = 10;

    tribe::BattleSystem system;
    const auto invalid = system.resolve(context, static_cast<tribe::Tactic>(999));
    REQUIRE(!invalid.valid);
    REQUIRE(!invalid.retreated);
    REQUIRE(!invalid.victory);
    REQUIRE(invalid.playerPower == 0);
    REQUIRE(invalid.message.find("非法战术") != std::string::npos);
}

TEST_CASE("baseline scouted ambush is safer while assault deals more damage") {
    tribe::BattleContext context;
    context.battlefieldScouted = true;
    context.warriors = 8;
    context.morale = 80;
    context.enemyStrength = 10;

    tribe::BattleSystem system;
    const auto assault = system.resolve(context, tribe::Tactic::Assault);
    const auto ambush = system.resolve(context, tribe::Tactic::Ambush);
    REQUIRE(assault.valid);
    REQUIRE(ambush.valid);
    REQUIRE(ambush.playerPower > assault.playerPower);
    REQUIRE(assault.enemyDamage > ambush.enemyDamage);
}
