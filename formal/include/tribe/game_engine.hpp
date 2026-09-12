#pragma once

#include "tribe/expansion_game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

enum class GameMode { Quick = 0, Standard, Long };
enum class GamePhase { Managing = 0, Mission, War, EndingChoice, Finished, Sandbox };
enum class GameEnding { None = 0, Alliance, Conquest, Prosperity, Migration, Extinction };
enum class WorkforceRole { FoodCrew = 0, WoodCrew, StoneCrew, HerbCrew, Crafters, Healers, Scouts, Envoys, CampGuards };

enum class BuildingId { Granary = 0, Wall, Workshop, HealerHut, Watchtower, CouncilFire, Count };

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

enum class WorldLocationId {
    Camp = 0,
    Forest,
    RedPlain,
    Marsh,
    RiverFord,
    WhiteFeatherCamp,
    Quarry,
    OldPass,
    RockfangFort,
    SaltwindCoast,
    TidesaltHarbor,
    ShellBeach,
    BlackstoneValley,
    BlackstoneWorkshop,
    MountainMarket,
    CliffTradeRoad,
    Count
};

enum class TribeId { Player = 0, RiverDeer, WhiteFeather, Rockfang, Tidesalt, Blackstone, Count };
enum class FactionCrisis { Calm = 0, Complaint, Slowdown, Refusal, Deposition, Coup };
enum class WarOrder { Advance = 0, Hold, Focus, Flank, Cover, Retreat };
enum class LocationRole { Camp = 0, Resource, Diplomacy, Route, War };
enum class PendingEventKind { Refugees = 0, Disease, Extortion, FactionDemand };

constexpr std::size_t kWorldLocationCount = static_cast<std::size_t>(WorldLocationId::Count);
constexpr std::size_t kTribeCount = static_cast<std::size_t>(TribeId::Count);
constexpr std::size_t kBuildingCount = static_cast<std::size_t>(BuildingId::Count);
constexpr std::size_t kTechnologyCount = static_cast<std::size_t>(TechnologyId::Count);
constexpr std::size_t kPlayerFactionCount = 3U;
constexpr int kSaveVersion = 5;

template <typename Enum>
constexpr std::size_t indexOf(const Enum value) {
    return static_cast<std::size_t>(value);
}

struct GameConfig {
    GameMode mode = GameMode::Standard;
    std::uint32_t seed = 1U;
    std::string tribeName = "燧火";
    std::string leaderName = "炎角";
    std::string leaderFocus = "生存";
};

struct WorldLocationInfo {
    WorldLocationId id = WorldLocationId::Camp;
    std::string name;
    std::string feature;
    LocationRole role = LocationRole::Route;
    std::vector<WorldLocationId> neighbors;
};

struct FactionState {
    std::string name;
    int influence = 30;
    int satisfaction = 60;
    std::string demand;
    std::string candidate;
    FactionCrisis crisis = FactionCrisis::Calm;
};

struct TribeProfile {
    TribeId id = TribeId::Player;
    std::string name;
    std::string leader;
    std::string actingLeader;
    std::string successor;
    std::string personality;
    std::vector<FactionState> factions;
};

struct DiplomacyRelation {
    int relation = 0;
    int trust = 0;
    int fear = 0;
    int tradeDependence = 0;
    bool atWar = false;
    bool truce = false;
    bool alliance = false;
    bool marriage = false;
    bool playerPaysTribute = false;
    bool otherPaysTribute = false;
    bool tradeRoute = false;
};

struct PermanentSquad {
    std::string name;
    std::string captain;
    std::vector<std::string> members;
    int fatigue = 0;
    int eliteExperience = 0;
    bool personallyDeployedThisSeason = false;
    bool refusingOrders = false;
    WorldLocationId station = WorldLocationId::Camp;
};

struct WarState {
    bool active = false;
    TribeId enemy = TribeId::Rockfang;
    std::string commander;
    int warriors = 0;
    int militia = 0;
    int playerPower = 0;
    int enemyPower = 0;
    WarOrder order = WarOrder::Hold;
    bool riskConfirmed = false;
    int spearMilitia = 0;
    int shieldBearers = 0;
    int heavySpears = 0;
    int craftsmanshipPower = 0;
    std::vector<Item> lockedEquipment;
    bool defensive = false;
};

struct WorkforceState {
    int foodCrew = 2;
    int woodCrew = 0;
    int stoneCrew = 0;
    int herbCrew = 0;
    int crafters = 0;
    int healers = 0;
    int scouts = 0;
    int envoys = 0;
    int campGuards = 0;
    std::array<int, kWorldLocationCount> outpostGuards{};
    std::array<int, kWorldLocationCount> outpostIdleSeasons{};
};

struct OccupationState {
    bool occupied = false;
    int garrison = 0;
    int unrest = 0;
};

struct PendingEvent {
    PendingEventKind kind = PendingEventKind::Refugees;
    bool active = false;
};

struct ChronicleEntry {
    int season = 0;
    int importance = 1;
    std::string title;
    std::string detail;
};

struct GameState {
    GameMode mode = GameMode::Standard;
    GamePhase phase = GamePhase::Managing;
    std::uint32_t seed = 1U;
    int season = 1;
    int seasonLimit = 16;
    int actionsLeft = 3;
    int population = 16;
    int food = 30;
    int wood = 12;
    int stone = 4;
    int herbs = 3;
    int hides = 0;
    int warriors = 3;
    int morale = 60;
    int campDurability = 20;
    int stability = 65;
    int tradeCount = 0;
    int warsWon = 0;
    int warsLost = 0;
    int missionCount = 0;
    int missionDeaths = 0;
    int highestLevel = 1;
    std::string tribeName = "燧火";
    std::string leaderName = "炎角";
    std::string actingLeaderName;
    std::string leaderFocus = "生存";
    std::array<bool, kWorldLocationCount> discovered{};
    std::array<bool, kBuildingCount> buildings{};
    std::array<bool, kTechnologyCount> technologies{};
    std::array<TribeProfile, kTribeCount> tribes{};
    std::array<DiplomacyRelation, kTribeCount> relations{};
    std::array<FactionState, kPlayerFactionCount> playerFactions{};
    std::array<bool, kTribeCount> tradePartners{};
    std::array<bool, kWorldLocationCount> outposts{};
    WorkforceState workforce;
    std::vector<Item> stockpile;
    std::array<OccupationState, kTribeCount> occupations{};
    PendingEvent pendingEvent;
    std::string workshopSupervisor;
    std::string healerSupervisor;
    std::vector<Character> roster;
    std::vector<PermanentSquad> squads;
    std::optional<ExpansionState> activeMission;
    WarState war;
    bool workforceReassignmentRequired = false;
    bool longModeFinalShown = false;
    GameEnding ending = GameEnding::None;
    std::vector<std::string> leadershipHistory;
    std::vector<ChronicleEntry> chronicle;
};

struct ActionResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool consumesAction = false;
    bool seasonAdvanced = false;
    bool endingReached = false;
    std::string message;
};

struct EndingSummary {
    GameEnding ending = GameEnding::None;
    std::string title;
    std::string epilogue;
    std::vector<std::string> statistics;
    std::vector<std::string> otherRoads;
    std::vector<ChronicleEntry> importantChronicle;
};

class GameEngine {
   public:
    explicit GameEngine(GameConfig config = {});
    explicit GameEngine(GameState state);

    ActionResult execute(std::string_view input);
    const GameState& state() const { return state_; }
    bool replaceState(const GameState& candidate, std::string& error);

    std::string statusText() const;
    std::string worldText() const;
    std::string diplomacyText() const;
    std::string factionText() const;
    std::string squadText() const;
    std::string objectiveText() const;
    std::string chronicleText() const;
    std::string helpText() const;
    std::string workforceText() const;
    std::string inventoryText() const;
    std::string peopleText() const;
    std::string personText(std::string_view name) const;
    std::string buildingsText() const;
    std::string technologiesText() const;
    std::string warTargetsText() const;
    std::string powerText() const;
    std::vector<GameEnding> availableEndings() const;
    EndingSummary endingSummary() const;

    static bool validateState(const GameState& candidate, std::string& error);
    static const std::array<WorldLocationInfo, kWorldLocationCount>& worldLocations();
    static std::string modeName(GameMode mode);
    static std::string phaseName(GamePhase phase);
    static std::string endingName(GameEnding ending);
    static std::string tribeName(TribeId tribe);
    static std::string resourceName(ResourceKind resource);

   private:
    ActionResult build(BuildingId building);
    ActionResult research(TechnologyId technology);
    ActionResult restSquad();
    ActionResult startMission(ResourceKind resource = ResourceKind::Food);
    ActionResult startOutpostMission();
    ActionResult startMission(MissionKind kind, ResourceKind resource);
    ActionResult executeMission(std::string_view input);
    ActionResult talk(TribeId tribe);
    ActionResult gift(TribeId tribe);
    ActionResult trade(TribeId tribe, ResourceKind offered, ResourceKind requested);
    ActionResult openTradeRoute(TribeId tribe);
    ActionResult marriage(TribeId tribe);
    ActionResult offerTribute(TribeId tribe);
    ActionResult demandTribute(TribeId tribe);
    ActionResult alliance(TribeId tribe);
    ActionResult declareWar(TribeId tribe);
    ActionResult negotiateTruce(TribeId tribe);
    ActionResult raid(TribeId tribe);
    ActionResult appeaseFaction(std::size_t faction);
    ActionResult formArmy(int warriors, int militia, std::string_view commander = {});
    ActionResult disbandArmy();
    ActionResult startWar(TribeId enemy);
    ActionResult setWarOrder(WarOrder order);
    ActionResult warAttack();
    ActionResult warDefend();
    ActionResult warRetreat();
    ActionResult endSeason();
    ActionResult chooseEnding(GameEnding ending);
    ActionResult continueSandbox();
    ActionResult assignWorkforce(WorkforceRole role, int count);
    ActionResult assignOutpostGuards(WorldLocationId location, int count);
    ActionResult craft(std::string_view recipe);
    ActionResult repair(std::string_view itemId);
    ActionResult scrap(std::string_view itemId);
    ActionResult equipPerson(std::string_view person, std::string_view slot, std::string_view itemId);
    ActionResult unequipPerson(std::string_view person, std::string_view slot);
    ActionResult appoint(std::string_view person, std::string_view role);
    ActionResult unappoint(std::string_view role);
    ActionResult configureSquad(const std::vector<std::string>& args);
    ActionResult treat(std::string_view squad);
    ActionResult chooseEvent(int option);
    ActionResult garrison(TribeId tribe, int warriors);

    ActionResult commit(GameState candidate, std::string message, bool consumesAction = false,
                        bool seasonAdvanced = false, bool endingReached = false);
    ActionResult rejected(std::string message) const;
    bool canSpendAction(ActionResult& result) const;
    bool diplomacyUsedThisSeason(TribeId tribe) const;
    ActionResult finalizeDiplomacy(TribeId tribe, ActionResult result);
    void spendAction(GameState& candidate) const;
    void addChronicle(GameState& candidate, int importance, std::string title, std::string detail) const;
    void settleFoodAndTribute(GameState& candidate, std::string& message) const;
    void settleAutonomousTribes(GameState& candidate, std::string& message) const;
    void settleFactions(GameState& candidate, std::string& message) const;
    void settleEvent(GameState& candidate, std::string& message) const;
    void finishExtinction(GameState& candidate, std::string& message) const;
    void concludeWarVictory(GameState& candidate, std::string& message) const;
    void releaseWarEquipment(GameState& candidate, bool damaged) const;
    int availableTeams(const GameState& state) const;
    int resourceValue(const GameState& state, ResourceKind resource) const;
    int& resourceRef(GameState& state, ResourceKind resource) const;
    WorldLocationId contactLocation(TribeId tribe) const;
    bool locationDiscovered(const GameState& state, WorldLocationId location) const;

    GameState state_;
};

} // namespace tribe
