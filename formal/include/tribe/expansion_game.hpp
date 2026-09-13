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
enum class ResourceKind { Food = 0, Wood, Stone, Herbs, Hides };
enum class MissionKind { Gather = 0, OutpostConstruction };

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
