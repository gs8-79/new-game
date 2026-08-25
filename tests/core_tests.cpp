#include "core.hpp"
#include "test_harness.hpp"

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

