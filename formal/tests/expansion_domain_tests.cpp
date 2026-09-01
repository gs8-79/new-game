#include "test_harness.hpp"

#include "tribe/expansion_types.hpp"

#include <cstddef>

namespace {

tribe::Item makeEquipment(const std::string& id, const tribe::EquipmentSlot slot) {
    tribe::Item item;
    item.id = id;
    item.name = id;
    item.weight = 2;
    item.slotCount = 1;
    item.equipmentSlot = slot;
    return item;
}

tribe::Character makeCharacter(const std::string& name, const tribe::Occupation occupation) {
    return tribe::Character{name, occupation};
}

} // namespace

TEST_CASE("expansion attributes contain eight values and allocation is atomic") {
    REQUIRE(tribe::kAttributeCount == 8);
    tribe::Character character{"燧石", tribe::Occupation::Warrior};
    character.growthPoints = 3;

    REQUIRE(tribe::allocateAttribute(character, tribe::Attribute::Strength, 2));
    REQUIRE(character.attributes[tribe::Attribute::Strength] == 7);
    REQUIRE(character.growthPoints == 1);

    const int beforeStrength = character.attributes[tribe::Attribute::Strength];
    const int beforePoints = character.growthPoints;
    REQUIRE(!tribe::allocateAttribute(character, tribe::Attribute::Strength, 2));
    REQUIRE(character.attributes[tribe::Attribute::Strength] == beforeStrength);
    REQUIRE(character.growthPoints == beforePoints);
}

TEST_CASE("expansion experience can grant multiple levels and growth points") {
    tribe::Character character{"白榆", tribe::Occupation::Healer};
    const int beforeLife = character.life;
    REQUIRE(tribe::gainExperience(character, 260));
    REQUIRE(character.level == 3);
    REQUIRE(character.experience == 10);
    REQUIRE(character.growthPoints == 2);
    REQUIRE(character.life > beforeLife);
}

TEST_CASE("expansion equipment uses the locked eight slot layout") {
    REQUIRE(tribe::kEquipmentSlotCount == 8);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::MainHand) == 0);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::OffHand) == 1);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::Head) == 2);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::Body) == 3);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::Hands) == 4);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::LegsFeet) == 5);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::Tool) == 6);
    REQUIRE(static_cast<std::size_t>(tribe::EquipmentSlot::Accessory) == 7);
}

TEST_CASE("expansion recommendation spends points using occupation priorities") {
    tribe::Character character{"岩槌", tribe::Occupation::Crafter};
    character.growthPoints = 3;
    REQUIRE(tribe::recommendAttributes(character));
    REQUIRE(character.growthPoints == 0);
    REQUIRE(character.attributes[tribe::Attribute::Survival] == 6);
    REQUIRE(character.attributes[tribe::Attribute::Perception] == 6);
    REQUIRE(character.attributes[tribe::Attribute::Endurance] == 6);
}

TEST_CASE("expansion equipment validates slots and condition before mutation") {
    tribe::Character character{"河矛", tribe::Occupation::Warrior};
    auto spear = makeEquipment("燧石长矛", tribe::EquipmentSlot::MainHand);
    spear.bonuses[tribe::Attribute::Strength] = 4;

    REQUIRE(!tribe::equipItem(character, tribe::EquipmentSlot::OffHand, spear));
    REQUIRE(!character.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::OffHand)]);
    REQUIRE(tribe::equipItem(character, tribe::EquipmentSlot::MainHand, spear));
    REQUIRE(character.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]->id == spear.id);
    REQUIRE(tribe::effectiveAttributes(character)[tribe::Attribute::Strength] == 9);

    auto scrap = makeEquipment("断裂短弓", tribe::EquipmentSlot::MainHand);
    scrap.condition = tribe::ItemCondition::Scrapped;
    REQUIRE(!tribe::equipItem(character, tribe::EquipmentSlot::MainHand, scrap));
    REQUIRE(character.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::MainHand)]->id == spear.id);
}

TEST_CASE("expansion damaged equipment provides reduced bonuses") {
    tribe::Character character{"苍眼", tribe::Occupation::Scout};
    auto boots = makeEquipment("旧皮靴", tribe::EquipmentSlot::LegsFeet);
    boots.condition = tribe::ItemCondition::Damaged;
    boots.bonuses[tribe::Attribute::Agility] = 3;
    REQUIRE(tribe::equipItem(character, tribe::EquipmentSlot::LegsFeet, boots));
    REQUIRE(tribe::effectiveAttributes(character)[tribe::Attribute::Agility] == 6);
}

TEST_CASE("expansion inventory pickup is free and capacity failures are atomic") {
    tribe::Inventory inventory{5, 2};
    auto spear = makeEquipment("spear", tribe::EquipmentSlot::MainHand);
    spear.weight = 3;
    REQUIRE(inventory.pickupFree(spear));
    REQUIRE(inventory.items().size() == 1);
    REQUIRE(inventory.usedWeight() == 3);
    REQUIRE(inventory.usedSlots() == 1);

    auto armor = makeEquipment("armor", tribe::EquipmentSlot::Body);
    armor.weight = 3;
    armor.slotCount = 2;
    const auto beforeSize = inventory.items().size();
    const int beforeWeight = inventory.usedWeight();
    const int beforeSlots = inventory.usedSlots();
    REQUIRE(!inventory.pickupFree(armor));
    REQUIRE(inventory.items().size() == beforeSize);
    REQUIRE(inventory.usedWeight() == beforeWeight);
    REQUIRE(inventory.usedSlots() == beforeSlots);
}

TEST_CASE("expansion rest clamps life and fatigue and rejects invalid recovery") {
    tribe::Character character{"芦风", tribe::Occupation::Hunter};
    character.life = 70;
    character.fatigue = 30;
    REQUIRE(tribe::rest(character, 100, 12));
    REQUIRE(character.life == tribe::maximumLife(character));
    REQUIRE(character.fatigue == 18);

    const int beforeLife = character.life;
    const int beforeFatigue = character.fatigue;
    REQUIRE(!tribe::rest(character, -1, 10));
    REQUIRE(character.life == beforeLife);
    REQUIRE(character.fatigue == beforeFatigue);
}

TEST_CASE("expansion squad requires two to eight unique named members and a leader") {
    tribe::Squad squad;
    squad.name = "晨火队";
    squad.members = {
        makeCharacter("青枝", tribe::Occupation::Envoy),
        makeCharacter("石刃", tribe::Occupation::Warrior)};
    squad.leaderIndex = 0;
    squad.residentMission = tribe::ResidentMission::Patrol;
    squad.cohesion = 75;
    REQUIRE(tribe::validateSquad(squad));

    squad.leaderIndex = 2;
    REQUIRE(!tribe::validateSquad(squad));
    squad.leaderIndex = 0;
    squad.members[1].name = squad.members[0].name;
    REQUIRE(!tribe::validateSquad(squad));
}

TEST_CASE("expansion squad rejects sizes outside the two to eight range") {
    tribe::Squad squad;
    squad.name = "孤行者";
    squad.members.push_back(makeCharacter("独行", tribe::Occupation::Scout));
    REQUIRE(!tribe::validateSquad(squad));

    for (int index = 0; index < 8; ++index) {
        squad.members.push_back(makeCharacter("成员" + std::to_string(index), tribe::Occupation::Hunter));
    }
    REQUIRE(squad.members.size() == 9);
    REQUIRE(!tribe::validateSquad(squad));
}

TEST_CASE("expansion army requires a commander and nonnegative personnel") {
    tribe::Army army{"石牙", 12, 20};
    REQUIRE(tribe::validateArmy(army));
    army.commanderName.clear();
    REQUIRE(!tribe::validateArmy(army));
    army.commanderName = "石牙";
    army.warriors = -1;
    REQUIRE(!tribe::validateArmy(army));
}
