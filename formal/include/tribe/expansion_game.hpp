#pragma once

#include "tribe/expansion_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tribe {

/// 用途：地图任务使用的十六地点数量常量。输入/输出：只读数组长度。
/// 不变量：必须与 GameEngine 的地点目录数量一致，否则持久化的地点编号会错位。
constexpr std::size_t kExpeditionWorldLocationCount = 16U;

/// 用途：标记任务处于行军中还是已完成结算。输入/输出：只读枚举；无状态修改。
enum class ExpansionPhase { Exploring = 0, Settled };
/// 用途：五种可由小队采集与贸易流转的资源类别。输入/输出：作为命令参数与载货字段使用。
/// 不变量：新增资源必须同步补齐资源名称、仓库显示以及 resourceValue / resourceRef 的映射。
enum class ResourceKind { Food = 0, Wood, Stone, Herbs, Hides };
/// 用途：区分资源采集任务与前哨建设任务。输入/输出：随任务状态一同持久化；无状态修改。
/// 不变量：前哨建设任务必须先在仓库装载木材与石料，再在地图现场建造。
enum class MissionKind { Gather = 0, OutpostConstruction };

/// 用途：地图命令的统一回执，供界面判断是否需要重绘页面。输入/输出：由调用方读取；无状态修改。
/// 失败：success 为假时 message 说明原因，且 state_ 必须保持完全不变。
struct ExpansionCommandResult {
    bool recognized = false;
    bool success = false;
    bool stateChanged = false;
    bool turnAdvanced = false;
    std::string message;
    /// 用途：将结果用于条件判断。输出：success；无状态修改和失败。
    explicit operator bool() const { return success; }
};

// 战役只使用一种远征格式：一支小队在十六地点地图上行进；地图状态必须始终由 ExpansionGame 校验。
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
    MissionKind missionKind = MissionKind::Gather;
    ResourceKind assignedResource = ResourceKind::Food;
    int crewSize = 2;
    int encounterLife = 0;
    bool encounterDefeated = false;
    bool settled = false;
};

/// 用途：承载一支小队在十六地点地图上的移动、采集、前哨建设与岩牙遭遇。
/// 状态影响：所有成功地图操作只经 commit 提交候选状态，失败路径不改变 state_。
/// 不变量：地图状态必须始终通过 validateState；装备只能处于仓库、人物、任务背包或军队锁定之一。
class ExpansionGame {
   public:
    /// 用途：按种子和队伍人数创建地图任务。输出：合法初始任务。
    /// 状态影响：初始化 state_。失败：非法状态由构造校验拒绝。不变量：营地已发现且可结算。
    explicit ExpansionGame(std::uint32_t seed = 1U, std::size_t squadSize = 4U);
    /// 用途：接管已有地图任务状态。输出：合法任务实例。失败：状态不合法时抛出异常。
    explicit ExpansionGame(ExpansionState state);
    /// 用途：解析并执行一条地图命令。输出：任务操作结果。失败不提交状态。
    ExpansionCommandResult execute(std::string_view input);
    /// 用途：只读取得已提交地图状态。输出：常量引用；无状态修改。
    const ExpansionState& state() const { return state_; }
    /// 用途：生成地点、载货和遭遇文本。输出：UTF-8 文本；无状态修改。
    std::string lookText() const;
    /// 用途：验证地图任务、背包与遭遇不变量。输出：操作结果；失败不修改输入状态。
    static OperationResult validateState(const ExpansionState& state);

   private:
    /// 以下地图操作输入已解析参数，输出 ExpansionCommandResult；成功仅提交候选状态；失败不改变 state_。
    /// 用途：沿合法道路移动。
    ExpansionCommandResult move(std::string_view target);
    /// 用途：采集当前地点允许的资源。
    ExpansionCommandResult gather(std::string_view resource);
    /// 用途：消耗携带材料建立前哨。
    ExpansionCommandResult buildOutpost();
    /// 用途：在营地或前哨结算任务。
    ExpansionCommandResult settle();
    /// 用途：攻击岩牙遭遇。
    ExpansionCommandResult attackEncounter();
    /// 用途：防守岩牙遭遇。
    ExpansionCommandResult defendEncounter();
    /// 用途：从岩牙遭遇撤退。
    ExpansionCommandResult retreatEncounter();
    /// 用途：使用任务载货草药。
    ExpansionCommandResult useHerb();
    /// 用途：验证并提交候选任务状态。失败不改变 state_；不变量由 validateState 统一保证。
    ExpansionCommandResult commit(ExpansionState candidate, std::string message, bool turnAdvanced);
    /// 用途：构造不提交状态的失败回执。
    ExpansionCommandResult rejected(std::string message) const;
    /// 用途：推进任务回合及队员疲劳。状态影响：仅修改 candidate；不变量：疲劳保持 0 至 100。
    void recordTurn(ExpansionState& candidate, int leaderFatigue, int followerFatigue) const;
    ExpansionState state_;
};

} // namespace tribe
