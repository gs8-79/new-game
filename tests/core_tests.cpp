#include "core.hpp"
#include "scenarios.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("parser normalizes ASCII and whitespace") {
    const auto command = mud::parseCommand("  Go    NoRtH  ");
    REQUIRE(command.verb == "go");
    REQUIRE(command.args.size() == 1U);
    REQUIRE(command.args[0] == "north");
}

TEST_CASE("parser preserves Chinese tokens") {
    const auto command = mud::parseCommand("  前往   北  ");
    REQUIRE(command.verb == "前往");
    REQUIRE(command.args.size() == 1U);
    REQUIRE(command.args[0] == "北");
}

TEST_CASE("strict scalar parsing rejects partial values") {
    int number = 0;
    bool flag = false;
    REQUIRE(mud::parseIntStrict("42", number));
    REQUIRE(number == 42);
    REQUIRE(!mud::parseIntStrict("42x", number));
    REQUIRE(mud::parseBoolStrict("1", flag));
    REQUIRE(flag);
    REQUIRE(!mud::parseBoolStrict("true", flag));
}

TEST_CASE("styled output remains plain when redirected") {
    std::ostringstream output;
    mud::writeStyled(output, mud::ConsoleStyle::Success, "完成");
    REQUIRE(output.str() == "完成");
}

TEST_CASE("menu accepts numeric English and Chinese selections") {
    REQUIRE(mud::makeScenarioForMenu(mud::parseCommand("1"))->id() == "starport");
    REQUIRE(mud::makeScenarioForMenu(mud::parseCommand("island"))->id() == "island");
    REQUIRE(mud::makeScenarioForMenu(mud::parseCommand("部落黎明"))->id() == "tribe");
    REQUIRE(!mud::makeScenarioForMenu(mud::parseCommand("unknown")));
    REQUIRE(!mud::makeScenarioForMenu(mud::parseCommand("1 extra")));
}

TEST_CASE("file save and load round trip preserves scenario state") {
    const auto savePath = std::filesystem::current_path() / "core-round-trip.sav";
    std::error_code ignored;
    std::filesystem::remove(savePath, ignored);

    auto source = mud::makeStarportScenario();
    REQUIRE(source->execute(mud::parseCommand("go north")).stateChanged);
    const auto expected = source->saveFields();

    std::string error;
    REQUIRE(mud::saveScenario(*source, savePath, error));

    auto restored = mud::makeStarportScenario();
    REQUIRE(mud::loadScenario(*restored, savePath, error));
    REQUIRE(restored->saveFields() == expected);

    std::filesystem::remove(savePath, ignored);
}

TEST_CASE("file loading rejects wrong scenario and version atomically") {
    const auto savePath = std::filesystem::current_path() / "core-invalid.sav";
    std::error_code ignored;
    std::filesystem::remove(savePath, ignored);

    auto starport = mud::makeStarportScenario();
    std::string error;
    REQUIRE(mud::saveScenario(*starport, savePath, error));

    auto island = mud::makeIslandScenario();
    const auto islandBefore = island->saveFields();
    REQUIRE(!mud::loadScenario(*island, savePath, error));
    REQUIRE(!error.empty());
    REQUIRE(island->saveFields() == islandBefore);

    {
        std::ofstream damaged(savePath, std::ios::trunc);
        REQUIRE(static_cast<bool>(damaged));
        damaged << "scenario=starport\nversion=99\n";
    }
    const auto starportBefore = starport->saveFields();
    REQUIRE(!mud::loadScenario(*starport, savePath, error));
    REQUIRE(!error.empty());
    REQUIRE(starport->saveFields() == starportBefore);

    std::filesystem::remove(savePath, ignored);
}

TEST_CASE("saving to a directory returns a visible error") {
    auto scenario = mud::makeStarportScenario();
    std::string error;
    REQUIRE(!mud::saveScenario(*scenario, std::filesystem::current_path(), error));
    REQUIRE(!error.empty());
}
