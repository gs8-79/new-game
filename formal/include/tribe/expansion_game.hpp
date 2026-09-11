#pragma once

#include "tribe/expansion_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tribe {

constexpr std::size_t kExpeditionWorldLocationCount = 16U;

enum class ExpansionPhase { Exploring = 0, Settled };

struct ExpansionCommandResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool turnAdvanced = false;
    std::string message;
    explicit operator bool() const { return success; }
};

// The campaign has one expedition format: a squad travelling on the sixteen-node map.
struct ExpansionState {
    std::uint32_t seed = 1U;
    int turn = 0;
    ExpansionPhase phase = ExpansionPhase::Exploring;
    Squad squad;
    Inventory backpack{80, 20};
    int worldLocation = 0;
    std::array<bool, kExpeditionWorldLocationCount> worldDiscovered{};
    std::array<bool, kExpeditionWorldLocationCount> outposts{};
    int cargoFood = 0;
    int cargoWood = 0;
    int cargoStone = 0;
    int cargoHerbs = 0;
    int cargoHides = 0;
    int harvestActions = 0;
    int cargoCapacity = 24;
    int foodGatherBonus = 0;
    int herbGatherBonus = 0;
    int assignedResource = 0;
    int crewSize = 2;
    int encounterLife = 0;
    bool encounterDefeated = false;
    bool settled = false;
};

class ExpansionGame {
   public:
    explicit ExpansionGame(std::uint32_t seed = 1U, std::size_t squadSize = 4U);
    explicit ExpansionGame(ExpansionState state);
    ExpansionCommandResult execute(std::string_view input);
    const ExpansionState& state() const { return state_; }
    std::string lookText() const;
    static OperationResult validateState(const ExpansionState& state);

   private:
    ExpansionCommandResult move(std::string_view target);
    ExpansionCommandResult gather(std::string_view resource);
    ExpansionCommandResult buildOutpost();
    ExpansionCommandResult settle();
    ExpansionCommandResult attackEncounter();
    ExpansionCommandResult defendEncounter();
    ExpansionCommandResult retreatEncounter();
    ExpansionCommandResult useHerb();
    ExpansionCommandResult commit(ExpansionState candidate, std::string message, bool turnAdvanced);
    ExpansionCommandResult rejected(std::string message) const;
    void recordTurn(ExpansionState& candidate, int leaderFatigue, int followerFatigue) const;
    ExpansionState state_;
};

} // namespace tribe
