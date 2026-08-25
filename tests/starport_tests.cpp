#include "scenarios.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

mud::CommandResult execute(mud::Scenario& scenario, const std::string& input) {
    return scenario.execute(mud::parseCommand(input));
}

bool contains(const std::string& text, const std::string& expected) {
    return text.find(expected) != std::string::npos;
}

} // namespace

TEST_CASE("starport official English route wins deterministically") {
    auto scenario = mud::makeStarportScenario();

    for (const auto* command : {
             "go north", "go west", "attack drone", "attack drone", "search",
             "use medkit", "go east", "go east", "repair reactor"}) {
        const auto result = execute(*scenario, command);
        REQUIRE(result.recognized);
        REQUIRE(result.stateChanged);
        REQUIRE(result.consumesAction);
    }

    const auto fields = scenario->saveFields();
    REQUIRE(scenario->outcome() == mud::Outcome::Won);
    REQUIRE(fields.at("room") == "reactor");
    REQUIRE(fields.at("hp") == "20");
    REQUIRE(fields.at("oxygen") == "3");
    REQUIRE(fields.at("drone_hp") == "0");
    REQUIRE(fields.at("reactor_repaired") == "1");
    REQUIRE(fields.at("inventory") == "wrench");
    REQUIRE(fields.at("outcome") == "won");
}

TEST_CASE("starport combat gates search and medkit is single use") {
    auto scenario = mud::makeStarportScenario();
    execute(*scenario, "go north");
    execute(*scenario, "go west");

    const auto blockedSearch = execute(*scenario, "search");
    REQUIRE(blockedSearch.recognized);
    REQUIRE(!blockedSearch.stateChanged);
    REQUIRE(!blockedSearch.consumesAction);
    REQUIRE(scenario->saveFields().at("oxygen") == "10");

    const auto firstAttack = execute(*scenario, "attack drone");
    REQUIRE(contains(firstAttack.message, "反击"));
    REQUIRE(scenario->saveFields().at("hp") == "16");
    REQUIRE(scenario->saveFields().at("drone_hp") == "6");
    REQUIRE(scenario->saveFields().at("oxygen") == "9");

    const auto stillBlocked = execute(*scenario, "搜索");
    REQUIRE(!stillBlocked.stateChanged);
    REQUIRE(scenario->saveFields().at("oxygen") == "9");

    execute(*scenario, "attack drone");
    const auto search = execute(*scenario, "search");
    REQUIRE(search.stateChanged);
    REQUIRE(scenario->saveFields().at("inventory") == "wrench,fuse,medkit");

    const auto repeatedSearch = execute(*scenario, "search");
    REQUIRE(!repeatedSearch.stateChanged);
    REQUIRE(scenario->saveFields().at("oxygen") == "7");

    const auto heal = execute(*scenario, "use medkit");
    REQUIRE(heal.stateChanged);
    REQUIRE(scenario->saveFields().at("hp") == "20");
    REQUIRE(scenario->saveFields().at("inventory") == "wrench,fuse");
    REQUIRE(scenario->saveFields().at("oxygen") == "6");

    const auto repeatedHeal = execute(*scenario, "use medkit");
    REQUIRE(!repeatedHeal.stateChanged);
    REQUIRE(scenario->saveFields().at("oxygen") == "6");
}

TEST_CASE("starport repair victory outranks final oxygen loss") {
    auto winner = mud::makeStarportScenario();
    auto winningFields = winner->saveFields();
    winningFields["room"] = "reactor";
    winningFields["oxygen"] = "1";
    winningFields["drone_hp"] = "0";
    winningFields["warehouse_searched"] = "1";
    winningFields["inventory"] = "wrench,fuse,medkit";
    std::string error;
    REQUIRE(winner->loadFields(winningFields, error));

    const auto repair = execute(*winner, "repair reactor");
    REQUIRE(repair.stateChanged);
    REQUIRE(winner->outcome() == mud::Outcome::Won);
    REQUIRE(winner->saveFields().at("oxygen") == "0");
    REQUIRE(winner->saveFields().at("outcome") == "won");

    auto loser = mud::makeStarportScenario();
    auto losingFields = loser->saveFields();
    losingFields["oxygen"] = "1";
    REQUIRE(loser->loadFields(losingFields, error));
    execute(*loser, "go north");
    REQUIRE(loser->outcome() == mud::Outcome::Lost);
    REQUIRE(loser->saveFields().at("oxygen") == "0");
}

TEST_CASE("starport drone can cause a health loss from a valid loaded state") {
    auto scenario = mud::makeStarportScenario();
    auto fields = scenario->saveFields();
    fields["room"] = "warehouse";
    fields["hp"] = "4";
    std::string error;
    REQUIRE(scenario->loadFields(fields, error));

    const auto attack = execute(*scenario, "attack drone");
    REQUIRE(attack.stateChanged);
    REQUIRE(scenario->outcome() == mud::Outcome::Lost);
    REQUIRE(scenario->saveFields().at("hp") == "0");
    REQUIRE(scenario->saveFields().at("drone_hp") == "6");
}

TEST_CASE("starport Chinese commands complete the same official route") {
    auto scenario = mud::makeStarportScenario();
    REQUIRE(execute(*scenario, "查看").recognized);
    REQUIRE(execute(*scenario, "状态").recognized);
    REQUIRE(execute(*scenario, "背包").recognized);

    for (const auto* command : {
             "前往 北", "移动 西", "攻击 无人机", "攻击 安保无人机", "搜索",
             "使用 医疗包", "前往 东", "移动 东", "修复 反应堆"}) {
        REQUIRE(execute(*scenario, command).recognized);
    }
    REQUIRE(scenario->outcome() == mud::Outcome::Won);
    REQUIRE(scenario->saveFields().at("oxygen") == "3");
}

TEST_CASE("starport loadFields round trips and rejects corruption atomically") {
    auto source = mud::makeStarportScenario();
    execute(*source, "go north");
    execute(*source, "go west");
    execute(*source, "attack drone");
    const auto midpoint = source->saveFields();

    auto restored = mud::makeStarportScenario();
    std::string error;
    REQUIRE(restored->loadFields(midpoint, error));
    REQUIRE(restored->saveFields() == midpoint);

    const auto unchanged = restored->saveFields();

    auto missingField = unchanged;
    missingField.erase("oxygen");
    REQUIRE(!restored->loadFields(missingField, error));
    REQUIRE(!error.empty());
    REQUIRE(restored->saveFields() == unchanged);

    auto extraField = unchanged;
    extraField["unexpected"] = "value";
    REQUIRE(!restored->loadFields(extraField, error));
    REQUIRE(restored->saveFields() == unchanged);

    auto invalidNumber = unchanged;
    invalidNumber["hp"] = "16x";
    REQUIRE(!restored->loadFields(invalidNumber, error));
    REQUIRE(restored->saveFields() == unchanged);

    auto duplicateInventory = unchanged;
    duplicateInventory["inventory"] = "wrench,wrench";
    REQUIRE(!restored->loadFields(duplicateInventory, error));
    REQUIRE(restored->saveFields() == unchanged);

    auto malformedInventory = unchanged;
    malformedInventory["inventory"] = "wrench,";
    REQUIRE(!restored->loadFields(malformedInventory, error));
    REQUIRE(restored->saveFields() == unchanged);

    auto inconsistentTask = unchanged;
    inconsistentTask["warehouse_searched"] = "1";
    inconsistentTask["inventory"] = "wrench,fuse,medkit";
    REQUIRE(!restored->loadFields(inconsistentTask, error));
    REQUIRE(restored->saveFields() == unchanged);

    auto impossibleOutcome = unchanged;
    impossibleOutcome["outcome"] = "won";
    REQUIRE(!restored->loadFields(impossibleOutcome, error));
    REQUIRE(restored->saveFields() == unchanged);
}
