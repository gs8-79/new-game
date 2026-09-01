#pragma once

#include "tribe/expansion_game.hpp"
#include "tribe/formal_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

enum class CampaignMode { Quick = 0, Course, Long };
enum class CampaignPhase { Managing = 0, Mission, War, EndingChoice, Finished, Sandbox };
enum class CampaignEnding { None = 0, Alliance, Conquest, Prosperity, Migration, Extinction };
enum class ResourceKind { Food = 0, Wood, Stone, Herbs, Shells };

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

enum class TribeIdV2 { Player = 0, RiverDeer, WhiteFeather, Rockfang, Tidesalt, Blackstone, Count };
enum class FactionCrisis { Calm = 0, Complaint, Slowdown, Refusal, Deposition, Coup };
enum class WarOrder { Advance = 0, Hold, Focus, Flank, Cover, Retreat };

constexpr std::size_t kWorldLocationCount = static_cast<std::size_t>(WorldLocationId::Count);
constexpr std::size_t kTribeV2Count = static_cast<std::size_t>(TribeIdV2::Count);
constexpr std::size_t kPlayerFactionCount = 3U;
constexpr int kCampaignSaveVersion = 2;

struct CampaignConfig {
    CampaignMode mode = CampaignMode::Course;
    std::uint32_t seed = 1U;
    std::string tribeName = "燧火";
    std::string leaderName = "炎角";
    std::string leaderFocus = "生存";
};

struct WorldLocationInfo {
    WorldLocationId id = WorldLocationId::Camp;
    std::string name;
    std::string feature;
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
    TribeIdV2 id = TribeIdV2::Player;
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
    ResidentMission residentMission = ResidentMission::None;
    int fatigue = 0;
    int eliteExperience = 0;
    bool personallyDeployedThisSeason = false;
    bool refusingOrders = false;
};

struct WarState {
    bool active = false;
    TribeIdV2 enemy = TribeIdV2::Rockfang;
    std::string commander;
    int warriors = 0;
    int militia = 0;
    int playerPower = 0;
    int enemyPower = 0;
    int front = 0;
    WarOrder order = WarOrder::Hold;
    bool riskConfirmed = false;
};

struct ChronicleEntry {
    int season = 0;
    int importance = 1;
    std::string title;
    std::string detail;
};

struct CampaignState {
    CampaignMode mode = CampaignMode::Course;
    CampaignPhase phase = CampaignPhase::Managing;
    std::uint32_t seed = 1U;
    int season = 1;
    int seasonLimit = 16;
    int actionsLeft = 3;
    int population = 16;
    int food = 30;
    int wood = 12;
    int stone = 4;
    int herbs = 3;
    int warriors = 3;
    int morale = 60;
    int campDurability = 20;
    int stability = 65;
    int shells = 0;
    int tradeCount = 0;
    int warsWon = 0;
    int warsLost = 0;
    int missionCount = 0;
    int missionDeaths = 0;
    int highestLevel = 1;
    int rockfangStrength = 20;
    std::string tribeName = "燧火";
    std::string leaderName = "炎角";
    std::string actingLeaderName;
    std::string leaderFocus = "生存";
    std::array<bool, kWorldLocationCount> discovered{};
    std::array<bool, kBuildingCount> buildings{};
    std::array<bool, kTechnologyCount> technologies{};
    std::array<TribeProfile, kTribeV2Count> tribes{};
    std::array<DiplomacyRelation, kTribeV2Count> relations{};
    std::array<FactionState, kPlayerFactionCount> playerFactions{};
    std::array<bool, kTribeV2Count> tradePartners{};
    std::vector<Character> roster;
    std::vector<PermanentSquad> squads;
    std::optional<ExpansionState> activeMission;
    bool missionRewardClaimed = false;
    WarState war;
    bool currencyUnlocked = false;
    bool rockfangFortCaptured = false;
    bool longModeFinalShown = false;
    CampaignEnding ending = CampaignEnding::None;
    std::vector<std::string> leadershipHistory;
    std::vector<ChronicleEntry> chronicle;
};

struct CampaignActionResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool consumesAction = false;
    bool seasonAdvanced = false;
    bool endingReached = false;
    std::string message;
};

struct EndingSummary {
    CampaignEnding ending = CampaignEnding::None;
    std::string title;
    std::string epilogue;
    std::vector<std::string> statistics;
    std::vector<std::string> otherRoads;
    std::vector<ChronicleEntry> importantChronicle;
};

class CampaignGame {
public:
    explicit CampaignGame(CampaignConfig config = {});
    explicit CampaignGame(CampaignState state);

    CampaignActionResult execute(std::string_view input);
    const CampaignState& state() const { return state_; }
    bool replaceState(const CampaignState& candidate, std::string& error);

    std::string statusText() const;
    std::string worldText() const;
    std::string diplomacyText() const;
    std::string factionText() const;
    std::string squadText() const;
    std::string objectiveText() const;
    std::string chronicleText() const;
    std::string helpText() const;
    std::vector<CampaignEnding> availableEndings() const;
    EndingSummary endingSummary() const;

    static bool validateState(const CampaignState& candidate, std::string& error);
    static const std::array<WorldLocationInfo, kWorldLocationCount>& worldLocations();
    static std::string modeName(CampaignMode mode);
    static std::string phaseName(CampaignPhase phase);
    static std::string endingName(CampaignEnding ending);
    static std::string tribeName(TribeIdV2 tribe);
    static std::string resourceName(ResourceKind resource);

private:
    CampaignActionResult gather(ResourceKind resource);
    CampaignActionResult scout(WorldLocationId location);
    CampaignActionResult build(BuildingId building);
    CampaignActionResult research(TechnologyId technology);
    CampaignActionResult setResidentMission(ResidentMission mission);
    CampaignActionResult restSquad();
    CampaignActionResult startMission();
    CampaignActionResult executeMission(std::string_view input);
    CampaignActionResult talk(TribeIdV2 tribe);
    CampaignActionResult gift(TribeIdV2 tribe);
    CampaignActionResult trade(TribeIdV2 tribe, ResourceKind offered, ResourceKind requested);
    CampaignActionResult openTradeRoute(TribeIdV2 tribe);
    CampaignActionResult marriage(TribeIdV2 tribe);
    CampaignActionResult offerTribute(TribeIdV2 tribe);
    CampaignActionResult demandTribute(TribeIdV2 tribe);
    CampaignActionResult alliance(TribeIdV2 tribe);
    CampaignActionResult declareWar(TribeIdV2 tribe);
    CampaignActionResult negotiateTruce(TribeIdV2 tribe);
    CampaignActionResult raid(TribeIdV2 tribe);
    CampaignActionResult appeaseFaction(std::size_t faction);
    CampaignActionResult formArmy(int warriors, int militia);
    CampaignActionResult startWar(TribeIdV2 enemy);
    CampaignActionResult setWarOrder(WarOrder order);
    CampaignActionResult warAttack();
    CampaignActionResult warDefend();
    CampaignActionResult warRetreat();
    CampaignActionResult endSeason();
    CampaignActionResult chooseEnding(CampaignEnding ending);
    CampaignActionResult continueSandbox();

    CampaignActionResult commit(CampaignState candidate, std::string message,
        bool consumesAction = false, bool seasonAdvanced = false, bool endingReached = false);
    CampaignActionResult rejected(std::string message) const;
    bool canSpendAction(CampaignActionResult& result) const;
    void spendAction(CampaignState& candidate) const;
    void addChronicle(CampaignState& candidate, int importance, std::string title, std::string detail) const;
    void settleResidentSquads(CampaignState& candidate, std::string& message) const;
    void settleFoodAndTribute(CampaignState& candidate, std::string& message) const;
    void settleAutonomousTribes(CampaignState& candidate, std::string& message) const;
    void settleFactions(CampaignState& candidate, std::string& message) const;
    void settleEvent(CampaignState& candidate, std::string& message) const;
    void unlockCurrencyIfEligible(CampaignState& candidate, std::string& message) const;
    void finishExtinction(CampaignState& candidate, std::string& message) const;
    void advanceWarFront(CampaignState& candidate, std::string& message) const;
    int availableTeams(const CampaignState& state) const;
    int resourceValue(const CampaignState& state, ResourceKind resource) const;
    int& resourceRef(CampaignState& state, ResourceKind resource) const;
    WorldLocationId contactLocation(TribeIdV2 tribe) const;
    bool locationDiscovered(const CampaignState& state, WorldLocationId location) const;

    CampaignState state_;
};

} // namespace tribe
