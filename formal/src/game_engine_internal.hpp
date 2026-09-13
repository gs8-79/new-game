#pragma once

#include "tribe/game_engine.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe::game_engine_detail {

/// 用途：判断枚举是否位于闭区间。输入：值和端点。输出：布尔值；无状态影响。
/// 失败：越界返回 false。不变量：仅比较底层整数，不转换或修改枚举。
template <typename Enum>
bool enumInRange(Enum value, Enum first, Enum last) {
    const int raw = static_cast<int>(value);
    return raw >= static_cast<int>(first) && raw <= static_cast<int>(last);
}

inline constexpr int kMaximumItemBonus = 20;

/// 下列解析与校验函数均不修改游戏状态；输入为文本或值对象，输出为解析值/布尔值；失败返回空或 false；不得接受越界枚举。
/// 用途：校验可持久化物品字段。
bool validStoredItem(const Item& item);
/// 用途：比较两份装备镜像是否完全一致。
bool sameItem(const Item& left, const Item& right);
/// 用途：解析非负十进制整数并写入 value。
bool parseNonnegative(std::string_view text, int& value);
/// 用途：解析中英文资源别名。
std::optional<ResourceKind> parseResource(std::string_view text);
/// 用途：解析中英文劳力职位别名。
std::optional<WorkforceRole> parseWorkforceRole(std::string_view text);
/// 用途：将装备槽位转换为显示名称。
std::string equipmentSlotName(EquipmentSlot slot);
/// 用途：解析装备槽位别名。
std::optional<EquipmentSlot> parseEquipmentSlot(std::string_view value);
/// 用途：解析部落别名。
std::optional<TribeId> parseTribe(std::string_view text);
/// 用途：映射外交地点到部落。
std::optional<TribeId> missionTribeAt(int location);
/// 用途：生成部落的英文命令键。
std::string tribeCommandName(TribeId tribe);
/// 用途：解析地点编号或中英文别名。
std::optional<WorldLocationId> parseLocation(std::string_view text);
/// 用途：查询敌方基础战力。
int enemyBasePower(TribeId tribe);
/// 用途：解析建筑别名。
std::optional<BuildingId> parseBuilding(std::string_view text);
/// 用途：解析技术别名。
std::optional<TechnologyId> parseTechnology(std::string_view text);
/// 用途：解析战争命令别名。
std::optional<WarOrder> parseWarOrder(std::string_view text);
/// 用途：解析可选结局别名。
std::optional<GameEnding> parseGameEnding(std::string_view text);

/// 下列构造、查询与数值辅助函数仅处理传入对象；除明确返回可写指针外无状态副作用；无效输入返回空、默认值或空指针。
/// 用途：创建带职业属性的初始角色。
Character makeCampaignCharacter(const std::string& name, Occupation occupation);
/// 用途：创建首领初始弓。
Item makeCampaignLeaderBow();
/// 用途：创建初始备用石刀。
Item makeCampaignSpareKnife();
/// 用途：在可写角色表中按姓名查询角色。
Character* findRosterCharacter(std::vector<Character>& roster, std::string_view name);
/// 用途：在只读角色表中按姓名查询角色。
const Character* findRosterCharacter(const std::vector<Character>& roster, std::string_view name);
/// 用途：计算永久小队成员的平均疲劳。
int permanentSquadFatigue(const PermanentSquad& squad, const std::vector<Character>& roster);
/// 用途：将外交关系夹紧到合法范围。
int relationClamp(int value);
/// 用途：将百分比夹紧到 0 至 100。
int percentClamp(int value);
/// 用途：判断文本是否含任一关键字。
bool containsAny(std::string_view text, std::initializer_list<std::string_view> keywords);
/// 用途：取得部落影响力最高的派系。
const FactionState& dominantFaction(const TribeProfile& profile);
/// 用途：判断是否已知目标部落派系诉求。
bool knowsFactionDemand(const DiplomacyRelation& relation);
/// 用途：判断是否已知完整派系网络。
bool knowsFullFactionNetwork(const DiplomacyRelation& relation);
/// 用途：转换战争命令显示名称。
std::string warOrderName(WarOrder order);
/// 用途：转换派系危机显示名称。
std::string crisisName(FactionCrisis crisis);

/// 用途：统计容器中的 true 数量。输入：可迭代布尔容器。输出：数量；无状态影响。
/// 失败：无。不变量：不改变容器元素。
template <typename Container>
int countTrue(const Container& values) {
    return static_cast<int>(std::count(values.begin(), values.end(), true));
}

/// 用途：计算品质对应的数值层级。
int itemQualityTier(ItemQuality quality);
/// 用途：转换物品品质显示名称。
std::string itemQualityName(ItemQuality quality);
/// 用途：转换职业显示名称。
std::string occupationName(Occupation occupation);
/// 用途：转换地点职能显示名称。
std::string locationRoleName(LocationRole role);
/// 用途：判断角色是否属于任一永久小队。
bool isPermanentSquadMember(const GameState& state, std::string_view name);
/// 用途：按负责人属性计算工艺等级。
int craftRankFor(const Character* supervisor);
/// 用途：查询当前工坊负责人的工艺等级。
int craftSupervisorRank(const GameState& state);
/// 用途：在建筑与劳力齐备时计算有效工艺等级。
int craftRank(const GameState& state);
/// 用途：按负责人属性计算医治等级。
int medicineRankFor(const Character* supervisor);
/// 用途：查询当前医者负责人的医治等级。
int medicineSupervisorRank(const GameState& state);
/// 用途：在建筑与劳力齐备时计算有效医治等级。
int medicineRank(const GameState& state);
/// 用途：转换季节事件显示名称。
std::string eventName(PendingEventKind kind);
/// 用途：生成当前待决事件及选项文本。
std::string eventText(const GameState& state);

} // namespace tribe::game_engine_detail
