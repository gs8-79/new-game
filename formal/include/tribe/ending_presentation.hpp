#pragma once

#include "tribe/campaign.hpp"

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
};

class EndingPresentation {
public:
    static std::vector<std::string> framesFor(CampaignEnding ending);
    static std::string renderStatic(CampaignEnding ending);
    static std::string formatChronicle(const std::vector<ChronicleEntry>& entries);
    static std::string formatSummary(const EndingSummary& summary);

    static void play(const EndingSummary& summary, std::ostream& output,
        EndingPresentationOptions options = {});
};

} // namespace tribe
