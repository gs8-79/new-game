#pragma once

#include "tribe/game_engine.hpp"

#include <iosfwd>
#include <string_view>

namespace tribe {

class ExpansionGame;

enum class UiColor { Normal, Title, Food, Wood, Stone, Herbs, Friendly, Neutral, Enemy, Warning };

class ConsoleUI {
public:
    ConsoleUI(std::ostream& output, bool interactive, bool ansiEnabled);

    void renderMainMenu(std::string_view message = {});
    void renderGame(const GameEngine& engine, std::string_view message);
    void renderExpansion(const ExpansionGame& game, std::string_view message);
    void showStandalone(std::string_view title, std::string_view text);
    void prompt(std::string_view text = "请输入命令 > ");
    void clear();

    bool interactive() const { return interactive_; }

    static bool initializeTerminal();
    static bool standardStreamsAreInteractive();

private:
    void write(UiColor color, std::string_view text);

    std::ostream& output_;
    bool interactive_ = false;
    bool ansiEnabled_ = false;
};

} // namespace tribe
