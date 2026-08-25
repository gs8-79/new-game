#include "core.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mud {
namespace {

std::string asciiLower(std::string text) {
    for (char& character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 128U) {
            character = static_cast<char>(std::tolower(byte));
        }
    }
    return text;
}

bool containsLineBreak(std::string_view text) {
    return text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos;
}

std::string commonHelp() {
    return
        "公共命令：\n"
        "  help / 帮助       查看帮助\n"
        "  save / 保存       保存当前 Demo\n"
        "  load / 读取       读取当前 Demo\n"
        "  back / 返回       返回选题菜单\n"
        "  quit / 退出       退出程序\n";
}

} // namespace

ParsedCommand parseCommand(std::string_view input) {
    std::istringstream stream{std::string(input)};
    ParsedCommand command;
    if (!(stream >> command.verb)) {
        return command;
    }

    command.verb = asciiLower(std::move(command.verb));
    std::string argument;
    while (stream >> argument) {
        command.args.push_back(asciiLower(std::move(argument)));
    }
    return command;
}

bool commandIs(const ParsedCommand& command, std::initializer_list<std::string_view> aliases) {
    return std::any_of(aliases.begin(), aliases.end(), [&](const std::string_view alias) {
        return command.verb == alias;
    });
}

std::string argumentAt(const ParsedCommand& command, const std::size_t index) {
    return index < command.args.size() ? command.args[index] : std::string{};
}

bool parseIntStrict(const std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parseBoolStrict(const std::string_view text, bool& value) {
    if (text == "1") {
        value = true;
        return true;
    }
    if (text == "0") {
        value = false;
        return true;
    }
    return false;
}

bool hasExactKeys(
    const SaveFields& fields,
    const std::initializer_list<std::string_view> keys,
    std::string& error) {
    std::set<std::string> expected;
    for (const auto key : keys) {
        expected.emplace(key);
    }

    if (fields.size() != expected.size()) {
        error = "存档字段数量不正确。";
        return false;
    }

    for (const auto& key : expected) {
        if (fields.find(key) == fields.end()) {
            error = "存档缺少字段：" + key;
            return false;
        }
    }
    return true;
}

bool saveScenario(const Scenario& scenario, const std::filesystem::path& path, std::string& error) {
    SaveFields fields = scenario.saveFields();
    if (fields.find("version") != fields.end() || fields.find("scenario") != fields.end()) {
        error = "场景存档字段与公共字段冲突。";
        return false;
    }

    fields.emplace("version", "1");
    fields.emplace("scenario", scenario.id());

    for (const auto& [key, value] : fields) {
        if (key.empty() || key.find('=') != std::string::npos || containsLineBreak(key) || containsLineBreak(value)) {
            error = "存档包含无法写入的字段。";
            return false;
        }
    }

    std::error_code directoryError;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            error = "无法创建存档目录：" + directoryError.message();
            return false;
        }
    }

    std::vector<std::pair<std::string, std::string>> ordered(fields.begin(), fields.end());
    std::sort(ordered.begin(), ordered.end());

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "无法打开存档文件进行写入。";
        return false;
    }
    for (const auto& [key, value] : ordered) {
        output << key << '=' << value << '\n';
    }
    if (!output) {
        error = "写入存档时发生错误。";
        return false;
    }
    return true;
}

bool loadScenario(Scenario& scenario, const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "找不到或无法读取存档文件。";
        return false;
    }

    SaveFields fields;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0U) {
            error = "存档格式损坏。";
            return false;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1U);
        if (!fields.emplace(key, value).second) {
            error = "存档包含重复字段：" + key;
            return false;
        }
    }
    if (!input.eof()) {
        error = "读取存档时发生错误。";
        return false;
    }

    const auto version = fields.find("version");
    const auto scenarioId = fields.find("scenario");
    if (version == fields.end() || version->second != "1") {
        error = "不支持的存档版本。";
        return false;
    }
    if (scenarioId == fields.end() || scenarioId->second != scenario.id()) {
        error = "存档不属于当前 Demo。";
        return false;
    }

    fields.erase("version");
    fields.erase("scenario");
    return scenario.loadFields(fields, error);
}

RunnerExit runScenario(
    Scenario& scenario,
    std::istream& input,
    std::ostream& output,
    const std::filesystem::path& savePath) {
    output << "\n=== " << scenario.title() << " ===\n";
    output << scenario.intro() << "\n输入 help 或 帮助 查看命令。\n";

    std::string line;
    while (scenario.outcome() == Outcome::Running) {
        output << "\n> ";
        if (!std::getline(input, line)) {
            output << "\n输入结束，退出程序。\n";
            return RunnerExit::QuitApplication;
        }

        const ParsedCommand command = parseCommand(line);
        if (command.verb.empty()) {
            output << "请输入命令。\n";
            continue;
        }
        if (commandIs(command, {"help", "帮助"})) {
            output << scenario.help() << '\n' << commonHelp();
            continue;
        }
        if (commandIs(command, {"save", "保存"})) {
            std::string error;
            output << (saveScenario(scenario, savePath, error) ? "保存成功。" : "保存失败：" + error) << '\n';
            continue;
        }
        if (commandIs(command, {"load", "读取"})) {
            std::string error;
            output << (loadScenario(scenario, savePath, error) ? "读取成功。" : "读取失败：" + error) << '\n';
            continue;
        }
        if (commandIs(command, {"back", "返回"})) {
            output << "返回选题菜单。\n";
            return RunnerExit::BackToMenu;
        }
        if (commandIs(command, {"quit", "退出"})) {
            output << "退出程序。\n";
            return RunnerExit::QuitApplication;
        }

        const CommandResult result = scenario.execute(command);
        output << (result.recognized ? result.message : "无法识别该命令，请输入 help 或 帮助。") << '\n';
    }

    if (scenario.outcome() == Outcome::Won) {
        output << "\n[成功] Demo 已完成，返回选题菜单。\n";
    } else {
        output << "\n[失败] 本次尝试结束，返回选题菜单。\n";
    }
    return RunnerExit::Completed;
}

void enableUtf8Console() {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace mud

