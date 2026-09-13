#pragma once

#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"

#include <cstddef>
#include <iosfwd>
#include <sstream>
#include <string_view>
#include <vector>

namespace tribe {

enum class UiColor { Normal, Title, Accent, Dim, Food, Wood, Stone, Herbs, Friendly, Neutral, Enemy, Warning };

class ConsoleUI {
   public:
    /// 用途：建立终端渲染器。输入：输出流、交互/ANSI 开关和可选列宽。输出：可用 UI。
    /// 状态影响：保存渲染选项。失败：宽度为零时探测终端并保留安全下限。不变量：不修改游戏状态。
    ConsoleUI(std::ostream& output, bool interactive, bool ansiEnabled, std::size_t width = 0U);

    /// 以下渲染函数只写入输出流：输入为展示数据与消息，输出为 UTF-8 文本；不修改 GameEngine；缺失数据以提示文本降级。
    /// 用途：渲染封面主菜单。
    void renderMainMenu(std::string_view message = {});
    /// 用途：渲染模式选择菜单。
    void renderModeMenu(std::string_view message = {});
    /// 用途：渲染七槽存档菜单。
    void renderSaveMenu(const std::vector<SaveSummary>& saves, std::string_view message = {});
    /// 用途：渲染指定主题帮助页。
    void renderHelpPage(int topic = 0);
    /// 用途：渲染主游戏状态与回执。
    void renderGame(const GameEngine& game, std::string_view message);
    /// 用途：渲染独立标题和长文本。
    void showStandalone(std::string_view title, std::string_view text);
    /// 用途：显示输入提示。
    void prompt(std::string_view text = "请输入命令 > ");
    /// 用途：清屏或在非交互环境输出分隔；不影响游戏状态。
    void clear();

    /// 用途：查询构造时的交互开关。输出：布尔值；无状态修改和失败。
    bool interactive() const { return interactive_; }
    /// 用途：查询构造时的 ANSI 开关。输出：布尔值；无状态修改和失败。
    bool ansiEnabled() const { return ansiEnabled_; }
    /// 用途：查询当前渲染列宽。输出：至少为二的列数；无状态修改和失败。
    std::size_t width() const { return width_; }

    /// 用途：将 Windows 终端输入输出切换至 UTF-8。输出：是否成功；失败不改变游戏规则。
    static bool initializeTerminal();
    /// 用途：判断标准流是否可交互。输出：布尔值；无状态修改和失败。
    static bool standardStreamsAreInteractive();
    /// 用途：探测终端列宽。输出：列数；失败时调用方使用安全下限。
    static std::size_t detectTerminalWidth();

   private:
    /// 用途：按颜色写入文本。状态影响：仅输出流；ANSI 关闭时不得输出控制序列。
    void write(UiColor color, std::string_view text);
    /// 用途：按显示列宽居中写入文本。输出：一行文本；不拆分 UTF-8 字形。
    void writeCentered(UiColor color, std::string_view text);
    /// 用途：写入横向分隔线。输出：一行填充字符；无游戏状态修改。
    void writeRule(char fill = '-');
    /// 用途：写入分节标题。输出：带分隔线的文本；无游戏状态修改。
    void writeSection(std::string_view title);
    /// 用途：渲染活动地图任务。输出：任务视图；无游戏状态修改。
    void renderMission(const GameEngine& game, std::string_view message);
    /// 用途：按终端宽度刷新分页输出。状态影响：可能更新自动宽度；失败时保持安全宽度。
    void flushPage();

    std::ostream& destination_;
    std::ostringstream output_;
    bool interactive_ = false;
    bool ansiEnabled_ = false;
    std::size_t width_ = 80U;
    bool automaticWidth_ = true;
};

} // namespace tribe
