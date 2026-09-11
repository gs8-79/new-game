#pragma once

#include "tribe/game_engine.hpp"

#include <chrono>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace tribe {

struct EndingPresentationOptions {
    bool animated = true;
    bool ansiEnabled = false;
    bool clearBetweenFrames = true;
    std::chrono::milliseconds frameDelay{180};
    std::function<void(std::chrono::milliseconds)> wait;
    // Return true to stop animation and render the final frame plus summary.
    // When omitted, interactive terminals use the platform's non-blocking key input.
    std::function<bool()> skipRequested;
};

class EndingPresentation {
   public:
    static std::vector<std::string> framesFor(GameEnding ending);
    static std::string renderStatic(GameEnding ending);
    static std::string formatChronicle(const std::vector<ChronicleEntry>& entries);
    static std::string formatSummary(const EndingSummary& summary);

    static void play(const EndingSummary& summary, std::ostream& output, EndingPresentationOptions options = {});
};

} // namespace tribe
