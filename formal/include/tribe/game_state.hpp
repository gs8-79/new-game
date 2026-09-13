#pragma once

#include "tribe/expansion_game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tribe {

/// 用途：定义一局游戏的时长规则。输入/输出：作为配置和状态字段传递；无状态修改。
enum class GameMode { Quick = 0, Standard, Long };
/// 用途：定义引擎当前可执行命令的阶段。输入/输出：作为状态机字段传递；无状态修改。
enum class GamePhase { Managing = 0, Mission, War, EndingChoice, Finished, Sandbox };
/// 用途：定义已选择或可选择的结局。输入/输出：作为状态字段和视图结果传递；无状态修改。
enum class GameEnding { None = 0, Alliance, Conquest, Prosperity, Migration, Extinction };
/// 用途：定义统一人口池中的可分配岗位。输入/输出：作为劳力命令参数；无状态修改。
enum class WorkforceRole { FoodCrew = 0, WoodCrew, StoneCrew, HerbCrew, Crafters, Healers, Scouts, Envoys, CampGuards };

/// 用途：定义可建设的营地设施。输入/输出：作为建设状态数组下标；无状态修改。
enum class BuildingId { Granary = 0, Wall, Workshop, HealerHut, Watchtower, CouncilFire, Count };
/// 用途：定义可研究技术。输入/输出：作为技术状态数组下标；无状态修改。
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

/// 用途：定义十六地点地图的稳定持久化编号。输入/输出：作为数组下标和存档字段；无状态修改。
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

/// 用途：定义玩家和五个可交互部落。输入/输出：作为关系、战争和占领数组下标；无状态修改。
enum class TribeId { Player = 0, RiverDeer, WhiteFeather, Rockfang, Tidesalt, Blackstone, Count };
/// 用途：定义派系危机程度。输入/输出：作为派系状态字段；无状态修改。
enum class FactionCrisis { Calm = 0, Complaint, Slowdown, Refusal, Deposition, Coup };
/// 用途：定义战争回合指令。输入/输出：作为战争状态字段；无状态修改。
enum class WarOrder { Advance = 0, Hold, Focus, Flank, Cover, Retreat };
/// 用途：定义地点的玩法职责。输入/输出：作为地图目录元数据；无状态修改。
enum class LocationRole { Camp = 0, Resource, Diplomacy, Route, War };
/// 用途：定义待决季节事件类型。输入/输出：作为事件状态字段；无状态修改。
enum class PendingEventKind { Refugees = 0, Disease, Extortion, FactionDemand };

constexpr std::size_t kWorldLocationCount = static_cast<std::size_t>(WorldLocationId::Count);
constexpr std::size_t kTribeCount = static_cast<std::size_t>(TribeId::Count);
constexpr std::size_t kBuildingCount = static_cast<std::size_t>(BuildingId::Count);
constexpr std::size_t kTechnologyCount = static_cast<std::size_t>(TechnologyId::Count);
constexpr std::size_t kPlayerFactionCount = 3U;
/// 持久化集合上限同时约束状态校验与二进制读取，避免正常制造流程因 64 件旧上限而无法存档。
constexpr std::size_t kMaximumStockpileItems = 100000U;
constexpr std::size_t kMaximumMissionInventoryItems = 64U;
constexpr std::size_t kMaximumLockedWarEquipment = 64U;
constexpr std::size_t kMaximumLeadershipHistoryEntries = 256U;
constexpr std::size_t kMaximumChronicleEntries = 200U;
/// v6 以持久化的全局序号保证制造物品在跨仓库转移后仍具有唯一编号。
constexpr int kSaveVersion = 6;

/// 用途：将 Count 结尾的连续枚举转换为数组下标。输入：已验证枚举。输出：无符号下标；无状态修改。
/// 失败/不变量：调用方必须先保证枚举合法；不得将 Count 或无效枚举用于索引。
template <typename Enum>
constexpr std::size_t indexOf(const Enum value) {
    return static_cast<std::size_t>(value);
}

/// 用途：描述新局初始化参数。输入/输出：由应用传给 GameEngine；无状态修改。
struct GameConfig {
    GameMode mode = GameMode::Standard;
    std::uint32_t seed = 1U;
    std::string tribeName = "燧火";
    std::string leaderName = "炎角";
    std::string leaderFocus = "生存";
};

/// 用途：描述地图目录中的稳定地点元数据。输入/输出：供规则、文本和终端视图只读使用。
/// 不变量：id 与目录下标一致，neighbors 仅包含合法且双向的地点。
struct WorldLocationInfo {
    WorldLocationId id = WorldLocationId::Camp;
    std::string name;
    std::string feature;
    LocationRole role = LocationRole::Route;
    std::vector<WorldLocationId> neighbors;
};

/// 用途：保存一个部落派系的可持久化状态。输入/输出：由外交和季结算规则读写。
struct FactionState {
    std::string name;
    int influence = 30;
    int satisfaction = 60;
    std::string demand;
    std::string candidate;
    FactionCrisis crisis = FactionCrisis::Calm;
};

/// 用途：保存一个部落的身份和派系网络。输入/输出：由外交、战争和文本视图读写。
struct TribeProfile {
    TribeId id = TribeId::Player;
    std::string name;
    std::string leader;
    std::string actingLeader;
    std::string successor;
    std::string personality;
    std::vector<FactionState> factions;
};

/// 用途：保存玩家与一个部落之间的外交关系。输入/输出：由外交和季结算规则读写。
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

/// 用途：保存长期小队的编制和驻地。输入/输出：由任务、休整和人口规则读写。
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

/// 用途：保存正在组织或进行中的战争。输入/输出：由战争和季结算规则读写。
/// 不变量：lockedEquipment 与库存、角色装备和任务背包的所有权互斥。
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

/// 用途：保存统一人口池中的岗位与前哨占用。输入/输出：由经营和人口规则读写。
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

/// 用途：保存敌对部落据点的占领与驻军状态。输入/输出：由战争和人口规则读写。
struct OccupationState {
    bool occupied = false;
    int garrison = 0;
    int unrest = 0;
};

/// 用途：保存本季必须处理的事件。输入/输出：由季结算和事件命令读写。
struct PendingEvent {
    PendingEventKind kind = PendingEventKind::Refugees;
    bool active = false;
};

/// 用途：保存一条有限长度的编年史记录。输入/输出：由候选状态提交和文本视图读写。
struct ChronicleEntry {
    int season = 0;
    int importance = 1;
    std::string title;
    std::string detail;
};

/// 用途：保存完整可持久化游戏状态。输入/输出：由引擎候选提交和 SaveRepository 读写。
/// 不变量：任何写入者都必须通过 GameEngine::validateState；字段顺序不得改变 v6 编解码语义。
struct GameState {
    // 会话与进度。
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
    // 下一件制造装备使用的序号；必须为正数，且只会递增而不会复用。
    std::uint32_t nextItemSerial = 1U;

    // 身份、地图与外交。
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

    // 人口、人物、任务与战争。
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

    // 派生门禁与结局记录。
    bool workforceReassignmentRequired = false;
    bool longModeFinalShown = false;
    GameEnding ending = GameEnding::None;
    std::vector<std::string> leadershipHistory;
    std::vector<ChronicleEntry> chronicle;
};

/// 用途：描述一条命令的执行结果。输入/输出：由引擎返回给应用；无状态修改。
struct ActionResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool consumesAction = false;
    bool seasonAdvanced = false;
    bool endingReached = false;
    std::string message;
};

/// 用途：汇总一个已达成结局的展示数据。输入/输出：由引擎生成、结局演出只读使用。
struct EndingSummary {
    GameEnding ending = GameEnding::None;
    std::string title;
    std::string epilogue;
    std::vector<std::string> statistics;
    std::vector<std::string> otherRoads;
    std::vector<ChronicleEntry> importantChronicle;
};

} // namespace tribe
