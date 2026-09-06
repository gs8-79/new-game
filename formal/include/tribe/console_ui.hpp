#pragma once

#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"

#include <cstddef>
#include <iosfwd>
#include <sstream>
#include <string_view>
#include <vector>

namespace tribe {

enum class UiColor {
    Normal,
    Title,
    Accent,
    Dim,
    Food,
    Wood,
    Stone,
    Herbs,
    Friendly,
    Neutral,
    Enemy,
    Warning
};

class ConsoleUI {
  public:
    ConsoleUI(std::ostream &output, bool interactive, bool ansiEnabled, std::size_t width = 0U);

    void renderMainMenu(std::string_view message = {});
    void renderModeMenu(std::string_view message = {});
    void renderSaveMenu(const std::vector<SaveSummary> &saves, std::string_view message = {});
    void renderHelpPage(int topic = 0);
    void renderGame(const GameEngine &game, std::string_view message);
    void showStandalone(std::string_view title, std::string_view text);
    void prompt(std::string_view text = "请输入命令 > ");
    void clear();

    bool interactive() const { return interactive_; }
    bool ansiEnabled() const { return ansiEnabled_; }
    std::size_t width() const { return width_; }

    static bool initializeTerminal();
    static bool standardStreamsAreInteractive();
    static std::size_t detectTerminalWidth();

  private:
    void write(UiColor color, std::string_view text);
    void writeCentered(UiColor color, std::string_view text);
    void writeRule(char fill = '-');
    void writeSection(std::string_view title);
    void renderMission(const GameEngine &game, std::string_view message);
    void flushPage();

    std::ostream &destination_;
    std::ostringstream output_;
    bool interactive_ = false;
    bool ansiEnabled_ = false;
    std::size_t width_ = 80U;
    bool automaticWidth_ = true;
};

} // namespace tribe
