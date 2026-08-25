#include "tribe/battle.hpp"
#include "tribe/console_ui.hpp"
#include "tribe/content.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace {

tribe::ActionResult execute(tribe::GameEngine& engine, const std::string& command) {
    return engine.execute(command);
}

void requireChanged(tribe::GameEngine& engine, const std::string& command) {
    const auto result = execute(engine, command);
    if (!result.stateChanged) throw std::runtime_error(command + " failed: " + result.message);
}

void gatherFoodUntilSeasonEnd(tribe::GameEngine& engine) {
    while (engine.state().phase == tribe::Phase::Playing && engine.state().actionsLeft > 0) {
        const auto result = execute(engine, "gather food");
        if (!result.stateChanged) {
            if (result.message.find("上限") != std::string::npos) break;
            throw std::runtime_error("gather food failed: " + result.message);
        }
    }
    if (engine.state().phase == tribe::Phase::Playing) requireChanged(engine, "endturn");
}

void replace(tribe::GameEngine& engine, const std::function<void(tribe::GameState&)>& change) {
    tribe::GameState candidate = engine.state();
    change(candidate);
    std::string error;
    REQUIRE(engine.replaceState(candidate, error));
    REQUIRE(error.empty());
}

void setTurn(tribe::GameState& state, const int turn) {
    state.turn = turn;
    state.currentEvent = state.eventSchedule.at(static_cast<std::size_t>(turn - 1));
}

std::uint32_t seedWithEvent(const std::size_t turnIndex, const tribe::EventId eventId) {
    for (std::uint32_t seed = 1; seed < 100000U; ++seed) {
        if (tribe::GameEngine::eventScheduleForSeed(seed).at(turnIndex) == static_cast<int>(eventId)) return seed;
    }
    throw std::runtime_error("unable to find event seed");
}

tribe::GameState finalChoiceState(tribe::GameEngine& engine) {
    tribe::GameState state = engine.state();
    setTurn(state, 16);
    state.actionsLeft = 0;
    state.phase = tribe::Phase::FinalChoice;
    state.pendingRaid = false;
    state.ending = tribe::Ending::None;
    return state;
}

std::filesystem::path testSaveRoot() {
    return std::filesystem::current_path() / "formal-test-saves";
}

std::string resourceCommand(const tribe::GameState& state, const int food, const int wood,
    const int stone, const std::string& readyCommand) {
    if (state.food < food) return "gather food";
    if (state.wood < wood) return "gather wood";
    if (state.stone < stone) return "gather stone";
    return readyCommand;
}

std::string nextAllianceCommand(const tribe::GameEngine& engine) {
    const tribe::GameState& state = engine.state();
    const auto building = [&](const tribe::BuildingId id) { return state.buildings[tribe::indexOf(id)]; };
    const auto technology = [&](const tribe::TechnologyId id) { return state.technologies[tribe::indexOf(id)]; };
    const auto discovered = [&](const tribe::LocationId id) { return state.discovered[tribe::indexOf(id)]; };

    if (state.food < 18) return "gather food";
    if (!discovered(tribe::LocationId::Quarry)) return "scout quarry";
    if (!technology(tribe::TechnologyId::FoodPreservation)) {
        return resourceCommand(state, 4, 4, 0, "research preservation");
    }
    if (!discovered(tribe::LocationId::Marsh)) return "scout marsh";
    if (!discovered(tribe::LocationId::RiverFord)) return "scout riverford";
    if (!discovered(tribe::LocationId::WhiteFeatherCamp)) return "scout whitefeather";
    if (!discovered(tribe::LocationId::OldPass)) return "scout pass";
    if (state.warriors < 5) return state.food >= 4 ? "train" : "gather food";
    if (!building(tribe::BuildingId::Workshop)) {
        return resourceCommand(state, 0, 10, 8, "build workshop");
    }
    if (!building(tribe::BuildingId::HealerHut)) {
        if (state.herbs < 3) return "gather herbs";
        return resourceCommand(state, 0, 8, 4, "build healer");
    }
    if (!building(tribe::BuildingId::CouncilFire)) {
        return resourceCommand(state, 0, 8, 6, "build council");
    }
    if (!technology(tribe::TechnologyId::HerbalKnowledge)) {
        return resourceCommand(state, 6, 0, 4, "research herbalism");
    }
    if (!technology(tribe::TechnologyId::GiftCustoms)) {
        return resourceCommand(state, 4, 4, 0, "research gifts");
    }
    if (!technology(tribe::TechnologyId::SharedLanguage)) {
        return resourceCommand(state, 6, 0, 4, "research language");
    }
    if (!technology(tribe::TechnologyId::Confederation)) {
        return resourceCommand(state, 8, 6, 6, "research confederation");
    }

    const std::size_t river = tribe::indexOf(tribe::FactionId::RiverDeer);
    const std::size_t white = tribe::indexOf(tribe::FactionId::WhiteFeather);
    const std::size_t rock = tribe::indexOf(tribe::FactionId::Rockfang);
    if (state.quests[river] == 0) return "talk riverdeer";
    if (state.quests[river] == 1) return state.food >= 8 ? "quest riverdeer" : "gather food";
    if (state.quests[river] == 2) return "quest riverdeer";
    if (state.quests[white] == 0) return "talk whitefeather";
    if (state.quests[white] == 1) return state.food >= 6 ? "quest whitefeather" : "gather food";
    if (state.quests[white] == 2) return "quest whitefeather";
    if (state.quests[rock] == 0) return "talk rockfang";
    if (state.quests[rock] == 1) return "quest rockfang";
    if (state.quests[rock] == 2) return state.food >= 6 ? "quest rockfang" : "gather food";
    if (state.relations[river] < 70) return state.food >= 2 ? "gift riverdeer" : "gather food";
    if (state.relations[white] < 70) return state.food >= 2 ? "gift whitefeather" : "gather food";
    if (state.food < 50) return "gather food";
    return "guard";
}

} // namespace

TEST_CASE("formal content contains nine locations six buildings nine technologies and all events") {
    REQUIRE(tribe::locations().size() == 9U);
    REQUIRE(tribe::buildings().size() == 6U);
    REQUIRE(tribe::technologies().size() == 9U);
    REQUIRE(tribe::events().size() == 22U);
    REQUIRE(tribe::areAdjacent(tribe::LocationId::Camp, tribe::LocationId::Forest));
    REQUIRE(!tribe::areAdjacent(tribe::LocationId::Camp, tribe::LocationId::RockfangFort));
}

TEST_CASE("formal and quick modes start at the promised years with the initial map") {
    tribe::GameEngine formal({tribe::GameMode::Standard, 7U});
    tribe::GameEngine quick({tribe::GameMode::Quick, 7U});
    REQUIRE(formal.state().turn == 1);
    REQUIRE(quick.state().turn == 9);
    REQUIRE(formal.state().population >= 15);
    REQUIRE(quick.state().population >= 17);
    REQUIRE(formal.state().discovered[tribe::indexOf(tribe::LocationId::Camp)]);
    REQUIRE(formal.state().discovered[tribe::indexOf(tribe::LocationId::Forest)]);
    REQUIRE(formal.state().discovered[tribe::indexOf(tribe::LocationId::RedPlain)]);
    REQUIRE(!formal.state().discovered[tribe::indexOf(tribe::LocationId::Marsh)]);
    REQUIRE(quick.state().discovered[tribe::indexOf(tribe::LocationId::Marsh)]);
    REQUIRE(quick.state().buildings[tribe::indexOf(tribe::BuildingId::Granary)]);
}

TEST_CASE("formal event schedules are reproducible and vary by seed") {
    const auto first = tribe::GameEngine::eventScheduleForSeed(123U);
    const auto repeat = tribe::GameEngine::eventScheduleForSeed(123U);
    const auto different = tribe::GameEngine::eventScheduleForSeed(124U);
    REQUIRE(first == repeat);
    REQUIRE(first != different);
    REQUIRE(first[3] == static_cast<int>(tribe::EventId::RiverEnvoys));
    REQUIRE(first[7] == static_cast<int>(tribe::EventId::WhiteFeatherSign));
    REQUIRE(first[11] == static_cast<int>(tribe::EventId::RockfangRaid));
    REQUIRE(first[15] == static_cast<int>(tribe::EventId::FinalCouncil));
}

TEST_CASE("formal numeric Chinese and English gather commands are equivalent") {
    tribe::GameEngine numeric({tribe::GameMode::Standard, 11U});
    tribe::GameEngine english({tribe::GameMode::Standard, 11U});
    tribe::GameEngine chinese({tribe::GameMode::Standard, 11U});
    REQUIRE(execute(numeric, "3").stateChanged);
    REQUIRE(execute(english, "GATHER   FOOD").stateChanged);
    REQUIRE(execute(chinese, "采集 食物").stateChanged);
    REQUIRE(numeric.state() == english.state());
    REQUIRE(english.state() == chinese.state());
}

TEST_CASE("formal unknown blank and malformed commands do not mutate state") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 9U});
    const auto before = engine.state();
    REQUIRE(!execute(engine, "   ").recognized);
    REQUIRE(!execute(engine, "nonsense").recognized);
    const auto malformed = execute(engine, "build");
    REQUIRE(malformed.recognized);
    REQUIRE(!malformed.stateChanged);
    REQUIRE(engine.state() == before);
}

TEST_CASE("formal resource failures are atomic and do not consume teams") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 5U});
    replace(engine, [](tribe::GameState& state) {
        state.food = 0;
        state.wood = 0;
        state.stone = 0;
        state.herbs = 0;
    });
    const auto before = engine.state();
    REQUIRE(!execute(engine, "train").stateChanged);
    REQUIRE(!execute(engine, "build granary").stateChanged);
    REQUIRE(!execute(engine, "scout rockfangfort").stateChanged);
    REQUIRE(engine.state() == before);
}

TEST_CASE("formal exploration follows map adjacency and unlocks resource locations") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 13U});
    const auto blocked = execute(engine, "侦察 白羽营地");
    REQUIRE(blocked.recognized);
    REQUIRE(!blocked.stateChanged);
    REQUIRE(execute(engine, "11").stateChanged);
    REQUIRE(engine.state().discovered[tribe::indexOf(tribe::LocationId::Marsh)]);
    REQUIRE(execute(engine, "侦察 白羽").stateChanged);
    REQUIRE(engine.state().discovered[tribe::indexOf(tribe::LocationId::WhiteFeatherCamp)]);
    REQUIRE(execute(engine, "采集 草药").stateChanged);
}

TEST_CASE("formal buildings are unique and apply their promised effects") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 15U});
    replace(engine, [](tribe::GameState& state) {
        state.wood = 30;
        state.stone = 20;
        state.herbs = 10;
    });
    REQUIRE(execute(engine, "21").stateChanged);
    REQUIRE(engine.state().buildings[tribe::indexOf(tribe::BuildingId::Granary)]);
    const auto built = engine.state();
    REQUIRE(!execute(engine, "建造 粮仓").stateChanged);
    REQUIRE(engine.state() == built);
    REQUIRE(execute(engine, "22").stateChanged);
    REQUIRE(engine.permanentDefense() == 6);
}

TEST_CASE("formal technology tiers enforce prerequisites and workshop") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 17U});
    replace(engine, [](tribe::GameState& state) {
        state.food = 60;
        state.wood = 60;
        state.stone = 40;
        state.actionsLeft = 3;
    });
    REQUIRE(!execute(engine, "research herbalism").stateChanged);
    REQUIRE(execute(engine, "research preservation").stateChanged);
    REQUIRE(!execute(engine, "research herbalism").stateChanged);
    replace(engine, [](tribe::GameState& state) {
        state.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
        state.actionsLeft = 3;
    });
    REQUIRE(execute(engine, "研究 草药知识").stateChanged);
    REQUIRE(execute(engine, "research irrigation").stateChanged);
    const int before = engine.state().food;
    REQUIRE(execute(engine, "gather food").stateChanged);
    REQUIRE(engine.state().food >= before);
}

TEST_CASE("formal population controls teams and winter settlement uses extra food") {
    const std::uint32_t seed = seedWithEvent(4U, tribe::EventId::ClearSky);
    tribe::GameEngine engine({tribe::GameMode::Standard, seed});
    replace(engine, [](tribe::GameState& state) {
        setTurn(state, 4);
        state.population = 9;
        state.warriors = 3;
        state.actionsLeft = 2;
        state.food = 5;
    });
    const auto result = execute(engine, "endturn");
    REQUIRE(result.seasonAdvanced);
    REQUIRE(result.message.find("需要5单位食物") != std::string::npos);
    REQUIRE(engine.state().turn == 5);
    REQUIRE(engine.state().actionsLeft == 2);
}

TEST_CASE("formal twelfth season creates a mandatory raid and defense resolves it") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 19U});
    replace(engine, [](tribe::GameState& state) {
        setTurn(state, 11);
        state.food = 60;
        state.warriors = 8;
        state.population = 16;
        state.morale = 80;
        state.buildings[tribe::indexOf(tribe::BuildingId::Wall)] = true;
        state.discovered[tribe::indexOf(tribe::LocationId::Quarry)] = true;
        state.scouted[tribe::indexOf(tribe::LocationId::Quarry)] = true;
        state.scouted[tribe::indexOf(tribe::LocationId::OldPass)] = true;
        state.discovered[tribe::indexOf(tribe::LocationId::OldPass)] = true;
    });
    REQUIRE(execute(engine, "下一季").seasonAdvanced);
    REQUIRE(engine.state().turn == 12);
    REQUIRE(engine.state().phase == tribe::Phase::AwaitingRaid);
    REQUIRE(!execute(engine, "gather food").stateChanged);
    const auto battle = execute(engine, "应战 防守");
    REQUIRE(battle.stateChanged);
    REQUIRE(engine.state().phase == tribe::Phase::Playing);
    REQUIRE(!engine.state().pendingRaid);
}

TEST_CASE("formal battle strategy covers assault ambush defend and retreat") {
    tribe::BattleContext context;
    context.isRaid = true;
    context.battlefieldScouted = true;
    context.hasFlintSpear = true;
    context.hasShieldWall = true;
    context.hasAmbushTraining = true;
    context.warriors = 7;
    context.morale = 70;
    context.defense = 9;
    context.enemyStrength = 14;
    tribe::BattleSystem system;
    REQUIRE(system.resolve(context, tribe::Tactic::Assault).playerPower > 0);
    REQUIRE(system.resolve(context, tribe::Tactic::Ambush).playerPower > 0);
    REQUIRE(system.resolve(context, tribe::Tactic::Defend).playerPower > 0);
    const auto retreat = system.resolve(context, tribe::Tactic::Retreat);
    REQUIRE(retreat.retreated);
    REQUIRE(retreat.casualties == 0);
}

TEST_CASE("formal river diplomacy requires talk and completes two tasks") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 21U});
    replace(engine, [](tribe::GameState& state) {
        state.discovered[tribe::indexOf(tribe::LocationId::RiverFord)] = true;
        state.scouted[tribe::indexOf(tribe::LocationId::RiverFord)] = true;
        state.buildings[tribe::indexOf(tribe::BuildingId::Wall)] = true;
        state.food = 60;
        state.actionsLeft = 3;
    });
    REQUIRE(!execute(engine, "quest riverdeer").stateChanged);
    REQUIRE(execute(engine, "talk riverdeer").stateChanged);
    REQUIRE(execute(engine, "协助 河鹿").stateChanged);
    REQUIRE(execute(engine, "quest riverdeer").stateChanged);
    REQUIRE(engine.state().quests[tribe::indexOf(tribe::FactionId::RiverDeer)] == 3);
    REQUIRE(engine.state().relations[tribe::indexOf(tribe::FactionId::RiverDeer)] >= 70);
}

TEST_CASE("formal white feather and rockfang tasks gate healing and truce") {
    tribe::GameEngine white({tribe::GameMode::Quick, 23U});
    replace(white, [](tribe::GameState& state) {
        state.discovered[tribe::indexOf(tribe::LocationId::WhiteFeatherCamp)] = true;
        state.scouted[tribe::indexOf(tribe::LocationId::WhiteFeatherCamp)] = true;
        state.food = 60;
        state.actionsLeft = 3;
    });
    REQUIRE(execute(white, "talk whitefeather").stateChanged);
    REQUIRE(execute(white, "quest whitefeather").stateChanged);
    REQUIRE(!execute(white, "quest whitefeather").stateChanged);
    replace(white, [](tribe::GameState& state) {
        state.buildings[tribe::indexOf(tribe::BuildingId::HealerHut)] = true;
        state.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
        state.technologies[tribe::indexOf(tribe::TechnologyId::HerbalKnowledge)] = true;
        state.technologies[tribe::indexOf(tribe::TechnologyId::FoodPreservation)] = true;
        state.actionsLeft = 3;
    });
    REQUIRE(execute(white, "quest whitefeather").stateChanged);

    tribe::GameEngine rock({tribe::GameMode::Quick, 25U});
    replace(rock, [](tribe::GameState& state) {
        state.discovered[tribe::indexOf(tribe::LocationId::OldPass)] = true;
        state.scouted[tribe::indexOf(tribe::LocationId::OldPass)] = true;
        state.warriors = 6;
        state.population = 18;
        state.food = 60;
        state.actionsLeft = 3;
    });
    REQUIRE(execute(rock, "talk rockfang").stateChanged);
    REQUIRE(execute(rock, "quest rockfang").stateChanged);
    replace(rock, [](tribe::GameState& state) {
        state.relations[tribe::indexOf(tribe::FactionId::Rockfang)] = 0;
        state.actionsLeft = 3;
    });
    REQUIRE(execute(rock, "quest rockfang").stateChanged);
    REQUIRE(rock.state().rockfangTruce);
}

TEST_CASE("formal exposes and selects alliance conquest prosperity migration and extinction endings") {
    const struct Case {
        tribe::Ending ending;
        const char* command;
    } cases[] = {
        {tribe::Ending::Alliance, "choose alliance"},
        {tribe::Ending::Conquest, "选择 征服"},
        {tribe::Ending::Prosperity, "choose prosperity"},
        {tribe::Ending::Migration, "74"},
    };
    for (const auto& item : cases) {
        tribe::GameEngine engine({tribe::GameMode::Standard, 27U});
        auto state = finalChoiceState(engine);
        if (item.ending == tribe::Ending::Alliance) {
            state.quests = {{3, 3, 3}};
            state.relations = {{70, 70, 0}};
            state.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
            state.technologies[tribe::indexOf(tribe::TechnologyId::GiftCustoms)] = true;
            state.technologies[tribe::indexOf(tribe::TechnologyId::SharedLanguage)] = true;
            state.technologies[tribe::indexOf(tribe::TechnologyId::Confederation)] = true;
            state.rockfangTruce = true;
        } else if (item.ending == tribe::Ending::Conquest) {
            state.rockfangStrength = 0;
            state.rockfangFortCaptured = true;
            state.discovered.fill(true);
            state.scouted.fill(true);
            state.warriors = 6;
            state.morale = 45;
        } else if (item.ending == tribe::Ending::Prosperity) {
            state.population = 22;
            state.food = 40;
            for (std::size_t index = 0; index < 4U; ++index) state.buildings[index] = true;
            for (std::size_t index = 0; index < 4U; ++index) state.technologies[index] = true;
        }
        std::string error;
        REQUIRE(engine.replaceState(state, error));
        const auto result = execute(engine, item.command);
        REQUIRE(result.stateChanged);
        REQUIRE(engine.state().phase == tribe::Phase::Finished);
        REQUIRE(engine.state().ending == item.ending);
        const auto finished = engine.state();
        REQUIRE(!execute(engine, "gather food").stateChanged);
        REQUIRE(engine.state() == finished);
    }

    tribe::GameEngine extinct({tribe::GameMode::Standard, 29U});
    auto state = extinct.state();
    state.population = 0;
    state.warriors = 0;
    state.phase = tribe::Phase::Finished;
    state.ending = tribe::Ending::Extinction;
    std::string error;
    REQUIRE(extinct.replaceState(state, error));
    REQUIRE(extinct.state().ending == tribe::Ending::Extinction);
}

TEST_CASE("formal official conquest route reaches the ending through ordinary commands") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 1U});
    const std::vector<std::vector<std::string>> openingSeasons{
        {"scout quarry", "gather food", "gather food"},
        {"scout pass", "gather food", "gather food"},
        {"scout rockfangfort", "train", "gather food"},
        {"research spear", "train", "gather food"},
        {"train", "train", "gather food"},
        {"train", "attack rockfang assault", "attack rockfang assault"},
        {"attack rockfang assault", "gather food", "gather food"},
    };
    for (const auto& season : openingSeasons) {
        for (const auto& command : season) requireChanged(engine, command);
        requireChanged(engine, "endturn");
    }
    while (engine.state().phase != tribe::Phase::FinalChoice
        && engine.state().phase != tribe::Phase::Finished) {
        if (engine.state().phase == tribe::Phase::AwaitingRaid) requireChanged(engine, "battle defend");
        else gatherFoodUntilSeasonEnd(engine);
    }
    REQUIRE(engine.state().phase == tribe::Phase::FinalChoice);
    requireChanged(engine, "choose conquest");
    REQUIRE(engine.state().ending == tribe::Ending::Conquest);
}

TEST_CASE("formal official migration route survives all seasons without special ending goals") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 2U});
    while (engine.state().phase != tribe::Phase::FinalChoice
        && engine.state().phase != tribe::Phase::Finished) {
        if (engine.state().phase == tribe::Phase::AwaitingRaid) requireChanged(engine, "battle retreat");
        else gatherFoodUntilSeasonEnd(engine);
    }
    REQUIRE(engine.state().phase == tribe::Phase::FinalChoice);
    requireChanged(engine, "choose migration");
    REQUIRE(engine.state().ending == tribe::Ending::Migration);
}

TEST_CASE("formal neglect route can reach extinction through ordinary commands") {
    bool foundExtinction = false;
    for (std::uint32_t seed = 1; seed <= 5000U && !foundExtinction; ++seed) {
        tribe::GameEngine engine({tribe::GameMode::Standard, seed});
        while (engine.state().phase != tribe::Phase::Finished
            && engine.state().phase != tribe::Phase::FinalChoice) {
            if (engine.state().phase == tribe::Phase::AwaitingRaid) {
                const auto battle = execute(engine, "battle assault");
                REQUIRE(battle.stateChanged);
            } else {
                const auto settlement = execute(engine, "endturn");
                REQUIRE(settlement.stateChanged);
            }
        }
        foundExtinction = engine.state().ending == tribe::Ending::Extinction;
    }
    REQUIRE(foundExtinction);
}

TEST_CASE("formal quick mode completes its eight seasons and reaches a selectable ending") {
    tribe::GameEngine engine({tribe::GameMode::Quick, 6U});
    REQUIRE(engine.state().turn == 9);
    while (engine.state().phase != tribe::Phase::FinalChoice
        && engine.state().phase != tribe::Phase::Finished) {
        if (engine.state().phase == tribe::Phase::AwaitingRaid) requireChanged(engine, "battle retreat");
        else gatherFoodUntilSeasonEnd(engine);
    }
    REQUIRE(engine.state().phase == tribe::Phase::FinalChoice);
    REQUIRE(engine.state().turn == 16);
    requireChanged(engine, "choose migration");
    REQUIRE(engine.state().ending == tribe::Ending::Migration);
}

TEST_CASE("formal official prosperity route grows population and completes four buildings and technologies") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 3U});
    const auto doSeason = [&](const std::vector<std::string>& commands) {
        for (const auto& command : commands) requireChanged(engine, command);
        requireChanged(engine, "endturn");
    };
    doSeason({"scout quarry", "gather food", "celebrate"});
    doSeason({"research preservation", "gather stone", "gather wood"});
    doSeason({"gather stone", "gather food", "build granary"});
    doSeason({"gather wood", "gather wood", "build workshop"});
    doSeason({"gather food", "celebrate", "gather food"});
    doSeason({"gather wood", "gather wood", "gather stone"});
    doSeason({"gather food", "gather wood", "build wall"});
    doSeason({"gather wood", "gather stone", "research spear"});
    doSeason({"gather food", "celebrate", "gather food"});
    doSeason({"gather wood", "gather stone", "research gifts"});
    doSeason({"gather food", "build watchtower", "research language"});
    REQUIRE(engine.state().phase == tribe::Phase::AwaitingRaid);
    requireChanged(engine, "battle defend");
    while (engine.state().phase != tribe::Phase::FinalChoice
        && engine.state().phase != tribe::Phase::Finished) {
        if (engine.state().phase == tribe::Phase::AwaitingRaid) requireChanged(engine, "battle defend");
        else {
            if (tribe::seasonForTurn(engine.state().turn) == tribe::Season::Spring
                && engine.state().morale < 65 && engine.state().food >= 3 && engine.state().actionsLeft > 0) {
                requireChanged(engine, "celebrate");
            }
            gatherFoodUntilSeasonEnd(engine);
        }
    }
    REQUIRE(engine.state().phase == tribe::Phase::FinalChoice);
    REQUIRE(engine.state().population >= 20);
    REQUIRE(engine.buildingCount() >= 4);
    REQUIRE(engine.technologyCount() >= 4);
    REQUIRE(engine.state().food >= 40);
    requireChanged(engine, "choose prosperity");
    REQUIRE(engine.state().ending == tribe::Ending::Prosperity);
}

TEST_CASE("formal official alliance route completes three diplomatic stories through ordinary commands") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 4U});
    while (engine.state().phase != tribe::Phase::FinalChoice
        && engine.state().phase != tribe::Phase::Finished) {
        if (engine.state().phase == tribe::Phase::AwaitingRaid) {
            requireChanged(engine, "battle defend");
            continue;
        }
        while (engine.state().phase == tribe::Phase::Playing && engine.state().actionsLeft > 0) {
            requireChanged(engine, nextAllianceCommand(engine));
        }
        if (engine.state().phase == tribe::Phase::Playing) requireChanged(engine, "endturn");
    }
    REQUIRE(engine.state().phase == tribe::Phase::FinalChoice);
    const auto endings = engine.availableEndings();
    if (std::find(endings.begin(), endings.end(), tribe::Ending::Alliance) == endings.end()) {
        throw std::runtime_error(engine.objectivesText() + "\n" + engine.statusText());
    }
    requireChanged(engine, "choose alliance");
    REQUIRE(engine.state().ending == tribe::Ending::Alliance);
}

TEST_CASE("formal save repository round trips every field for all four slots") {
    const auto root = testSaveRoot();
    std::error_code code;
    std::filesystem::remove_all(root, code);
    tribe::SaveRepository repository(root);
    tribe::GameEngine source({tribe::GameMode::Quick, 31U});
    REQUIRE(execute(source, "gather wood").stateChanged);
    for (const auto slot : {tribe::SaveSlot::Slot1, tribe::SaveSlot::Slot2,
             tribe::SaveSlot::Slot3, tribe::SaveSlot::Autosave}) {
        std::string error;
        REQUIRE(repository.save(source.state(), slot, error));
        tribe::GameState loaded;
        REQUIRE(repository.load(slot, loaded, error));
        REQUIRE(loaded == source.state());
    }
    std::filesystem::remove_all(root, code);
}

TEST_CASE("formal damaged save is rejected without changing the destination state") {
    const auto root = testSaveRoot();
    std::error_code code;
    std::filesystem::remove_all(root, code);
    std::filesystem::create_directories(root, code);
    REQUIRE(!code);
    {
        std::ofstream output(root / "slot1.sav", std::ios::binary | std::ios::trunc);
        output << "format=tribe-dawn\nversion=1\npopulation=not-a-number\n";
    }
    tribe::SaveRepository repository(root);
    tribe::GameEngine engine({tribe::GameMode::Standard, 33U});
    tribe::GameState destination = engine.state();
    const auto before = destination;
    std::string error;
    REQUIRE(!repository.load(tribe::SaveSlot::Slot1, destination, error));
    REQUIRE(!error.empty());
    REQUIRE(destination == before);
    std::filesystem::remove_all(root, code);
}

TEST_CASE("formal state validation rejects corrupt event and inconsistent fortress atomically") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 35U});
    const auto before = engine.state();
    auto badEvent = before;
    badEvent.eventSchedule[0] = static_cast<int>(tribe::EventId::FinalCouncil);
    std::string error;
    REQUIRE(!engine.replaceState(badEvent, error));
    REQUIRE(engine.state() == before);
    auto badFort = before;
    badFort.rockfangFortCaptured = true;
    REQUIRE(!engine.replaceState(badFort, error));
    REQUIRE(engine.state() == before);
}

TEST_CASE("formal plain UI emits UTF8 text without ANSI escape sequences") {
    tribe::GameEngine engine({tribe::GameMode::Standard, 37U});
    std::ostringstream output;
    tribe::ConsoleUI ui(output, false, false);
    ui.renderGame(engine, "测试消息");
    REQUIRE(output.str().find("燧火纪：部落黎明") != std::string::npos);
    REQUIRE(output.str().find("测试消息") != std::string::npos);
    REQUIRE(output.str().find("\x1b[") == std::string::npos);
}
