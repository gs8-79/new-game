#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace tribe::command_parser {

/// 用途：统一保存终端命令的动词和参数。输入：解析后的词元。输出：命令对象。
/// 状态影响：无。失败：空白输入产生空动词。不变量：仅规范化 ASCII，中文 UTF-8 字节保持原样。
struct Command {
    std::string verb;
    std::vector<std::string> args;
};

/// 用途：将任意空白分隔的命令拆分为词，并将 ASCII 字母转换为小写。
/// 输入：原始终端文本。输出：动词与参数。状态影响：无。失败：空白输入返回空动词。
/// 不变量：不修改非 ASCII 字节，避免破坏 UTF-8 中文命令。
Command parse(std::string_view input);

/// 用途：判断值是否命中一组中英文别名。输出：布尔值；无状态影响。
/// 失败：无。不变量：调用方无需重复实现别名循环，比较不修改输入。
bool equalsAny(std::string_view value, std::initializer_list<std::string_view> aliases);

/// 用途：判断命令动词是否命中别名。输出：布尔值；无状态影响。
/// 失败：空动词返回 false。不变量：参数数量仍由调用方按命令语法检查。
bool verbIs(const Command& command, std::initializer_list<std::string_view> aliases);

} // namespace tribe::command_parser
