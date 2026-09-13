#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

/// 用途：定义角色八项属性，并同时充当 Attributes 的下标。输入/输出：由规则与界面只读使用。
/// 失败：无。不变量：Count 只是数组长度哨兵，不是可分配属性；下标顺序即存档数组与界面“属性1至8”的顺序。
enum class Attribute {
    Strength = 0,
    Agility,
    Endurance,
    Perception,
    Survival,
    Diplomacy,
    Willpower,
    Leadership,
    Count
};

constexpr std::size_t kAttributeCount = static_cast<std::size_t>(Attribute::Count);
/// 用途：单项属性的合法闭区间，加点、状态校验与存档解析共用。输入/输出：只读常量。
/// 不变量：任何分配或持久化路径都不得让属性落到该区间之外。
constexpr int kMinimumAttribute = 1;
constexpr int kMaximumAttribute = 20;

/// 用途：保存一名角色的八项基础属性值。输入/输出：按 Attribute 下标读写；无状态修改。
/// 失败：越界枚举由 operator[] 抛出范围异常。不变量：数组长度恒为 kAttributeCount。
struct Attributes {
    /// 用途：以默认属性值创建数组。输出：属性集合；无外部状态修改。
    Attributes();
    /// 用途：以统一初值创建属性数组。输入：初值。输出：属性集合。
    explicit Attributes(int initialValue);

    /// 用途：取得指定属性可写引用。失败：枚举越界时抛出范围异常。
    int& operator[](Attribute attribute);
    /// 用途：取得指定属性值。失败：枚举越界时抛出范围异常；无状态修改。
    int operator[](Attribute attribute) const;

    std::array<int, kAttributeCount> values{};
};

/// 用途：角色职业，决定升级时的推荐属性顺序与负责人任命校验。输入/输出：只读枚举；无状态修改。
/// 失败：无。不变量：新增职业必须同步补齐 prioritiesFor 分支与职业显示名称。
enum class Occupation { Hunter = 0, Warrior, Scout, Healer, Crafter, Envoy };

/// 用途：角色可装备的槽位，同时作为 equipment 数组下标。输入/输出：只读枚举；无状态修改。
/// 失败：无。不变量：Count 只是数组长度哨兵，不可作为实际装备栏。
enum class EquipmentSlot { MainHand = 0, OffHand, Head, Body, Hands, LegsFeet, Tool, Accessory, Count };

constexpr std::size_t kEquipmentSlotCount = static_cast<std::size_t>(EquipmentSlot::Count);

/// 用途：装备品质，决定叠加在正属性上的额外加成档位。输入/输出：只读枚举；无状态修改。
/// 失败：未知枚举按零加成处理。不变量：品质只放大已经为正的加成，不会凭空产生属性。
enum class ItemQuality { Crude = 0, Common, Fine, Rare, Legendary };
/// 用途：装备完好度。输入/输出：只读枚举；无状态修改。
/// 失败：无。不变量：Damaged 的加成减半，Scrapped 既不能装备也不计入有效属性。
enum class ItemCondition { Intact = 0, Damaged, Scrapped };

/// 用途：描述一件装备的名称、品质、完好度与属性加成。输入/输出：由仓库、人物、任务背包和战争锁定共用。
/// 失败：无。不变量：同一件装备同一时刻只能存在于上述四处之一；id 由全局制造序号保证唯一。
struct Item {
    std::string id;
    std::string name;
    ItemQuality quality = ItemQuality::Common;
    ItemCondition condition = ItemCondition::Intact;
    int weight = 0;
    int slotCount = 1;
    std::optional<EquipmentSlot> equipmentSlot;
    Attributes bonuses;
};

/// 用途：一名具名角色的属性、装备和当前状态。输入/输出：由人物名单与任务小队持有；无状态修改。
/// 失败：无。不变量：槽内物品的 equipmentSlot 必须与所在槽位一致，Scrapped 物品不得入槽。
struct Character {
    /// 用途：创建空角色。输出：默认值；无外部状态修改。
    Character() = default;
    /// 用途：创建带姓名和职业的角色。输出：角色对象；调用方仍须通过规则函数完善属性。
    Character(std::string characterName, Occupation characterOccupation);

    std::string name;
    Occupation occupation = Occupation::Hunter;
    int level = 1;
    int experience = 0;
    int growthPoints = 0;
    int life = 100;
    int fatigue = 0;
    int loyalty = 50;
    Attributes attributes{5};
    std::array<std::optional<Item>, kEquipmentSlotCount> equipment{};
};

/// 用途：规则函数的统一回执。输入/输出：由调用方读取 success 与 message；无状态修改。
/// 失败：success 为假时 message 说明原因，调用方须保证失败路径没有改动传入对象。
struct OperationResult {
    bool success = false;
    std::string message;

    /// 用途：将操作结果用于条件判断。输出：success；无状态修改和失败。
    explicit operator bool() const { return success; }
};

/// 用途：容量受限的装备容器，同时按总重量与总格数设限。输入/输出：由任务背包和共享仓库使用。
/// 失败：超重或超格时拒绝放入，容器内容保持不变。不变量：上限恒为非负，已用容量永不超过上限。
class Inventory {
   public:
    /// 用途：创建容量受限背包。输入：重量与格数上限。输出：背包对象；不变量：上限为正。
    explicit Inventory(int weightLimit = 50, int slotLimit = 16);

    /// 用途：查询重量上限。输出：常量；无状态修改。
    int weightLimit() const { return weightLimit_; }
    /// 用途：查询格数上限。输出：常量；无状态修改。
    int slotLimit() const { return slotLimit_; }
    /// 用途：计算已用重量。输出：总重量；无状态修改。
    int usedWeight() const;
    /// 用途：计算已用格数。输出：总格数；无状态修改。
    int usedSlots() const;
    /// 用途：只读取得背包物品。输出：常量引用；调用方不得绕过容量规则修改。
    const std::vector<Item>& items() const { return items_; }

    /// 用途：无代价放入物品。状态影响：成功追加物品；失败容量不足时不修改背包。
    OperationResult pickupFree(Item item);
    /// 用途：按编号取出物品。状态影响：成功移除并写 item；失败保持背包和 item 不变。
    OperationResult take(std::string_view itemId, Item& item);

   private:
    int weightLimit_ = 0;
    int slotLimit_ = 0;
    std::vector<Item> items_;
};

/// 用途：一支地图小队的成员、队长和凝聚力。输入/输出：由任务状态持有；无状态修改。
/// 失败：无。不变量：成员人数在 kMinimumSquadSize 至 kMaximumSquadSize 之间，队长下标必须落在成员范围内。
struct Squad {
    std::string name;
    std::vector<Character> members;
    std::size_t leaderIndex = 0;
    int cohesion = 50;
};

/// 用途：出征军队的统帅与兵员构成。输入/输出：由战争状态持有；无状态修改。
/// 失败：无。不变量：战士与民兵均非负且至少一人，统帅名不得为空。
struct Army {
    std::string commanderName;
    int warriors = 0;
    int militia = 0;
};

constexpr std::size_t kMinimumSquadSize = 2;
constexpr std::size_t kMaximumSquadSize = 8;

/// 用途：消耗成长点分配角色属性。成功修改 character；失败保持角色不变；属性范围不得越界。
OperationResult allocateAttribute(Character& character, Attribute attribute, int points);
/// 用途：增加经验并在需要时升级。成功修改 character；失败保持角色不变。
OperationResult gainExperience(Character& character, int amount);
/// 用途：按职业推荐属性分配。成功修改 character；失败保持角色不变。
OperationResult recommendAttributes(Character& character);
/// 用途：为角色装备指定槽位物品。成功替换槽位；失败不修改角色；槽位必须匹配物品。
OperationResult equipItem(Character& character, EquipmentSlot slot, const Item& item);
/// 用途：恢复角色生命与疲劳。成功修改 character；失败保持角色不变；数值保持合法范围。
OperationResult rest(Character& character, int lifeRecovery, int fatigueRecovery);

/// 用途：计算指定等级的升级经验门槛。输出：正数；无状态修改。
int experienceForNextLevel(int level);
/// 用途：计算角色生命上限。输出：非负上限；无状态修改。
int maximumLife(const Character& character);
/// 用途：合并角色基础与装备属性。输出：有效属性；无状态修改。
Attributes effectiveAttributes(const Character& character);
/// 用途：验证地图小队。输出：结果；失败不修改小队。
OperationResult validateSquad(const Squad& squad);
/// 用途：验证战争军队。输出：结果；失败不修改军队。
OperationResult validateArmy(const Army& army);

} // namespace tribe
