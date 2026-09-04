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

std::string endingTitle(const CampaignEnding ending) {
    switch (ending) {
    case CampaignEnding::Alliance: return "联盟共主";
    case CampaignEnding::Conquest: return "山河征服者";
    case CampaignEnding::Prosperity: return "燧火繁荣";
    case CampaignEnding::Migration: return "迁徙新生";
    case CampaignEnding::Extinction: return "部落覆灭";
    case CampaignEnding::None: break;
    }
    return "尚未结算";
}

const char* endingColor(const CampaignEnding ending) {
    switch (ending) {
    case CampaignEnding::Alliance: return "\x1b[32m";
    case CampaignEnding::Conquest: return "\x1b[31m";
    case CampaignEnding::Prosperity: return "\x1b[33m";
    case CampaignEnding::Migration: return "\x1b[36m";
    case CampaignEnding::Extinction: return "\x1b[90m";
    case CampaignEnding::None: break;
    }
    return "\x1b[37m";
}

std::string valueOrFallback(const std::string& value, const char* fallback) {
    return value.empty() ? std::string{fallback} : value;
}

class TerminalKeyPoller {
public:
    explicit TerminalKeyPoller(const bool enabled) {
        if (!enabled) return;
#if defined(_WIN32)
        input_ = ::GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        active_ = input_ != nullptr && input_ != INVALID_HANDLE_VALUE
            && ::GetConsoleMode(input_, &mode) != 0;
#else
        if (::isatty(STDIN_FILENO) == 0 || ::tcgetattr(STDIN_FILENO, &original_) != 0) return;
        termios immediate = original_;
        immediate.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        immediate.c_cc[VMIN] = 0;
        immediate.c_cc[VTIME] = 0;
        active_ = ::tcsetattr(STDIN_FILENO, TCSANOW, &immediate) == 0;
#endif
    }

    TerminalKeyPoller(const TerminalKeyPoller&) = delete;
    TerminalKeyPoller& operator=(const TerminalKeyPoller&) = delete;

    ~TerminalKeyPoller() {
#if !defined(_WIN32)
        if (active_) ::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
#endif
    }

    bool active() const { return active_; }

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

void waitOnce(const EndingPresentationOptions& options, const std::chrono::milliseconds duration) {
    if (options.wait) {
        options.wait(duration);
        return;
    }
    std::this_thread::sleep_for(duration);
}

bool waitForNextFrame(const EndingPresentationOptions& options,
    const std::function<bool()>& skipRequested) {
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

void writeFrame(const EndingSummary& summary, const std::string& frame, std::ostream& output,
    const bool ansiEnabled) {
    if (ansiEnabled) {
        output << endingColor(summary.ending) << frame << "\x1b[0m";
    } else {
        output << frame;
    }
}

} // namespace

std::vector<std::string> EndingPresentation::framesFor(const CampaignEnding ending) {
    switch (ending) {
    case CampaignEnding::Alliance:
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

    case CampaignEnding::Conquest:
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

    case CampaignEnding::Prosperity:
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

    case CampaignEnding::Migration:
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

    case CampaignEnding::Extinction:
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

    case CampaignEnding::None:
        break;
    }
    return {".-----------------------.\n|    ENDING PENDING     |\n'-----------------------'"};
}

std::string EndingPresentation::renderStatic(const CampaignEnding ending) {
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

    output << "\n重要编年史：\n" << formatChronicle(summary.importantChronicle) << '\n'
           << "==============================";
    return output.str();
}

void EndingPresentation::play(const EndingSummary& summary, std::ostream& output,
    EndingPresentationOptions options) {
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
        // Static output is also the redirected-terminal fallback, so it must stay free of ANSI codes.
        output << renderStatic(summary.ending);
    }
    output << "\n\n" << formatSummary(summary);
}

} // namespace tribe
