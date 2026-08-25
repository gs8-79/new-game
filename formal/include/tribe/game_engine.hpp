#pragma once

#include "tribe/battle.hpp"
#include "tribe/formal_types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

class GameEngine {
public:
    explicit GameEngine(GameConfig config = {});

    void newGame(GameConfig config);
    ActionResult execute(std::string_view input);

    const GameState& state() const { return state_; }
    bool replaceState(const GameState& candidate, std::string& error);

    std::string helpText() const;
    std::string statusText() const;
    std::string mapText() const;
    std::string objectivesText() const;
    const std::string& openingMessage() const { return openingMessage_; }

    int permanentDefense() const;
    int buildingCount() const;
    int technologyCount() const;
    std::vector<Ending> availableEndings() const;

    static bool validateState(const GameState& candidate, std::string& error);
    static std::array<int, kSeasonCount> eventScheduleForSeed(std::uint32_t seed);

private:
    ActionResult gather(std::string_view resource);
    ActionResult guardCamp();
    ActionResult scout(std::string_view target);
    ActionResult trainWarrior();
    ActionResult build(std::string_view target);
    ActionResult research(std::string_view target);
    ActionResult diplomacy(std::string_view action, std::string_view faction);
    ActionResult attack(Tactic tactic, bool raid);
    ActionResult endSeason();
    ActionResult chooseEnding(std::string_view target);

    ActionResult commitAction(GameState candidate, std::string message);
    ActionResult rejected(std::string message) const;
    bool canUseAction(ActionResult& result) const;
    void applyEvent(GameState& candidate, EventId eventId, std::string& message) const;
    void finishExtinctionIfNeeded(GameState& candidate, std::string& message) const;

    GameState state_;
    BattleSystem battles_;
    std::string openingMessage_;
};

} // namespace tribe
