#include "tribe/ending_presentation.hpp"

#include <ostream>
#include <sstream>
#include <thread>

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

void waitForNextFrame(const EndingPresentationOptions& options) {
    if (options.frameDelay.count() <= 0) return;
    if (options.wait) {
        options.wait(options.frameDelay);
        return;
    }
    std::this_thread::sleep_for(options.frameDelay);
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
        for (std::size_t index = 0; index < frames.size(); ++index) {
            if (index != 0U) {
                if (options.ansiEnabled && options.clearBetweenFrames) {
                    output << "\x1b[2J\x1b[H";
                } else {
                    output << "\n\n";
                }
            }
            if (options.ansiEnabled) {
                output << endingColor(summary.ending) << frames[index] << "\x1b[0m";
            } else {
                output << frames[index];
            }
            if (index + 1U < frames.size()) waitForNextFrame(options);
        }
    } else {
        // Static output is also the redirected-terminal fallback, so it must stay free of ANSI codes.
        output << renderStatic(summary.ending);
    }
    output << "\n\n" << formatSummary(summary);
}

} // namespace tribe
