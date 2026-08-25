#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tribe {

enum class GameMode { Standard = 0, Quick = 1 };
enum class Season { Spring = 0, Summer = 1, Autumn = 2, Winter = 3 };
enum class Phase { Playing = 0, AwaitingRaid = 1, FinalChoice = 2, Finished = 3 };

enum class LocationId {
    Camp = 0,
    Forest,
    RedPlain,
    Marsh,
    RiverFord,
    WhiteFeatherCamp,
    Quarry,
    OldPass,
    RockfangFort,
    Count
};

enum class BuildingId {
    Granary = 0,
    Wall,
    Workshop,
    HealerHut,
    Watchtower,
    CouncilFire,
    Count
};

enum class TechnologyId {
    FoodPreservation = 0,
    HerbalKnowledge,
    Irrigation,
    FlintSpear,
    ShieldWall,
    AmbushTraining,
    GiftCustoms,
    SharedLanguage,
    Confederation,
    Count
};

enum class FactionId { RiverDeer = 0, WhiteFeather, Rockfang, Count };
enum class Tactic { Assault = 0, Ambush, Defend, Retreat };
enum class Ending { None = 0, Alliance, Conquest, Prosperity, Migration, Extinction };

enum class EventId {
    GentleSpring = 0,
    RichHunt,
    Drought,
    Flood,
    Sickness,
    Refugees,
    Traders,
    ForestFire,
    Predators,
    Dispute,
    HerbBloom,
    ColdSnap,
    Craftspeople,
    RockfangScouts,
    Newborns,
    Festival,
    LostHunters,
    ClearSky,
    RiverEnvoys,
    WhiteFeatherSign,
    RockfangRaid,
    FinalCouncil,
    Count
};

constexpr std::size_t kLocationCount = static_cast<std::size_t>(LocationId::Count);
constexpr std::size_t kBuildingCount = static_cast<std::size_t>(BuildingId::Count);
constexpr std::size_t kTechnologyCount = static_cast<std::size_t>(TechnologyId::Count);
constexpr std::size_t kFactionCount = static_cast<std::size_t>(FactionId::Count);
constexpr std::size_t kEventCount = static_cast<std::size_t>(EventId::Count);
constexpr std::size_t kSeasonCount = 16U;

template <typename Enum>
constexpr std::size_t indexOf(const Enum value) {
    return static_cast<std::size_t>(value);
}

struct GameConfig {
    GameMode mode = GameMode::Standard;
    std::uint32_t seed = 1U;
};

struct GameState {
    GameMode mode = GameMode::Standard;
    std::uint32_t seed = 1U;
    int turn = 1;
    int actionsLeft = 3;
    int population = 16;
    int food = 30;
    int wood = 12;
    int stone = 4;
    int herbs = 3;
    int warriors = 3;
    int morale = 60;
    int campDurability = 20;
    int temporaryDefense = 0;
    int rockfangStrength = 14;
    Phase phase = Phase::Playing;
    Ending ending = Ending::None;
    bool pendingRaid = false;
    bool rockfangTruce = false;
    bool rockfangFortCaptured = false;
    std::array<bool, kLocationCount> discovered{};
    std::array<bool, kLocationCount> scouted{};
    std::array<bool, kBuildingCount> buildings{};
    std::array<bool, kTechnologyCount> technologies{};
    std::array<int, kFactionCount> relations{{20, 0, -40}};
    std::array<int, kFactionCount> quests{{0, 0, 0}};
    std::array<int, kSeasonCount> eventSchedule{};
    int currentEvent = 0;
};

struct ActionResult {
    bool recognized = false;
    bool stateChanged = false;
    bool consumesAction = false;
    bool seasonAdvanced = false;
    std::string message;
};

bool operator==(const GameState& left, const GameState& right);
bool operator!=(const GameState& left, const GameState& right);

Season seasonForTurn(int turn);
int yearForTurn(int turn);
int teamsForPopulation(int population);
std::string modeName(GameMode mode);
std::string seasonName(Season season);
std::string phaseName(Phase phase);
std::string endingName(Ending ending);

} // namespace tribe
