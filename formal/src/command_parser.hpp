#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace tribe::command_parser {

// 统一保存终端命令的动词和参数；仅规范化 ASCII，中文命令保持原样。
struct Command {
    std::string verb;
    std::vector<std::string> args;
};

// 将任意空白分隔的命令拆分为词，并将 ASCII 字母转换为小写。
// 空白输入返回空动词；不修改非 ASCII 字节，避免破坏 UTF-8 中文命令。
Command parse(std::string_view input);

// 判断值是否命中一组中英文别名，调用方无需重复实现别名循环。
bool equalsAny(std::string_view value, std::initializer_list<std::string_view> aliases);

// 判断命令动词是否命中别名；参数数量由调用方按各命令语法检查。
bool verbIs(const Command& command, std::initializer_list<std::string_view> aliases);

} // namespace tribe::command_parser
