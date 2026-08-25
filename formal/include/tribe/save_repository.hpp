#pragma once

#include "tribe/formal_types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace tribe {

enum class SaveSlot { Slot1 = 0, Slot2, Slot3, Autosave };

class SaveRepository {
public:
    explicit SaveRepository(std::filesystem::path root);

    bool save(const GameState& state, SaveSlot slot, std::string& error) const;
    bool load(SaveSlot slot, GameState& candidate, std::string& error) const;
    std::filesystem::path pathFor(SaveSlot slot) const;

    static std::optional<SaveSlot> parseSlot(std::string_view text);
    static std::string slotName(SaveSlot slot);

private:
    std::filesystem::path root_;
};

} // namespace tribe
