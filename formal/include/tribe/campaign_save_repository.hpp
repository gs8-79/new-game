#pragma once

#include "tribe/campaign.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace tribe {

enum class CampaignSaveSlot { Slot1 = 0, Slot2, Slot3, Slot4, Slot5, Slot6, Autosave };

class CampaignSaveRepository {
public:
    explicit CampaignSaveRepository(std::filesystem::path root);

    bool save(const CampaignState& state, CampaignSaveSlot slot, std::string& error) const;
    bool load(CampaignSaveSlot slot, CampaignState& candidate, std::string& error) const;
    std::filesystem::path pathFor(CampaignSaveSlot slot) const;

    static std::optional<CampaignSaveSlot> parseSlot(std::string_view text);
    static std::string slotName(CampaignSaveSlot slot);

private:
    std::filesystem::path root_;
};

} // namespace tribe
