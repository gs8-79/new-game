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
// 资源队枚举和字段保留用于读取旧存档，但不再参与新任务的资源类型限制。
enum class WorkforceRole {
    FoodCrew = 0,
    WoodCrew,
    StoneCrew,
    HerbCrew,
    Crafters,
    Healers,
    Scouts,
    Envoys,
    CampGuards,
    Housing
};

enum class BuildingId { Granary = 0, Wall, Workshop, HealerHut, Watchtower, CouncilFire, Longhouse, Count };

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
// v7 重构人口、行动力与季度事件队列，同时保留 v5/v6 的只读兼容解析。
constexpr int kSaveVersion = 7;

/// 用途：将连续枚举转换为数组下标。输入：枚举值。输出：无符号下标；无状态修改。
/// 失败：调用方必须先保证枚举合法。不变量：仅用于以 Count 结尾的连续枚举。
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
    // 以下四个字段仅为旧存档兼容保留；新规则不再按资源分配岗位。
    int foodCrew = 0;
    int woodCrew = 0;
    int stoneCrew = 0;
    int herbCrew = 0;
    int crafters = 0;
    int healers = 0;
    int scouts = 0;
    int envoys = 0;
    int campGuards = 0;
    int housing = 0;
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
    int actionsLeft = 7;
    int population = 16;
    int populationLimit = 18;
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
    // 当前季度按顺序等待处理的事件；pendingEvent 是队首兼容镜像。
    std::vector<PendingEventKind> pendingEvents;
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
    /// 用途：按配置创建一局完整游戏。输入：模式、种子和可选名称。输出：合法初始引擎。
    /// 状态影响：初始化 state_。失败：初始装备或状态校验失败时抛出异常。不变量：初始 state_ 必须可验证。
    explicit GameEngine(GameConfig config = {});
    /// 用途：接管已持久化的游戏状态。输入：候选状态。输出：合法引擎。
    /// 状态影响：移动写入 state_。失败：状态不合法时抛出异常。不变量：不接受部分合法状态。
    explicit GameEngine(GameState state);

    /// 用途：解析并调度一条中英文游戏命令。输入：原始终端文本。输出：ActionResult。
    /// 状态影响：仅成功提交候选状态。失败：语法、前置条件或校验失败时拒绝。不变量：失败不污染 state_。
    ActionResult execute(std::string_view input);
    /// 用途：只读取得当前已提交状态。输入：无。输出：state_ 常量引用。
    /// 状态影响：无。失败：无。不变量：调用者不得通过返回值修改状态。
    const GameState& state() const { return state_; }
    /// 用途：以外部候选状态替换当前局面。输入：完整 GameState 与错误输出。输出：是否提交。
    /// 状态影响：仅校验成功才替换 state_。失败：状态非法时写入 error。不变量：失败保持原状态。
    bool replaceState(const GameState& candidate, std::string& error);

    /// 以下文本视图均为只读：输入为当前已提交状态或查询名，输出 UTF-8
    /// 文本；不修改状态；查询失败以文本说明；不得泄露非法控制序列。 用途：生成资源、季节和行动总览。
    std::string statusText() const;
    /// 用途：生成十六地点地图、道路与任务状态。
    std::string worldText() const;
    /// 用途：生成部落关系与外交选项。
    std::string diplomacyText() const;
    /// 用途：生成玩家派系的影响力和危机信息。
    std::string factionText() const;
    /// 用途：生成永久小队及活动任务信息。
    std::string squadText() const;
    /// 用途：生成当前目标和结局进度。
    std::string objectiveText() const;
    /// 用途：生成已记录的编年史。
    std::string chronicleText() const;
    /// 用途：生成可用命令帮助文本。
    std::string helpText() const;
    /// 用途：生成统一人口池与劳力分配文本。
    std::string workforceText() const;
    /// 用途：生成库存与装备流转文本。
    std::string inventoryText() const;
    /// 用途：生成角色名单摘要。
    std::string peopleText() const;
    /// 用途：生成指定角色详情；未知姓名返回提示。
    std::string personText(std::string_view name) const;
    /// 用途：生成建筑状态与建设条件。
    std::string buildingsText() const;
    /// 用途：生成技术状态与研究条件。
    std::string technologiesText() const;
    /// 用途：生成战争目标、驻军和敌情。
    std::string warTargetsText() const;
    /// 用途：生成生产、外交、战争等综合实力。
    std::string powerText() const;
    /// 用途：列出当前状态可选择的结局。输出：结局枚举集合；状态影响和失败均无。
    std::vector<GameEnding> availableEndings() const;
    /// 用途：汇总已达成结局。输出：展示用统计；状态影响和失败均无。
    EndingSummary endingSummary() const;

    /// 以下元数据和验证函数均不修改状态：验证失败写 error 或返回空/默认文本，并保持输入不变。
    /// 用途：集中验证完整持久化状态及跨模块不变量。
    static bool validateState(const GameState& candidate, std::string& error);
    /// 用途：取得固定十六地点道路定义。
    static const std::array<WorldLocationInfo, kWorldLocationCount>& worldLocations();
    /// 用途：将模式枚举转换为显示名称。
    static std::string modeName(GameMode mode);
    /// 用途：将阶段枚举转换为显示名称。
    static std::string phaseName(GamePhase phase);
    /// 用途：将结局枚举转换为显示名称。
    static std::string endingName(GameEnding ending);
    /// 用途：将部落枚举转换为显示名称。
    static std::string tribeName(TribeId tribe);
    /// 用途：将资源枚举转换为显示名称。
    static std::string resourceName(ResourceKind resource);

   private:
    /// 以下经营操作输入命令已解析参数，输出 ActionResult；仅成功提交候选状态；失败保持 state_
    /// 不变；人口、装备和资源不变量必须通过校验。 用途：建设指定建筑。
    ActionResult build(BuildingId building, int workers = 2);
    /// 用途：研究指定技术。
    ActionResult research(TechnologyId technology, int workers = 0);
    /// 用途：安排永久小队在营地休整。
    ActionResult restSquad();
    /// 用途：以指定资源发起采集任务。
    ActionResult startMission(ResourceKind resource = ResourceKind::Food, int people = 0);
    /// 用途：发起前哨建设任务。
    ActionResult startOutpostMission();
    /// 用途：按任务类型创建活动地图状态。
    ActionResult startMission(MissionKind kind, ResourceKind resource, int people = 0);
    /// 用途：转交活动地图命令并结算返回结果。
    ActionResult executeMission(std::string_view input);

    /// 以下外交操作输入目标部落和资源，输出 ActionResult；成功时同时写入候选编年史；失败不消耗行动且不修改关系。
    /// 用途：与目标部落交谈。
    ActionResult talk(TribeId tribe);
    /// 用途：向目标部落赠礼。
    ActionResult gift(TribeId tribe);
    /// 用途：以两类资源进行贸易。
    ActionResult trade(TribeId tribe, ResourceKind offered, ResourceKind requested);
    /// 用途：开通目标部落的商路。
    ActionResult openTradeRoute(TribeId tribe);
    /// 用途：与目标部落联姻。
    ActionResult marriage(TribeId tribe);
    /// 用途：向目标部落进贡。
    ActionResult offerTribute(TribeId tribe);
    /// 用途：要求目标部落进贡。
    ActionResult demandTribute(TribeId tribe);
    /// 用途：与目标部落结盟。
    ActionResult alliance(TribeId tribe);
    /// 用途：向目标部落宣战。
    ActionResult declareWar(TribeId tribe);
    /// 用途：与交战部落谈判停战。
    ActionResult negotiateTruce(TribeId tribe);
    /// 用途：劫掠目标部落以换取资源。
    ActionResult raid(TribeId tribe);
    /// 用途：安抚指定玩家派系。
    ActionResult appeaseFaction(std::size_t faction);

    /// 以下战争操作成功时锁定或归还装备并保持人口池一致；失败不移动装备、不改变军队编制。
    /// 用途：按战士、民兵和统帅组建军队。
    ActionResult formArmy(int warriors, int militia, std::string_view commander = {});
    /// 用途：解散军队并归还可用装备。
    ActionResult disbandArmy();
    /// 用途：向指定敌对部落发起出征。
    ActionResult startWar(TribeId enemy);
    /// 用途：设置当前战争命令。
    ActionResult setWarOrder(WarOrder order);
    /// 用途：执行战争进攻回合。
    ActionResult warAttack();
    /// 用途：执行战争防守回合。
    ActionResult warDefend();
    /// 用途：执行战争撤退回合。
    ActionResult warRetreat();

    /// 以下季结算操作成功时推进季节或结局阶段；资源不足、未决事件或状态非法时拒绝，且保持已提交状态不变。
    /// 用途：结算当前季节。
    ActionResult endSeason();
    /// 用途：选择已满足条件的结局。
    ActionResult chooseEnding(GameEnding ending);
    /// 用途：在达成结局后继续沙盒。
    ActionResult continueSandbox();
    /// 用途：选择当前季节事件选项。
    ActionResult chooseEvent(int option);

    /// 用途：设置某类劳力人数。
    ActionResult assignWorkforce(WorkforceRole role, int count);
    /// 用途：设置已发现前哨的守卫人数。
    ActionResult assignOutpostGuards(WorldLocationId location, int count);
    /// 用途：消耗材料制造装备。
    ActionResult craft(std::string_view recipe);
    /// 用途：消耗材料修复库存装备。
    ActionResult repair(std::string_view itemId);
    /// 用途：将库存装备报废为材料。
    ActionResult scrap(std::string_view itemId);
    /// 用途：将库存装备穿戴到角色槽位。
    ActionResult equipPerson(std::string_view person, std::string_view slot, std::string_view itemId);
    /// 用途：卸下角色指定槽位的装备。
    ActionResult unequipPerson(std::string_view person, std::string_view slot);
    /// 用途：任命建筑负责人。
    ActionResult appoint(std::string_view person, std::string_view role);
    /// 用途：撤销建筑负责人任命。
    ActionResult unappoint(std::string_view role);
    /// 用途：配置永久小队成员和队长。
    ActionResult configureSquad(const std::vector<std::string>& args);
    /// 用途：以草药治疗指定永久小队。
    ActionResult treat(std::string_view squad);
    /// 用途：调整已占领据点的驻军。
    ActionResult garrison(TribeId tribe, int warriors);

    /// 用途：验证并原子提交候选状态。输入：候选状态和结果元数据。输出：ActionResult。
    /// 状态影响：校验成功才移动写入 state_。失败：校验失败返回拒绝。不变量：提交后人口和物品所有权一致。
    ActionResult commit(GameState candidate, std::string message, bool consumesAction = false,
                        bool seasonAdvanced = false, bool endingReached = false);
    /// 用途：构造不改变状态的失败结果。输入：错误消息。输出：失败 ActionResult；无状态影响。
    ActionResult rejected(std::string message) const;
    /// 用途：检查本季剩余行动。输入：结果写入位置。输出：是否可行动；无状态影响。
    bool canSpendAction(ActionResult& result, int cost = 1) const;
    /// 用途：判断目标部落本季是否已外交。输出：布尔值；无状态影响。
    bool diplomacyUsedThisSeason(TribeId tribe) const;
    /// 用途：向候选状态写入外交编年史。状态影响：仅修改 candidate；不变量：必须在 commit 前调用。
    void finalizeDiplomacy(GameState& candidate, TribeId tribe) const;
    /// 用途：从候选状态扣除一次行动。失败：调用方负责先验证行动数；不变量：不得对已提交 state_ 直接调用。
    void spendAction(GameState& candidate, int cost = 1) const;
    /// 用途：向候选状态追加受限长度编年史。状态影响：可能丢弃最旧记录；不变量：最多保留 200 条。
    void addChronicle(GameState& candidate, int importance, std::string title, std::string detail) const;
    /// 用途：结算食物消耗与朝贡。
    void settleFoodAndTribute(GameState& candidate, std::string& message) const;
    /// 用途：结算其他部落的自主变化。
    void settleAutonomousTribes(GameState& candidate, std::string& message) const;
    /// 用途：结算玩家派系影响与危机。
    void settleFactions(GameState& candidate, std::string& message) const;
    /// 用途：将候选状态转入灭绝结局。
    void finishExtinction(GameState& candidate, std::string& message) const;
    /// 用途：将候选状态转入战争胜利结果。
    void concludeWarVictory(GameState& candidate, std::string& message) const;
    /// 用途：归还或损坏战争锁定装备。状态影响：只修改 candidate；不变量：装备不能同时留在两处。
    void releaseWarEquipment(GameState& candidate, bool damaged) const;
    /// 用途：计算当前可出发小队容量。输出：人数上限；无状态影响。
    int availableTeams(const GameState& state) const;
    /// 用途：读取某类资源的数值。输出：资源数量；无状态影响。
    int resourceValue(const GameState& state, ResourceKind resource) const;
    /// 用途：取得候选状态中某资源的可写引用。调用方必须在 commit 前保持资源非负。
    int& resourceRef(GameState& state, ResourceKind resource) const;
    /// 用途：映射部落到地图接触点。输出：地点枚举；无状态影响。
    WorldLocationId contactLocation(TribeId tribe) const;
    /// 用途：判断地点是否已发现。输出：布尔值；无状态影响。
    bool locationDiscovered(const GameState& state, WorldLocationId location) const;

    GameState state_;
};

} // namespace tribe
