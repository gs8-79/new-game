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
    Attributes();
    explicit Attributes(int initialValue);

    int& operator[](Attribute attribute);
    int operator[](Attribute attribute) const;

    std::array<int, kAttributeCount> values{};
};

enum class Occupation { Hunter = 0, Warrior, Scout, Healer, Crafter, Envoy };

enum class EquipmentSlot {
    MainHand = 0,
    OffHand,
    Head,
    Body,
    Hands,
    LegsFeet,
    Tool,
    Accessory,
    Count
};

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
    Character() = default;
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

    explicit operator bool() const { return success; }
};

class Inventory {
public:
    explicit Inventory(int weightLimit = 50, int slotLimit = 16);

    int weightLimit() const { return weightLimit_; }
    int slotLimit() const { return slotLimit_; }
    int usedWeight() const;
    int usedSlots() const;
    const std::vector<Item>& items() const { return items_; }

    OperationResult pickupFree(Item item);
    OperationResult take(std::string_view itemId, Item& item);

private:
    int weightLimit_ = 0;
    int slotLimit_ = 0;
    std::vector<Item> items_;
};

enum class ResidentMission { None = 0, Gather, Patrol, Explore, Escort, Train };

struct Squad {
    std::string name;
    std::vector<Character> members;
    std::size_t leaderIndex = 0;
    ResidentMission residentMission = ResidentMission::None;
    int cohesion = 50;
};

struct Army {
    std::string commanderName;
    int warriors = 0;
    int militia = 0;
};

constexpr std::size_t kMinimumSquadSize = 2;
constexpr std::size_t kMaximumSquadSize = 8;

OperationResult allocateAttribute(Character& character, Attribute attribute, int points);
OperationResult gainExperience(Character& character, int amount);
OperationResult recommendAttributes(Character& character);
OperationResult equipItem(Character& character, EquipmentSlot slot, const Item& item);
OperationResult rest(Character& character, int lifeRecovery, int fatigueRecovery);

int experienceForNextLevel(int level);
int maximumLife(const Character& character);
Attributes effectiveAttributes(const Character& character);
OperationResult validateSquad(const Squad& squad);
OperationResult validateArmy(const Army& army);

} // namespace tribe
