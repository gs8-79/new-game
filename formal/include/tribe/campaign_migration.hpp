#pragma once

#include "tribe/campaign.hpp"
#include "tribe/formal_types.hpp"

#include <filesystem>
#include <string>

namespace tribe {

class CampaignMigration {
public:
    static bool loadLegacyReadOnly(
        const std::filesystem::path& primaryPath, GameState& candidate, std::string& error);

    static bool convert(
        const GameState& legacy, CampaignState& candidate, std::string& error);

    static bool loadAndConvertReadOnly(
        const std::filesystem::path& primaryPath, CampaignState& candidate, std::string& error);
};

} // namespace tribe
