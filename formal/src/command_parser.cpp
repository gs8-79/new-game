#include "command_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace tribe::command_parser {
namespace {

std::string asciiLower(std::string text) {
    for (char& character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 128U) character = static_cast<char>(std::tolower(byte));
    }
    return text;
}

} // namespace

Command parse(const std::string_view input) {
    std::istringstream stream{std::string(input)};
    Command command;
    stream >> command.verb;
    command.verb = asciiLower(std::move(command.verb));
    for (std::string argument; stream >> argument;) command.args.push_back(asciiLower(std::move(argument)));
    return command;
}

bool equalsAny(const std::string_view value, const std::initializer_list<std::string_view> aliases) {
    return std::find(aliases.begin(), aliases.end(), value) != aliases.end();
}

bool verbIs(const Command& command, const std::initializer_list<std::string_view> aliases) {
    return equalsAny(command.verb, aliases);
}

} // namespace tribe::command_parser
