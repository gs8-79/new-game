#include "command_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace tribe::command_parser {
namespace {

/// 用途：将 ASCII 字母转换为小写而保留非 ASCII 字节。输入：词元副本。输出：规范化文本；无状态修改。
/// 失败：无。不变量：不触碰 UTF-8 中文字节，调用方可安全复用中英文命令解析。
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
