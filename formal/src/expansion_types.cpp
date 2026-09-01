#include "tribe/expansion_types.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <utility>

namespace tribe {
namespace {

std::size_t attributeIndex(const Attribute attribute) {
    return static_cast<std::size_t>(attribute);
}

std::size_t equipmentIndex(const EquipmentSlot slot) {
    return static_cast<std::size_t>(slot);
}

bool validAttribute(const Attribute attribute) {
    return attributeIndex(attribute) < kAttributeCount;
}

bool validEquipmentSlot(const EquipmentSlot slot) {
    return equipmentIndex(slot) < kEquipmentSlotCount;
}

const std::array<Attribute, kAttributeCount>& prioritiesFor(const Occupation occupation) {
    static const std::array<Attribute, kAttributeCount> hunter{{
        Attribute::Perception, Attribute::Survival, Attribute::Agility, Attribute::Endurance,
        Attribute::Willpower, Attribute::Strength, Attribute::Leadership, Attribute::Diplomacy}};
    static const std::array<Attribute, kAttributeCount> warrior{{
        Attribute::Strength, Attribute::Endurance, Attribute::Willpower, Attribute::Leadership,
        Attribute::Agility, Attribute::Perception, Attribute::Survival, Attribute::Diplomacy}};
    static const std::array<Attribute, kAttributeCount> scout{{
        Attribute::Agility, Attribute::Perception, Attribute::Survival, Attribute::Endurance,
        Attribute::Willpower, Attribute::Strength, Attribute::Diplomacy, Attribute::Leadership}};
    static const std::array<Attribute, kAttributeCount> healer{{
        Attribute::Survival, Attribute::Perception, Attribute::Willpower, Attribute::Diplomacy,
        Attribute::Endurance, Attribute::Agility, Attribute::Leadership, Attribute::Strength}};
    static const std::array<Attribute, kAttributeCount> crafter{{
        Attribute::Survival, Attribute::Perception, Attribute::Endurance, Attribute::Willpower,
        Attribute::Strength, Attribute::Agility, Attribute::Leadership, Attribute::Diplomacy}};
    static const std::array<Attribute, kAttributeCount> envoy{{
        Attribute::Diplomacy, Attribute::Leadership, Attribute::Willpower, Attribute::Perception,
        Attribute::Agility, Attribute::Endurance, Attribute::Survival, Attribute::Strength}};

    switch (occupation) {
    case Occupation::Hunter: return hunter;
    case Occupation::Warrior: return warrior;
    case Occupation::Scout: return scout;
    case Occupation::Healer: return healer;
    case Occupation::Crafter: return crafter;
    case Occupation::Envoy: return envoy;
    }
    return hunter;
}

OperationResult accepted(std::string message) {
    return {true, std::move(message)};
}

OperationResult rejected(std::string message) {
    return {false, std::move(message)};
}

} // namespace

Attributes::Attributes() = default;

Attributes::Attributes(const int initialValue) {
    values.fill(initialValue);
}

int& Attributes::operator[](const Attribute attribute) {
    return values.at(attributeIndex(attribute));
}

int Attributes::operator[](const Attribute attribute) const {
    return values.at(attributeIndex(attribute));
}

Character::Character(std::string characterName, const Occupation characterOccupation)
    : name(std::move(characterName)), occupation(characterOccupation) {}

Inventory::Inventory(const int weightLimit, const int slotLimit)
    : weightLimit_(std::max(0, weightLimit)), slotLimit_(std::max(0, slotLimit)) {}

int Inventory::usedWeight() const {
    return std::accumulate(items_.begin(), items_.end(), 0,
        [](const int total, const Item& item) { return total + item.weight; });
}

int Inventory::usedSlots() const {
    return std::accumulate(items_.begin(), items_.end(), 0,
        [](const int total, const Item& item) { return total + item.slotCount; });
}

OperationResult Inventory::pickupFree(Item item) {
    if (item.id.empty() || item.name.empty()) return rejected("物品必须有标识和名称。");
    if (item.weight < 0 || item.slotCount <= 0) return rejected("物品重量或格数不合法。");
    if (item.weight > weightLimit_ - usedWeight()) return rejected("背包重量不足，物品未加入。");
    if (item.slotCount > slotLimit_ - usedSlots()) return rejected("背包格数不足，物品未加入。");

    items_.push_back(std::move(item));
    return accepted("免费拾取成功。");
}

OperationResult Inventory::take(const std::string_view itemId, Item& item) {
    const auto found = std::find_if(items_.begin(), items_.end(),
        [&](const Item& candidate) { return candidate.id == itemId; });
    if (found == items_.end()) return rejected("背包中没有该物品。");

    item = std::move(*found);
    items_.erase(found);
    return accepted("物品已从背包取出。");
}

OperationResult allocateAttribute(Character& character, const Attribute attribute, const int points) {
    if (!validAttribute(attribute)) return rejected("属性编号不合法。");
    if (points <= 0) return rejected("加点数必须为正数。");
    if (character.growthPoints < points) return rejected("成长点不足。");
    if (character.attributes[attribute] > kMaximumAttribute - points) {
        return rejected("属性不能超过上限。");
    }

    character.attributes[attribute] += points;
    character.growthPoints -= points;
    return accepted("属性加点成功。");
}

int experienceForNextLevel(const int level) {
    if (level <= 0) return 100;
    const long long required = 100LL + static_cast<long long>(level - 1) * 50LL;
    return static_cast<int>(std::min<long long>(required, std::numeric_limits<int>::max()));
}

OperationResult gainExperience(Character& character, const int amount) {
    if (amount <= 0) return rejected("经验值必须为正数。");
    if (character.level <= 0 || character.experience < 0) return rejected("角色等级或经验状态不合法。");

    const long long combined = static_cast<long long>(character.experience) + amount;
    if (combined > std::numeric_limits<int>::max()) return rejected("经验值超出允许范围。");

    int experience = static_cast<int>(combined);
    int level = character.level;
    int gainedLevels = 0;
    while (experience >= experienceForNextLevel(level)) {
        if (level == std::numeric_limits<int>::max()) return rejected("角色等级已达到允许上限。");
        experience -= experienceForNextLevel(level);
        ++level;
        ++gainedLevels;
    }

    character.experience = experience;
    character.level = level;
    character.growthPoints += gainedLevels;
    if (gainedLevels > 0) {
        character.life = std::min(maximumLife(character), character.life + gainedLevels * 10);
        character.loyalty = std::min(100, character.loyalty + gainedLevels);
    }
    return accepted(gainedLevels > 0 ? "获得经验并升级。" : "获得经验。");
}

OperationResult recommendAttributes(Character& character) {
    if (character.growthPoints <= 0) return rejected("没有可分配的成长点。");

    Character candidate = character;
    const auto& priorities = prioritiesFor(candidate.occupation);
    int allocated = 0;
    bool progress = true;
    while (candidate.growthPoints > 0 && progress) {
        progress = false;
        for (const Attribute attribute : priorities) {
            if (candidate.growthPoints == 0) break;
            if (candidate.attributes[attribute] >= kMaximumAttribute) continue;
            ++candidate.attributes[attribute];
            --candidate.growthPoints;
            ++allocated;
            progress = true;
        }
    }
    if (allocated == 0) return rejected("所有属性均已达到上限。");

    character = std::move(candidate);
    return accepted("已按职业推荐分配成长点。");
}

OperationResult equipItem(Character& character, const EquipmentSlot slot, const Item& item) {
    if (!validEquipmentSlot(slot)) return rejected("装备栏编号不合法。");
    if (item.id.empty() || item.name.empty()) return rejected("装备必须有标识和名称。");
    if (!item.equipmentSlot || *item.equipmentSlot != slot) return rejected("物品与装备栏不匹配。");
    if (item.condition == ItemCondition::Scrapped) return rejected("报废物品不能装备。");

    character.equipment[equipmentIndex(slot)] = item;
    return accepted("装备成功。");
}

Attributes effectiveAttributes(const Character& character) {
    Attributes result = character.attributes;
    for (const auto& equipped : character.equipment) {
        if (!equipped || equipped->condition == ItemCondition::Scrapped) continue;
        const int divisor = equipped->condition == ItemCondition::Damaged ? 2 : 1;
        for (std::size_t index = 0; index < kAttributeCount; ++index) {
            result.values[index] += equipped->bonuses.values[index] / divisor;
        }
    }
    return result;
}

int maximumLife(const Character& character) {
    const int endurance = std::max(kMinimumAttribute, character.attributes[Attribute::Endurance]);
    const long long maximum = 50LL + static_cast<long long>(endurance) * 10LL
        + static_cast<long long>(std::max(1, character.level)) * 2LL;
    return static_cast<int>(std::min<long long>(maximum, std::numeric_limits<int>::max()));
}

OperationResult rest(Character& character, const int lifeRecovery, const int fatigueRecovery) {
    if (lifeRecovery < 0 || fatigueRecovery < 0) return rejected("休整恢复量不能为负数。");
    if (lifeRecovery == 0 && fatigueRecovery == 0) return rejected("休整至少需要一种恢复效果。");

    const int recoveredLife = std::min(maximumLife(character), character.life + lifeRecovery);
    const int recoveredFatigue = std::max(0, character.fatigue - fatigueRecovery);
    if (recoveredLife == character.life && recoveredFatigue == character.fatigue) {
        return rejected("角色当前无需休整。");
    }

    character.life = recoveredLife;
    character.fatigue = recoveredFatigue;
    return accepted("休整完成。");
}

OperationResult validateSquad(const Squad& squad) {
    if (squad.name.empty()) return rejected("小队必须有名称。");
    if (squad.members.size() < kMinimumSquadSize || squad.members.size() > kMaximumSquadSize) {
        return rejected("小队人数必须为2至8人。");
    }
    if (squad.leaderIndex >= squad.members.size()) return rejected("小队长不在成员列表中。");
    if (squad.cohesion < 0 || squad.cohesion > 100) return rejected("小队凝聚力必须在0至100之间。");

    std::unordered_set<std::string> names;
    for (const auto& member : squad.members) {
        if (member.name.empty()) return rejected("小队成员必须有名称。");
        if (!names.insert(member.name).second) return rejected("小队成员名称不能重复。");
    }
    return accepted("小队配置合法。");
}

OperationResult validateArmy(const Army& army) {
    if (army.commanderName.empty()) return rejected("军队必须指定统帅。");
    if (army.warriors < 0 || army.militia < 0) return rejected("战士和民兵数量不能为负数。");
    if (army.warriors == 0 && army.militia == 0) return rejected("军队至少需要一名战士或民兵。");
    return accepted("军队配置合法。");
}

} // namespace tribe
