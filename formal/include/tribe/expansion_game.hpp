#pragma once

#include "tribe/expansion_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tribe {

enum class ExpansionPhase {
    CampPreparation = 0,
    ForestExploration,
    ForeignEncounter,
    FrontlineCombat,
    ReturnSettlement
};

enum class ExpansionLocation {
    Camp = 0,
    ForestEdge,
    DeepForest,
    HuntingGround,
    StrangerClearing
};

enum class ForeignStance { Unknown = 0, Neutral, Peaceful, Trading, Hostile, Defeated };
enum class SquadOrder { Follow = 0, Advance, Hold, Focus, Withdraw };

struct ExpansionCommandResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool turnAdvanced = false;
    std::string message;

    explicit operator bool() const { return success; }
};

struct ExpansionState {
    std::uint32_t seed = 1U;
    int turn = 0;
    ExpansionPhase phase = ExpansionPhase::CampPreparation;
    ExpansionLocation location = ExpansionLocation::Camp;
    ForeignStance foreignStance = ForeignStance::Unknown;
    SquadOrder order = SquadOrder::Follow;
    Squad squad;
    Inventory inventory{80, 20};
    int supplies = 12;
    int herbs = 0;
    int hides = 0;
    int medicine = 1;
    int tradeGoods = 0;
    int frontline = 0;
    int enemyLife = 0;
    int enemySpeed = 0;
    int leaderActions = 0;
    int followerActions = 0;
    int dodges = 0;
    int lastPlayerInitiative = 0;
    int lastEnemyInitiative = 0;
    bool traded = false;
    bool battleWon = false;
    bool retreated = false;
    bool missionFailed = false;
    bool lootAvailable = false;
    bool settled = false;
};

class ExpansionGame {
public:
    explicit ExpansionGame(std::uint32_t seed = 1U, std::size_t squadSize = 4U);
    explicit ExpansionGame(ExpansionState state);

    ExpansionCommandResult execute(std::string_view input);
    const ExpansionState& state() const { return state_; }

    std::string lookText() const;
    std::string stateFingerprint() const;

    static OperationResult validateState(const ExpansionState& state);
    static std::string phaseName(ExpansionPhase phase);
    static std::string locationName(ExpansionLocation location);
    static std::string orderName(SquadOrder order);

private:
    ExpansionCommandResult move(std::string_view target);
    ExpansionCommandResult gather(std::string_view resource);
    ExpansionCommandResult talk();
    ExpansionCommandResult trade();
    ExpansionCommandResult raid();
    ExpansionCommandResult attack();
    ExpansionCommandResult defend();
    ExpansionCommandResult order(std::string_view value);
    ExpansionCommandResult use(std::string_view item);
    ExpansionCommandResult loot();
    ExpansionCommandResult retreat();
    ExpansionCommandResult returnToCamp();
    ExpansionCommandResult equip(std::string_view slot, std::string_view itemId);

    ExpansionCommandResult commit(ExpansionState candidate, std::string message, bool turnAdvanced);
    ExpansionCommandResult rejected(std::string message) const;
    void recordSquadTurn(ExpansionState& candidate, int leaderFatigue, int followerFatigue) const;
    void enemyResponse(ExpansionState& candidate, bool defending) const;
    bool finishIfLeaderFallen(ExpansionState& candidate, std::string& message) const;
    void advanceFrontline(ExpansionState& candidate, std::string& message) const;
    int squadSpeed(const ExpansionState& state) const;
    int attackPower(const ExpansionState& state) const;
    int frontlineLife(int frontline) const;
    Item victoryLoot() const;

    ExpansionState state_;
};

} // namespace tribe
