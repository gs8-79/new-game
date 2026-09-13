#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

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
constexpr int kMinimumAttribute = 1;
constexpr int kMaximumAttribute = 20;

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

enum class Occupation { Hunter = 0, Warrior, Scout, Healer, Crafter, Envoy };

enum class EquipmentSlot { MainHand = 0, OffHand, Head, Body, Hands, LegsFeet, Tool, Accessory, Count };

constexpr std::size_t kEquipmentSlotCount = static_cast<std::size_t>(EquipmentSlot::Count);

enum class ItemQuality { Crude = 0, Common, Fine, Rare, Legendary };
enum class ItemCondition { Intact = 0, Damaged, Scrapped };

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

struct OperationResult {
    bool success = false;
    std::string message;

    /// 用途：将操作结果用于条件判断。输出：success；无状态修改和失败。
    explicit operator bool() const { return success; }
};

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

struct Squad {
    std::string name;
    std::vector<Character> members;
    std::size_t leaderIndex = 0;
    int cohesion = 50;
};

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
