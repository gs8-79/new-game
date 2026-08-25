#include "core.hpp"
#include "scenarios.hpp"
#include "test_harness.hpp"

#include <string>

namespace {

mud::CommandResult execute(mud::Scenario& scenario, const std::string& text) {
    return scenario.execute(mud::parseCommand(text));
}

int numberField(const mud::Scenario& scenario, const std::string& key) {
    const auto fields = scenario.saveFields();
    int value = 0;
    REQUIRE(fields.find(key) != fields.end());
    REQUIRE(mud::parseIntStrict(fields.at(key), value));
    return value;
}

void endSeason(mud::Scenario& scenario) {
    const auto result = execute(scenario, "endturn");
    REQUIRE(result.recognized);
    REQUIRE(result.stateChanged);
}

mud::SaveFields validFields() {
    return mud::makeTribeScenario()->saveFields();
}

} // namespace

TEST_CASE("tribe starts with the approved deterministic state") {
    const auto scenario = mud::makeTribeScenario();
    REQUIRE(scenario->id() == "tribe");
    REQUIRE(scenario->outcome() == mud::Outcome::Running);
    REQUIRE(numberField(*scenario, "season") == 1);
    REQUIRE(numberField(*scenario, "actions_left") == 2);
    REQUIRE(numberField(*scenario, "population") == 12);
    REQUIRE(numberField(*scenario, "food") == 18);
    REQUIRE(numberField(*scenario, "wood") == 6);
    REQUIRE(numberField(*scenario, "warriors") == 2);
    REQUIRE(numberField(*scenario, "morale") == 60);
    REQUIRE(numberField(*scenario, "river_relation") == 20);
    REQUIRE(numberField(*scenario, "rockfang_strength") == 10);
}

TEST_CASE("tribe supports an official English alliance route") {
    auto scenario = mud::makeTribeScenario();
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    REQUIRE(execute(*scenario, "diplomacy riverdeer").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "diplomacy riverdeer").stateChanged);
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    REQUIRE(execute(*scenario, "gather wood").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    const auto result = execute(*scenario, "endturn");
    REQUIRE(scenario->outcome() == mud::Outcome::Won);
    REQUIRE(result.message.find("联盟之主") != std::string::npos);
}

TEST_CASE("tribe supports an official Chinese conquest route") {
    auto scenario = mud::makeTribeScenario();
    REQUIRE(execute(*scenario, "训练").stateChanged);
    REQUIRE(execute(*scenario, "训练").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "进攻 岩牙部落").stateChanged);
    REQUIRE(numberField(*scenario, "rockfang_strength") == 0);
    REQUIRE(execute(*scenario, "采集 食物").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "采集 食物").stateChanged);
    REQUIRE(execute(*scenario, "采集 食物").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "采集 食物").stateChanged);
    const auto result = execute(*scenario, "下一季");
    REQUIRE(scenario->outcome() == mud::Outcome::Won);
    REQUIRE(result.message.find("征服者") != std::string::npos);
}

TEST_CASE("tribe resource failures are atomic and do not consume actions") {
    auto scenario = mud::makeTribeScenario();
    auto fields = scenario->saveFields();
    fields["food"] = "2";
    fields["wood"] = "9";
    std::string error;
    REQUIRE(scenario->loadFields(fields, error));
    const auto before = scenario->saveFields();
    const auto trainResult = execute(*scenario, "train");
    REQUIRE(trainResult.recognized);
    REQUIRE(!trainResult.stateChanged);
    REQUIRE(scenario->saveFields() == before);
    const auto wallResult = execute(*scenario, "build wall");
    REQUIRE(wallResult.recognized);
    REQUIRE(!wallResult.stateChanged);
    REQUIRE(scenario->saveFields() == before);
}

TEST_CASE("tribe wall is unique and rejected action stays atomic") {
    auto scenario = mud::makeTribeScenario();
    REQUIRE(execute(*scenario, "gather wood").stateChanged);
    REQUIRE(execute(*scenario, "build wall").stateChanged);
    const auto built = scenario->saveFields();
    REQUIRE(built.at("wall_built") == "1");
    REQUIRE(numberField(*scenario, "defense") == 6);
    endSeason(*scenario);
    const auto beforeDuplicate = scenario->saveFields();
    const auto duplicate = execute(*scenario, "建造 木墙");
    REQUIRE(duplicate.recognized);
    REQUIRE(!duplicate.stateChanged);
    REQUIRE(scenario->saveFields() == beforeDuplicate);
}

TEST_CASE("tribe third-season raid defeats an unprepared defense") {
    auto scenario = mud::makeTribeScenario();
    endSeason(*scenario);
    endSeason(*scenario);
    const int populationBefore = numberField(*scenario, "population");
    const auto result = execute(*scenario, "endturn");
    REQUIRE(result.message.find("防线失守") != std::string::npos);
    REQUIRE(numberField(*scenario, "population") == populationBefore - 2);
}

TEST_CASE("tribe third-season raid is repelled by the wall") {
    auto scenario = mud::makeTribeScenario();
    REQUIRE(execute(*scenario, "gather wood").stateChanged);
    REQUIRE(execute(*scenario, "build wall").stateChanged);
    endSeason(*scenario);
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    endSeason(*scenario);
    const int populationBefore = numberField(*scenario, "population");
    const auto result = execute(*scenario, "结束回合");
    REQUIRE(result.message.find("袭击被击退") != std::string::npos);
    REQUIRE(numberField(*scenario, "population") == populationBefore);
}

TEST_CASE("tribe exposes all four endings with strict compatible state") {
    struct EndingCase {
        const char* ending;
        const char* population;
        const char* relation;
        const char* rockfang;
        mud::Outcome outcome;
    };
    const EndingCase cases[] = {
        {"1", "8", "70", "10", mud::Outcome::Won},
        {"2", "8", "45", "0", mud::Outcome::Won},
        {"3", "8", "45", "10", mud::Outcome::Won},
        {"4", "0", "45", "10", mud::Outcome::Lost},
    };
    for (const auto& item : cases) {
        auto scenario = mud::makeTribeScenario();
        auto fields = validFields();
        fields["season"] = "4";
        fields["population"] = item.population;
        fields["warriors"] = "0";
        fields["river_relation"] = item.relation;
        fields["rockfang_strength"] = item.rockfang;
        fields["ending"] = item.ending;
        std::string error;
        REQUIRE(scenario->loadFields(fields, error));
        REQUIRE(scenario->outcome() == item.outcome);
    }
}

TEST_CASE("tribe extinction is reached when fourth-season starvation removes the last population") {
    auto scenario = mud::makeTribeScenario();
    auto fields = scenario->saveFields();
    fields["season"] = "4";
    fields["population"] = "2";
    fields["warriors"] = "0";
    fields["food"] = "0";
    std::string error;
    REQUIRE(scenario->loadFields(fields, error));
    const auto result = execute(*scenario, "endturn");
    REQUIRE(scenario->outcome() == mud::Outcome::Lost);
    REQUIRE(result.message.find("部落覆灭") != std::string::npos);
}

TEST_CASE("tribe reaches the survival ending without alliance or conquest") {
    auto scenario = mud::makeTribeScenario();
    endSeason(*scenario);
    endSeason(*scenario);
    endSeason(*scenario);
    const auto result = execute(*scenario, "结束回合");
    REQUIRE(scenario->outcome() == mud::Outcome::Won);
    REQUIRE(result.message.find("艰难幸存") != std::string::npos);
}

TEST_CASE("tribe query and action commands work in both languages") {
    auto english = mud::makeTribeScenario();
    auto chinese = mud::makeTribeScenario();
    REQUIRE(execute(*english, "status").recognized);
    REQUIRE(execute(*chinese, "状态").recognized);
    REQUIRE(execute(*english, "map").recognized);
    REQUIRE(execute(*chinese, "地图").recognized);
    REQUIRE(execute(*english, "gather food").stateChanged);
    REQUIRE(execute(*chinese, "采集 食物").stateChanged);
    REQUIRE(english->saveFields() == chinese->saveFields());
}

TEST_CASE("tribe load is strict and atomic for missing extra malformed and inconsistent fields") {
    auto scenario = mud::makeTribeScenario();
    REQUIRE(execute(*scenario, "gather food").stateChanged);
    const auto original = scenario->saveFields();
    std::string error;

    auto missing = original;
    missing.erase("food");
    REQUIRE(!scenario->loadFields(missing, error));
    REQUIRE(scenario->saveFields() == original);

    auto extra = original;
    extra["unexpected"] = "1";
    REQUIRE(!scenario->loadFields(extra, error));
    REQUIRE(scenario->saveFields() == original);

    auto malformed = original;
    malformed["population"] = "12people";
    REQUIRE(!scenario->loadFields(malformed, error));
    REQUIRE(scenario->saveFields() == original);

    auto inconsistent = original;
    inconsistent["wall_built"] = "1";
    inconsistent["defense"] = "0";
    REQUIRE(!scenario->loadFields(inconsistent, error));
    REQUIRE(scenario->saveFields() == original);
}

TEST_CASE("tribe save fields round trip exactly") {
    auto source = mud::makeTribeScenario();
    REQUIRE(execute(*source, "采集 木材").stateChanged);
    REQUIRE(execute(*source, "建造 木墙").stateChanged);
    endSeason(*source);
    const auto fields = source->saveFields();
    auto target = mud::makeTribeScenario();
    std::string error;
    REQUIRE(target->loadFields(fields, error));
    REQUIRE(error.empty());
    REQUIRE(target->saveFields() == fields);
}
