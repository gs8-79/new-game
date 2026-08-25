#include "scenarios.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

mud::CommandResult issue(mud::Scenario& scenario, const std::string& text) {
    return scenario.execute(mud::parseCommand(text));
}

int integerField(const mud::Scenario& scenario, const std::string& key) {
    int value = 0;
    const auto fields = scenario.saveFields();
    REQUIRE(fields.find(key) != fields.end());
    REQUIRE(mud::parseIntStrict(fields.at(key), value));
    return value;
}

void requireAcceptedAction(const mud::CommandResult& result) {
    REQUIRE(result.recognized);
    REQUIRE(result.stateChanged);
    REQUIRE(result.consumesAction);
}

} // namespace

TEST_CASE("island official route reaches the day seven rescue") {
    auto scenario = mud::makeIslandScenario();

    requireAcceptedAction(issue(*scenario, "go jungle"));
    requireAcceptedAction(issue(*scenario, "gather vine"));
    requireAcceptedAction(issue(*scenario, "gather vine"));
    requireAcceptedAction(issue(*scenario, "gather wood"));
    REQUIRE(integerField(*scenario, "day") == 6);
    REQUIRE(integerField(*scenario, "actionPoints") == 4);

    const auto coconut = issue(*scenario, "use coconut");
    REQUIRE(coconut.recognized);
    REQUIRE(coconut.stateChanged);
    REQUIRE(!coconut.consumesAction);
    requireAcceptedAction(issue(*scenario, "craft rope"));
    requireAcceptedAction(issue(*scenario, "gather wood"));
    requireAcceptedAction(issue(*scenario, "gather wood"));
    requireAcceptedAction(issue(*scenario, "go spring"));
    REQUIRE(integerField(*scenario, "day") == 7);

    requireAcceptedAction(issue(*scenario, "collect water"));
    REQUIRE(issue(*scenario, "use water").stateChanged);
    requireAcceptedAction(issue(*scenario, "go jungle"));
    requireAcceptedAction(issue(*scenario, "go cliff"));
    const auto victory = issue(*scenario, "build signal");
    requireAcceptedAction(victory);
    REQUIRE(victory.message.find("救援船发现了你") != std::string::npos);
    REQUIRE(scenario->outcome() == mud::Outcome::Won);

    const auto fields = scenario->saveFields();
    REQUIRE(fields.at("signalBuilt") == "1");
    REQUIRE(fields.at("outcome") == "won");
    REQUIRE(integerField(*scenario, "wood") == 0);
    REQUIRE(integerField(*scenario, "cloth") == 0);
}

TEST_CASE("island cliff requires rope and rejected moves cost no action") {
    auto scenario = mud::makeIslandScenario();
    requireAcceptedAction(issue(*scenario, "go jungle"));
    const int before = integerField(*scenario, "actionPoints");

    const auto blocked = issue(*scenario, "go cliff");
    REQUIRE(blocked.recognized);
    REQUIRE(!blocked.stateChanged);
    REQUIRE(!blocked.consumesAction);
    REQUIRE(integerField(*scenario, "actionPoints") == before);
    REQUIRE(scenario->saveFields().at("location") == "jungle");
}

TEST_CASE("island night settlement advances days and applies hunger thirst damage") {
    auto scenario = mud::makeIslandScenario();

    const auto firstNight = issue(*scenario, "end");
    REQUIRE(firstNight.recognized);
    REQUIRE(firstNight.stateChanged);
    REQUIRE(integerField(*scenario, "day") == 6);
    REQUIRE(integerField(*scenario, "actionPoints") == 4);
    REQUIRE(integerField(*scenario, "hunger") == 55);
    REQUIRE(integerField(*scenario, "thirst") == 40);
    REQUIRE(integerField(*scenario, "health") == 100);

    issue(*scenario, "end");
    REQUIRE(integerField(*scenario, "day") == 7);
    REQUIRE(integerField(*scenario, "hunger") == 30);
    REQUIRE(integerField(*scenario, "thirst") == 10);

    const auto finalNight = issue(*scenario, "end");
    REQUIRE(finalNight.message.find("损失25点生命") != std::string::npos);
    REQUIRE(integerField(*scenario, "hunger") == 5);
    REQUIRE(integerField(*scenario, "thirst") == 0);
    REQUIRE(integerField(*scenario, "health") == 75);
    REQUIRE(scenario->outcome() == mud::Outcome::Lost);
}

TEST_CASE("island signal fire cannot be built before day seven") {
    auto scenario = mud::makeIslandScenario();
    auto fields = scenario->saveFields();
    fields["day"] = "6";
    fields["actionPoints"] = "4";
    fields["location"] = "cliff";
    fields["wood"] = "3";
    fields["rope"] = "1";

    std::string error;
    REQUIRE(scenario->loadFields(fields, error));
    const auto before = scenario->saveFields();
    const auto early = issue(*scenario, "build signal");
    REQUIRE(early.recognized);
    REQUIRE(!early.stateChanged);
    REQUIRE(!early.consumesAction);
    REQUIRE(early.message.find("第7天") != std::string::npos);
    REQUIRE(scenario->saveFields() == before);
    REQUIRE(scenario->outcome() == mud::Outcome::Running);
}

TEST_CASE("island accepts equivalent Chinese and English commands") {
    auto chinese = mud::makeIslandScenario();

    REQUIRE(issue(*chinese, "前往 丛林").stateChanged);
    REQUIRE(issue(*chinese, "采集 藤蔓").stateChanged);
    REQUIRE(issue(*chinese, "收集 藤蔓").stateChanged);
    REQUIRE(issue(*chinese, "采集 木材").stateChanged);
    REQUIRE(issue(*chinese, "使用 椰子").stateChanged);
    REQUIRE(issue(*chinese, "制作 绳索").stateChanged);
    REQUIRE(issue(*chinese, "采集 木材").stateChanged);
    REQUIRE(issue(*chinese, "采集 木材").stateChanged);
    REQUIRE(issue(*chinese, "前往 淡水泉").stateChanged);
    REQUIRE(issue(*chinese, "收集 水").stateChanged);
    REQUIRE(issue(*chinese, "使用 水").stateChanged);
    REQUIRE(issue(*chinese, "前往 丛林").stateChanged);
    REQUIRE(issue(*chinese, "前往 悬崖").stateChanged);
    REQUIRE(issue(*chinese, "建造 信号火").stateChanged);
    REQUIRE(chinese->outcome() == mud::Outcome::Won);
}

TEST_CASE("island loadFields rejects corrupt state atomically") {
    auto scenario = mud::makeIslandScenario();
    REQUIRE(issue(*scenario, "前往 丛林").stateChanged);
    const auto original = scenario->saveFields();

    std::string error;
    auto restored = mud::makeIslandScenario();
    REQUIRE(restored->loadFields(original, error));
    REQUIRE(restored->saveFields() == original);

    error.clear();
    auto malformed = original;
    malformed["hunger"] = "70x";
    REQUIRE(!scenario->loadFields(malformed, error));
    REQUIRE(!error.empty());
    REQUIRE(scenario->saveFields() == original);

    error.clear();
    auto inconsistent = original;
    inconsistent["location"] = "cliff";
    inconsistent["rope"] = "0";
    REQUIRE(!scenario->loadFields(inconsistent, error));
    REQUIRE(!error.empty());
    REQUIRE(scenario->saveFields() == original);

    error.clear();
    auto extraField = original;
    extraField["unexpected"] = "1";
    REQUIRE(!scenario->loadFields(extraField, error));
    REQUIRE(!error.empty());
    REQUIRE(scenario->saveFields() == original);

    error.clear();
    auto missingField = original;
    missingField.erase("water");
    REQUIRE(!scenario->loadFields(missingField, error));
    REQUIRE(!error.empty());
    REQUIRE(scenario->saveFields() == original);
}
