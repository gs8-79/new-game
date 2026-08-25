#pragma once

#include <filesystem>
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mud {

enum class Outcome { Running, Won, Lost };

struct ParsedCommand {
    std::string verb;
    std::vector<std::string> args;
};

struct CommandResult {
    bool recognized = false;
    bool stateChanged = false;
    bool consumesAction = false;
    std::string message;
};

using SaveFields = std::unordered_map<std::string, std::string>;

class Scenario {
public:
    virtual ~Scenario() = default;
    virtual std::string id() const = 0;
    virtual std::string title() const = 0;
    virtual std::string intro() const = 0;
    virtual std::string help() const = 0;
    virtual void reset() = 0;
    virtual CommandResult execute(const ParsedCommand& command) = 0;
    virtual Outcome outcome() const = 0;
    virtual SaveFields saveFields() const = 0;
    virtual bool loadFields(const SaveFields& fields, std::string& error) = 0;
};

enum class RunnerExit { BackToMenu, QuitApplication, Completed };
enum class ConsoleStyle { Normal, Title, Success, Warning, Error };

ParsedCommand parseCommand(std::string_view input);
bool commandIs(const ParsedCommand& command, std::initializer_list<std::string_view> aliases);
std::string argumentAt(const ParsedCommand& command, std::size_t index);

bool parseIntStrict(std::string_view text, int& value);
bool parseBoolStrict(std::string_view text, bool& value);
bool hasExactKeys(const SaveFields& fields, std::initializer_list<std::string_view> keys, std::string& error);

bool saveScenario(const Scenario& scenario, const std::filesystem::path& path, std::string& error);
bool loadScenario(Scenario& scenario, const std::filesystem::path& path, std::string& error);

RunnerExit runScenario(
    Scenario& scenario,
    std::istream& input,
    std::ostream& output,
    const std::filesystem::path& savePath);

void enableUtf8Console();
void writeStyled(std::ostream& output, ConsoleStyle style, std::string_view text);

} // namespace mud
