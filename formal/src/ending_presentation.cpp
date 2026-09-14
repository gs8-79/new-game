#include "tribe/ending_presentation.hpp"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace tribe {
namespace {

// 本匿名命名空间的结局展示辅助函数只格式化帧、文本与时间：不修改游戏状态。
// 缺失回调或非交互环境必须降级为静态输出，避免 ANSI 控制序列污染重定向终端。
/// 用途：映射结局到演出标题。输入：结局枚举。输出：UTF-8 标题；无状态修改。
/// 失败：未知结局返回保守标题。不变量：不读取或修改游戏状态。
std::string endingTitle(const GameEnding ending) {
    switch (ending) {
        case GameEnding::Alliance:
            return "联盟共主";
        case GameEnding::Conquest:
            return "山河征服者";
        case GameEnding::Prosperity:
            return "燧火繁荣";
        case GameEnding::Migration:
            return "迁徙新生";
        case GameEnding::Extinction:
            return "部落覆灭";
        case GameEnding::None:
            break;
    }
    return "尚未结算";
}

/// 用途：映射结局到 ANSI 颜色代码。输入：结局枚举。输出：静态 SGR 序列；无状态修改。
/// 失败：未知结局使用默认颜色。不变量：调用方在非 ANSI 模式不得输出该序列。
const char* endingColor(const GameEnding ending) {
    switch (ending) {
        case GameEnding::Alliance:
            return "\x1b[32m";
        case GameEnding::Conquest:
            return "\x1b[31m";
        case GameEnding::Prosperity:
            return "\x1b[33m";
        case GameEnding::Migration:
            return "\x1b[36m";
        case GameEnding::Extinction:
            return "\x1b[90m";
        case GameEnding::None:
            break;
    }
    return "\x1b[37m";
}

/// 用途：为缺失展示字段提供保守文本。输入：值与回退文本。输出：非空展示文本；无状态修改。
/// 失败：无。不变量：非空 value 原样返回。
std::string valueOrFallback(const std::string& value, const char* fallback) {
    return value.empty() ? std::string{fallback} : value;
}

/// 用途：在交互终端轮询跳过动画的按键。输入：构造期开关。输出：轮询器。
/// 状态影响：Unix 上临时调整终端模式。失败：不可交互终端保持 inactive；不变量：析构时恢复已修改的终端状态。
class TerminalKeyPoller {
   public:
    /// 用途：按平台初始化非阻塞按键轮询。输入：是否启用。输出：轮询器。
    /// 状态影响：可能保存并调整终端属性。失败：初始化失败保持 inactive；不变量：不修改游戏数据。
    explicit TerminalKeyPoller(const bool enabled) {
        if (!enabled) return;
#if defined(_WIN32)
        input_ = ::GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        active_ = input_ != nullptr && input_ != INVALID_HANDLE_VALUE && ::GetConsoleMode(input_, &mode) != 0;
#else
        if (::isatty(STDIN_FILENO) == 0 || ::tcgetattr(STDIN_FILENO, &original_) != 0) return;
        termios immediate = original_;
        immediate.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        immediate.c_cc[VMIN] = 0;
        immediate.c_cc[VTIME] = 0;
        active_ = ::tcsetattr(STDIN_FILENO, TCSANOW, &immediate) == 0;
#endif
    }

    /// 用途：禁止复制终端所有权。输入：另一个轮询器。输出：无。
    /// 状态影响：无。失败：编译期删除；不变量：同一终端模式只能由一个对象负责恢复。
    TerminalKeyPoller(const TerminalKeyPoller&) = delete;
    /// 用途：禁止复制赋值终端所有权。输入：另一个轮询器。输出：无。
    /// 状态影响：无。失败：编译期删除；不变量：终端恢复责任不可转移。
    TerminalKeyPoller& operator=(const TerminalKeyPoller&) = delete;

    /// 用途：恢复构造时修改的终端属性。输入：无。输出：无。
    /// 状态影响：仅恢复平台终端模式。失败：系统恢复失败静默降级；不变量：绝不修改游戏状态。
    ~TerminalKeyPoller() {
#if !defined(_WIN32)
        if (active_) ::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
#endif
    }

    /// 用途：查询轮询器是否可用。输入：无。输出：布尔值；无状态修改。
    /// 失败：无。不变量：false 时 consumeKey 不读取终端。
    bool active() const { return active_; }

    /// 用途：消费当前可读按键。输入：无。输出：是否检测到按键。
    /// 状态影响：只推进终端输入队列。失败：不可用或读取失败返回 false；不变量：不向游戏命令流注入文本。
    bool consumeKey() const {
        if (!active_) return false;
#if defined(_WIN32)
        bool consumedKey = false;
        INPUT_RECORD record{};
        DWORD available = 0;
        while (::PeekConsoleInputW(input_, &record, 1, &available) != 0 && available > 0) {
            DWORD read = 0;
            if (::ReadConsoleInputW(input_, &record, 1, &read) == 0 || read != 1) break;
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown != FALSE) {
                consumedKey = true;
            }
        }
        return consumedKey;
#else
        unsigned char buffer[64];
        if (::read(STDIN_FILENO, buffer, sizeof(buffer)) <= 0) return false;
        while (::read(STDIN_FILENO, buffer, sizeof(buffer)) > 0) {}
        return true;
#endif
    }

   private:
    bool active_ = false;
#if defined(_WIN32)
    HANDLE input_ = INVALID_HANDLE_VALUE;
#else
    termios original_{};
#endif
};

/// 用途：执行一次可替换的帧等待。输入：选项与时长。输出：无。
/// 状态影响：可能调用测试等待回调。失败：无；不变量：无论等待方式如何都不修改游戏状态。
void waitOnce(const EndingPresentationOptions& options, const std::chrono::milliseconds duration) {
    if (options.wait) {
        options.wait(duration);
        return;
    }
    std::this_thread::sleep_for(duration);
}

/// 用途：等待下一动画帧并轮询跳过请求。输入：选项与回调。输出：是否跳过。
/// 状态影响：仅消费终端输入或调用等待回调。失败：无；不变量：零延迟不触发等待。
bool waitForNextFrame(const EndingPresentationOptions& options, const std::function<bool()>& skipRequested) {
    if (options.frameDelay.count() <= 0) return false;
    if (!skipRequested) {
        waitOnce(options, options.frameDelay);
        return false;
    }

    constexpr std::chrono::milliseconds kInputPollInterval{10};
    std::chrono::milliseconds remaining = options.frameDelay;
    while (remaining.count() > 0) {
        if (skipRequested()) return true;
        const auto interval = std::min(remaining, kInputPollInterval);
        waitOnce(options, interval);
        remaining -= interval;
    }
    return skipRequested();
}

/// 用途：向输出流写一帧结局画面。输入：摘要、帧、流和 ANSI 开关。输出：无。
/// 状态影响：仅写输出流。失败：流错误由调用方处理；不变量：ANSI 关闭时绝不附加控制序列。
void writeFrame(const EndingSummary& summary, const std::string& frame, std::ostream& output, const bool ansiEnabled) {
    if (ansiEnabled) {
        output << endingColor(summary.ending) << frame << "\x1b[0m";
    } else {
        output << frame;
    }
}

} // namespace

// 每个帧由多行相邻字符串字面量拼成一条文本，clang-tidy 会把这种写法误判成漏写逗号，故整段关闭该检查。
// NOLINTBEGIN(bugprone-suspicious-missing-comma)
std::vector<std::string> EndingPresentation::framesFor(const GameEnding ending) {
    switch (ending) {
        case GameEnding::Alliance:
            return {
                "  o                 o\n"
                " /|\\               /|\\\n"
                " / \\               / \\",
                "  o        -->      o\n"
                " /|\\               /|\\\n"
                " / \\               / \\",
                "       o       o\n"
                "      /|\\_____ /|\\\n"
                "      / \\     / \\",
                "    o-----+-----o\n"
                "   /|\\    |    /|\\\n"
                "   / \\   /\\   / \\",
                ".=====================.\n"
                "|      ALLIANCE       |\n"
                "'==o=======+=======o=='\n"
                "  /|\\     /\\     /|\\\n"
                "  / \\    /  \\    / \\"};

        case GameEnding::Conquest:
            return {
                "                 /\\\n"
                "            /\\  /  \\\n"
                "       /\\  /  \\/    \\",
                "                 /\\\n"
                "            /\\  /  \\\n"
                "       /\\  / o\\/    \\",
                "                 /\\\n"
                "            /\\  /|\\ \\\n"
                "       /\\  /  \\/ \\  \\",
                "                 |>\n"
                "                 | /\\\n"
                "            /\\  |/  \\\n"
                "       /\\  /  \\/    \\",
                "              .------.\n"
                "              |VICTORY\n"
                "              '---+--'\n"
                "            /\\   |  /\\\n"
                "       /\\  /  \\ | /  \\",
            };

        case GameEnding::Prosperity:
            return {
                "          .\n"
                "         / \\\n"
                "________/_ _\\________",
                "       \\  |  /\n"
                "        \\ | /\n"
                "_________\\|/_________",
                "   \\ | /   \\ | /\n"
                "    \\|/     \\|/\n"
                "_____|_______|_________",
                " \\|/ \\|/ \\|/ \\|/\n"
                "  |   |   |   |\n"
                "==|===|===|===|========",
                ".=======================.\n"
                "|      PROSPERITY       |\n"
                "'======================='\n"
                " \\|/ \\|/ \\|/ \\|/ \\|/\n"
                "  |   |   |   |   |"};

        case GameEnding::Migration:
            return {
                "   ______________\n"
                " _/[] [] [] [] []\\_\n"
                "(__________________)\n"
                "  O              O",
                "       ______________\n"
                "     _/[] [] [] [] []\\_\n"
                "____(__________________)___\n"
                "      O              O",
                "             ______________\n"
                "           _/[] [] [] [] []\\_\n"
                "__________(__________________)_\n"
                "            O              O",
                "                   _____________\n"
                "                 _/[] [] [] [] /\n"
                "________________(______________/__\n"
                "                  O          O",
                "                         /\\\n"
                "        NEW LAND        /  \\\n"
                "_______________________/____\\___\n"
                "             *    *    *    *"};

        case GameEnding::Extinction:
            return {
                "       (  )\n"
                "      ( /\\ )\n"
                "       /  \\\n"
                "      /____\\",
                "        ( )\n"
                "       ( /\\\n"
                "        /  \\\n"
                "       /____\\",
                "         .\n"
                "        /\\\n"
                "       /  \\\n"
                "      /____\\",
                "\n"
                "        /\\\n"
                "       /  \\\n"
                "      /____\\",
                "\n"
                "\n"
                "       _.._\n"
                "______.______.________"};

        case GameEnding::None:
            break;
    }
    return {".-----------------------.\n|    ENDING PENDING     |\n'-----------------------'"};
}
// 到此恢复该检查，避免掩盖后续代码中真实的漏写逗号。
// NOLINTEND(bugprone-suspicious-missing-comma)

std::string EndingPresentation::renderStatic(const GameEnding ending) {
    const auto frames = framesFor(ending);
    return frames.empty() ? std::string{} : frames.back();
}

std::string EndingPresentation::formatChronicle(const std::vector<ChronicleEntry>& entries) {
    if (entries.empty()) return "（暂无重要记录）";

    std::ostringstream output;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const ChronicleEntry& entry = entries[index];
        if (index != 0U) output << '\n';
        output << "[季节 " << entry.season << " | 重要度 " << entry.importance << "] "
               << valueOrFallback(entry.title, "（无标题）") << '\n'
               << "  " << valueOrFallback(entry.detail, "（无详情）");
    }
    return output.str();
}

std::string EndingPresentation::formatSummary(const EndingSummary& summary) {
    std::ostringstream output;
    const std::string title = summary.title.empty() ? endingTitle(summary.ending) : summary.title;
    output << "==============================\n"
           << "结局：" << title << '\n'
           << "后日谈：\n"
           << valueOrFallback(summary.epilogue, "（暂无后日谈）") << '\n'
           << "\n结算统计：\n";

    if (summary.statistics.empty()) {
        output << "- （暂无统计）\n";
    } else {
        for (const auto& statistic : summary.statistics) {
            output << "- " << valueOrFallback(statistic, "（空白统计）") << '\n';
        }
    }

    output << "\n其他道路：\n";
    if (summary.otherRoads.empty()) {
        output << "- （没有记录其他道路）\n";
    } else {
        for (const auto& road : summary.otherRoads) {
            output << "- " << valueOrFallback(road, "（未命名道路）") << '\n';
        }
    }

    output << "\n重要编年史：\n"
           << formatChronicle(summary.importantChronicle) << '\n'
           << "==============================";
    return output.str();
}

void EndingPresentation::play(const EndingSummary& summary, std::ostream& output, EndingPresentationOptions options) {
    const auto frames = framesFor(summary.ending);
    if (options.animated) {
        std::function<bool()> skipRequested = std::move(options.skipRequested);
        TerminalKeyPoller terminalKeys(!skipRequested);
        if (!skipRequested && terminalKeys.active()) {
            skipRequested = [&terminalKeys]() { return terminalKeys.consumeKey(); };
        }
        bool skipped = false;
        for (std::size_t index = 0; index < frames.size(); ++index) {
            if (index != 0U) {
                if (options.ansiEnabled && options.clearBetweenFrames) {
                    output << "\x1b[2J\x1b[H";
                } else {
                    output << "\n\n";
                }
            }
            writeFrame(summary, frames[index], output, options.ansiEnabled);
            output.flush();
            if (index + 1U < frames.size() && waitForNextFrame(options, skipRequested)) {
                skipped = true;
                break;
            }
        }
        if (skipped) {
            if (options.ansiEnabled && options.clearBetweenFrames) {
                output << "\x1b[2J\x1b[H";
            } else {
                output << "\n\n";
            }
            writeFrame(summary, frames.back(), output, options.ansiEnabled);
        }
    } else {
        // 静态输出同时是重定向终端的降级方案，因此不得混入 ANSI 控制序列。
        output << renderStatic(summary.ending);
    }
    output << "\n\n" << formatSummary(summary);
}

} // namespace tribe
