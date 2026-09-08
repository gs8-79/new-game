#include "tribe/save_repository.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tribe {
namespace {

constexpr std::array<char, 8> kMagic{{'T', 'R', 'I', 'B', 'E', 'S', 'A', 'V'}};
constexpr std::size_t kMaximumSaveBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumStringBytes = 1024U * 1024U;
constexpr std::size_t kMaximumProfileFactions = 3U;
constexpr std::size_t kMaximumRoster = 64U;
constexpr std::size_t kMaximumSquads = 8U;
constexpr std::size_t kMaximumSquadMembers = 8U;
constexpr std::size_t kMaximumInventoryItems = 64U;
constexpr std::size_t kMaximumLeadershipEntries = 256U;
constexpr std::size_t kMaximumChronicleEntries = 200U;

class BufferWriter {
public:
    void writeByte(const std::uint8_t value) { data_.push_back(static_cast<char>(value)); }

    void writeU32(const std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            writeByte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void writeInt(const int value) {
        static_assert(sizeof(int) == sizeof(std::int32_t), "Campaign saves require 32-bit int fields.");
        const auto signedValue = static_cast<std::int32_t>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &signedValue, sizeof(bits));
        writeU32(bits);
    }

    void writeBool(const bool value) { writeByte(value ? 1U : 0U); }

    void writeRaw(const char* data, const std::size_t size) { data_.append(data, size); }

    void writeString(const std::string_view value) {
        if (value.size() > kMaximumStringBytes
            || value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            valid_ = false;
            return;
        }
        writeU32(static_cast<std::uint32_t>(value.size()));
        writeRaw(value.data(), value.size());
    }

    bool valid() const { return valid_ && data_.size() <= kMaximumSaveBytes; }
    const std::string& data() const { return data_; }
    std::string take() { return std::move(data_); }

private:
    std::string data_;
    bool valid_ = true;
};

class BufferReader {
public:
    explicit BufferReader(const std::string_view data) : data_(data) {}

    bool readByte(std::uint8_t& value) {
        if (offset_ >= data_.size()) return false;
        value = static_cast<std::uint8_t>(static_cast<unsigned char>(data_[offset_++]));
        return true;
    }

    bool readU32(std::uint32_t& value) {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!readByte(byte)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool readInt(int& value) {
        static_assert(sizeof(int) == sizeof(std::int32_t), "Campaign saves require 32-bit int fields.");
        std::uint32_t bits = 0;
        if (!readU32(bits)) return false;
        std::int32_t signedValue = 0;
        std::memcpy(&signedValue, &bits, sizeof(bits));
        value = static_cast<int>(signedValue);
        return true;
    }

    bool readBool(bool& value) {
        std::uint8_t raw = 0;
        if (!readByte(raw) || raw > 1U) return false;
        value = raw != 0U;
        return true;
    }

    bool readBytes(const std::size_t size, std::string_view& value) {
        if (size > data_.size() - offset_) return false;
        value = data_.substr(offset_, size);
        offset_ += size;
        return true;
    }

    bool readString(std::string& value) {
        std::uint32_t size = 0;
        if (!readU32(size) || size > kMaximumStringBytes) return false;
        std::string_view bytes;
        if (!readBytes(size, bytes)) return false;
        value.assign(bytes.data(), bytes.size());
        return true;
    }

    bool finished() const { return offset_ == data_.size(); }

private:
    std::string_view data_;
    std::size_t offset_ = 0U;
};

std::uint32_t checksum(const std::string_view data) {
    std::uint32_t value = 2166136261U;
    for (const unsigned char byte : data) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

template <typename Enum>
void writeEnum(BufferWriter& writer, const Enum value) {
    writer.writeInt(static_cast<int>(value));
}

template <typename Enum>
bool readEnum(BufferReader& reader, Enum& value, const Enum minimum, const Enum maximum) {
    int raw = 0;
    if (!reader.readInt(raw) || raw < static_cast<int>(minimum) || raw > static_cast<int>(maximum)) {
        return false;
    }
    value = static_cast<Enum>(raw);
    return true;
}

template <std::size_t Size>
void writeBoolArray(BufferWriter& writer, const std::array<bool, Size>& values) {
    for (const bool value : values) writer.writeBool(value);
}

template <std::size_t Size>
bool readBoolArray(BufferReader& reader, std::array<bool, Size>& values) {
    for (std::size_t index = 0; index < Size; ++index) {
        bool value = false;
        if (!reader.readBool(value)) return false;
        values[index] = value;
    }
    return true;
}

void writeAttributes(BufferWriter& writer, const Attributes& attributes) {
    for (const int value : attributes.values) writer.writeInt(value);
}

bool readAttributes(BufferReader& reader, Attributes& attributes) {
    for (int& value : attributes.values) {
        if (!reader.readInt(value)) return false;
    }
    return true;
}

void writeItem(BufferWriter& writer, const Item& item) {
    writer.writeString(item.id);
    writer.writeString(item.name);
    writeEnum(writer, item.quality);
    writeEnum(writer, item.condition);
    writer.writeInt(item.weight);
    writer.writeInt(item.slotCount);
    writer.writeBool(item.equipmentSlot.has_value());
    if (item.equipmentSlot) writeEnum(writer, *item.equipmentSlot);
    writeAttributes(writer, item.bonuses);
}

bool readItem(BufferReader& reader, Item& item) {
    bool hasSlot = false;
    if (!reader.readString(item.id) || !reader.readString(item.name)
        || !readEnum(reader, item.quality, ItemQuality::Crude, ItemQuality::Legendary)
        || !readEnum(reader, item.condition, ItemCondition::Intact, ItemCondition::Scrapped)
        || !reader.readInt(item.weight) || !reader.readInt(item.slotCount)
        || !reader.readBool(hasSlot)) {
        return false;
    }
    if (hasSlot) {
        EquipmentSlot slot = EquipmentSlot::MainHand;
        if (!readEnum(reader, slot, EquipmentSlot::MainHand, EquipmentSlot::Accessory)) return false;
        item.equipmentSlot = slot;
    } else {
        item.equipmentSlot.reset();
    }
    if (!readAttributes(reader, item.bonuses)) return false;
    return !item.id.empty() && !item.name.empty() && item.weight >= 0 && item.slotCount > 0;
}

void writeCharacter(BufferWriter& writer, const Character& character) {
    writer.writeString(character.name);
    writeEnum(writer, character.occupation);
    writer.writeInt(character.level);
    writer.writeInt(character.experience);
    writer.writeInt(character.growthPoints);
    writer.writeInt(character.life);
    writer.writeInt(character.fatigue);
    writer.writeInt(character.loyalty);
    writeAttributes(writer, character.attributes);
    for (const auto& equipped : character.equipment) {
        writer.writeBool(equipped.has_value());
        if (equipped) writeItem(writer, *equipped);
    }
}

bool readCharacter(BufferReader& reader, Character& character) {
    if (!reader.readString(character.name)
        || !readEnum(reader, character.occupation, Occupation::Hunter, Occupation::Envoy)
        || !reader.readInt(character.level) || !reader.readInt(character.experience)
        || !reader.readInt(character.growthPoints) || !reader.readInt(character.life)
        || !reader.readInt(character.fatigue) || !reader.readInt(character.loyalty)
        || !readAttributes(reader, character.attributes)) {
        return false;
    }
    if (character.name.empty() || character.level <= 0 || character.level > 1000000
        || character.experience < 0 || character.growthPoints < 0) {
        return false;
    }
    for (const int value : character.attributes.values) {
        if (value < kMinimumAttribute || value > kMaximumAttribute) return false;
    }
    for (std::size_t index = 0; index < character.equipment.size(); ++index) {
        bool hasItem = false;
        if (!reader.readBool(hasItem)) return false;
        if (!hasItem) {
            character.equipment[index].reset();
            continue;
        }
        Item item;
        if (!readItem(reader, item) || !item.equipmentSlot
            || static_cast<std::size_t>(*item.equipmentSlot) != index) {
            return false;
        }
        character.equipment[index] = std::move(item);
    }
    return true;
}

void writeInventory(BufferWriter& writer, const Inventory& inventory) {
    writer.writeInt(inventory.weightLimit());
    writer.writeInt(inventory.slotLimit());
    writer.writeU32(static_cast<std::uint32_t>(inventory.items().size()));
    for (const Item& item : inventory.items()) writeItem(writer, item);
}

bool readInventory(BufferReader& reader, Inventory& inventory) {
    int weightLimit = 0;
    int slotLimit = 0;
    std::uint32_t count = 0;
    if (!reader.readInt(weightLimit) || !reader.readInt(slotLimit) || !reader.readU32(count)
        || weightLimit < 0 || slotLimit < 0 || count > kMaximumInventoryItems) {
        return false;
    }
    Inventory parsed{weightLimit, slotLimit};
    for (std::uint32_t index = 0; index < count; ++index) {
        Item item;
        if (!readItem(reader, item) || !parsed.pickupFree(std::move(item))) return false;
    }
    inventory = std::move(parsed);
    return true;
}

void writeExpansionSquad(BufferWriter& writer, const Squad& squad) {
    writer.writeString(squad.name);
    writer.writeU32(static_cast<std::uint32_t>(squad.members.size()));
    for (const Character& member : squad.members) writeCharacter(writer, member);
    writer.writeU32(static_cast<std::uint32_t>(squad.leaderIndex));
    writer.writeInt(squad.cohesion);
}

bool readExpansionSquad(BufferReader& reader, Squad& squad) {
    std::uint32_t count = 0;
    std::uint32_t leaderIndex = 0;
    if (!reader.readString(squad.name) || !reader.readU32(count)
        || count < kMinimumSquadSize || count > kMaximumSquadSize) {
        return false;
    }
    squad.members.clear();
    squad.members.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        Character character;
        if (!readCharacter(reader, character)) return false;
        squad.members.push_back(std::move(character));
    }
    if (!reader.readU32(leaderIndex) || leaderIndex >= count || !reader.readInt(squad.cohesion)) {
        return false;
    }
    squad.leaderIndex = leaderIndex;
    return true;
}

void writeExpansionState(BufferWriter& writer, const ExpansionState& state) {
    writer.writeU32(state.seed);
    writer.writeInt(state.turn);
    writeEnum(writer, state.phase);
    writeExpansionSquad(writer, state.squad);
    writeInventory(writer, state.backpack);
    writer.writeBool(state.settled);
    writer.writeInt(state.worldLocation);
    writeBoolArray(writer, state.worldDiscovered);
    writeBoolArray(writer, state.outposts);
    writer.writeInt(state.cargoFood);
    writer.writeInt(state.cargoWood);
    writer.writeInt(state.cargoStone);
    writer.writeInt(state.cargoHerbs);
    writer.writeInt(state.cargoHides);
    writer.writeInt(state.harvestActions);
    writer.writeInt(state.cargoCapacity);
    writer.writeInt(state.foodGatherBonus);
    writer.writeInt(state.herbGatherBonus);
    writer.writeInt(state.assignedResource);
    writer.writeInt(state.crewSize);
    writer.writeInt(state.encounterLife);
    writer.writeBool(state.encounterDefeated);
}

bool readExpansionState(BufferReader& reader, ExpansionState& state) {
    if (!reader.readU32(state.seed) || !reader.readInt(state.turn)
        || !readEnum(reader, state.phase, ExpansionPhase::Exploring, ExpansionPhase::Settled)
        || !readExpansionSquad(reader, state.squad) || !readInventory(reader, state.backpack)
        || !reader.readBool(state.settled)
        || !reader.readInt(state.worldLocation) || !readBoolArray(reader, state.worldDiscovered)
        || !readBoolArray(reader, state.outposts) || !reader.readInt(state.cargoFood)
        || !reader.readInt(state.cargoWood) || !reader.readInt(state.cargoStone)
        || !reader.readInt(state.cargoHerbs) || !reader.readInt(state.cargoHides) || !reader.readInt(state.harvestActions)
        || !reader.readInt(state.cargoCapacity) || !reader.readInt(state.foodGatherBonus)
        || !reader.readInt(state.herbGatherBonus) || !reader.readInt(state.assignedResource) || !reader.readInt(state.crewSize)
        || !reader.readInt(state.encounterLife) || !reader.readBool(state.encounterDefeated)) {
        return false;
    }
    return static_cast<bool>(ExpansionGame::validateState(state));
}

void writeFaction(BufferWriter& writer, const FactionState& faction) {
    writer.writeString(faction.name);
    writer.writeInt(faction.influence);
    writer.writeInt(faction.satisfaction);
    writer.writeString(faction.demand);
    writer.writeString(faction.candidate);
    writeEnum(writer, faction.crisis);
}

bool readFaction(BufferReader& reader, FactionState& faction) {
    return reader.readString(faction.name) && reader.readInt(faction.influence)
        && reader.readInt(faction.satisfaction) && reader.readString(faction.demand)
        && reader.readString(faction.candidate)
        && readEnum(reader, faction.crisis, FactionCrisis::Calm, FactionCrisis::Coup);
}

void writeTribeProfile(BufferWriter& writer, const TribeProfile& profile) {
    writeEnum(writer, profile.id);
    writer.writeString(profile.name);
    writer.writeString(profile.leader);
    writer.writeString(profile.actingLeader);
    writer.writeString(profile.successor);
    writer.writeString(profile.personality);
    writer.writeU32(static_cast<std::uint32_t>(profile.factions.size()));
    for (const FactionState& faction : profile.factions) writeFaction(writer, faction);
}

bool readTribeProfile(BufferReader& reader, TribeProfile& profile) {
    std::uint32_t count = 0;
    if (!readEnum(reader, profile.id, TribeId::Player, TribeId::Blackstone)
        || !reader.readString(profile.name) || !reader.readString(profile.leader)
        || !reader.readString(profile.actingLeader) || !reader.readString(profile.successor)
        || !reader.readString(profile.personality) || !reader.readU32(count)
        || count < 2U || count > kMaximumProfileFactions) {
        return false;
    }
    profile.factions.clear();
    profile.factions.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        FactionState faction;
        if (!readFaction(reader, faction)) return false;
        profile.factions.push_back(std::move(faction));
    }
    return true;
}

void writeRelation(BufferWriter& writer, const DiplomacyRelation& relation) {
    writer.writeInt(relation.relation);
    writer.writeInt(relation.trust);
    writer.writeInt(relation.fear);
    writer.writeInt(relation.tradeDependence);
    writer.writeBool(relation.atWar);
    writer.writeBool(relation.truce);
    writer.writeBool(relation.alliance);
    writer.writeBool(relation.marriage);
    writer.writeBool(relation.playerPaysTribute);
    writer.writeBool(relation.otherPaysTribute);
    writer.writeBool(relation.tradeRoute);
}

bool readRelation(BufferReader& reader, DiplomacyRelation& relation) {
    return reader.readInt(relation.relation) && reader.readInt(relation.trust)
        && reader.readInt(relation.fear) && reader.readInt(relation.tradeDependence)
        && reader.readBool(relation.atWar) && reader.readBool(relation.truce)
        && reader.readBool(relation.alliance) && reader.readBool(relation.marriage)
        && reader.readBool(relation.playerPaysTribute) && reader.readBool(relation.otherPaysTribute)
        && reader.readBool(relation.tradeRoute);
}

void writePermanentSquad(BufferWriter& writer, const PermanentSquad& squad) {
    writer.writeString(squad.name);
    writer.writeString(squad.captain);
    writer.writeU32(static_cast<std::uint32_t>(squad.members.size()));
    for (const std::string& member : squad.members) writer.writeString(member);
    writer.writeInt(squad.fatigue);
    writer.writeInt(squad.eliteExperience);
    writer.writeBool(squad.personallyDeployedThisSeason);
    writer.writeBool(squad.refusingOrders);
    writeEnum(writer, squad.station);
}

bool readPermanentSquad(BufferReader& reader, PermanentSquad& squad) {
    std::uint32_t count = 0;
    if (!reader.readString(squad.name) || !reader.readString(squad.captain)
        || !reader.readU32(count) || count < 2U || count > kMaximumSquadMembers) {
        return false;
    }
    squad.members.clear();
    squad.members.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string member;
        if (!reader.readString(member)) return false;
        squad.members.push_back(std::move(member));
    }
    return reader.readInt(squad.fatigue) && reader.readInt(squad.eliteExperience)
        && reader.readBool(squad.personallyDeployedThisSeason)
        && reader.readBool(squad.refusingOrders)
        && readEnum(reader, squad.station, WorldLocationId::Camp, WorldLocationId::CliffTradeRoad);
}

void writeWar(BufferWriter& writer, const WarState& war) {
    writer.writeBool(war.active);
    writeEnum(writer, war.enemy);
    writer.writeString(war.commander);
    writer.writeInt(war.warriors);
    writer.writeInt(war.militia);
    writer.writeInt(war.playerPower);
    writer.writeInt(war.enemyPower);
    writeEnum(writer, war.order);
    writer.writeBool(war.riskConfirmed);
    writer.writeInt(war.spearMilitia);
    writer.writeInt(war.shieldBearers);
    writer.writeInt(war.heavySpears);
    writer.writeU32(static_cast<std::uint32_t>(war.lockedEquipment.size()));
    for (const Item& item : war.lockedEquipment) writeItem(writer, item);
    writer.writeBool(war.defensive);
}

bool readWar(BufferReader& reader, WarState& war) {
    std::uint32_t count = 0;
    if (!reader.readBool(war.active) || !readEnum(reader, war.enemy, TribeId::Player, TribeId::Blackstone)
        || !reader.readString(war.commander) || !reader.readInt(war.warriors) || !reader.readInt(war.militia)
        || !reader.readInt(war.playerPower) || !reader.readInt(war.enemyPower)
        || !readEnum(reader, war.order, WarOrder::Advance, WarOrder::Retreat) || !reader.readBool(war.riskConfirmed)
        || !reader.readInt(war.spearMilitia) || !reader.readInt(war.shieldBearers) || !reader.readInt(war.heavySpears)
        || !reader.readU32(count) || count > kMaximumInventoryItems) return false;
    war.lockedEquipment.clear();
    for (std::uint32_t i = 0; i < count; ++i) { Item item; if (!readItem(reader, item)) return false; war.lockedEquipment.push_back(std::move(item)); }
    return reader.readBool(war.defensive);
}

void writeChronicle(BufferWriter& writer, const ChronicleEntry& entry) {
    writer.writeInt(entry.season);
    writer.writeInt(entry.importance);
    writer.writeString(entry.title);
    writer.writeString(entry.detail);
}

bool readChronicle(BufferReader& reader, ChronicleEntry& entry) {
    return reader.readInt(entry.season) && reader.readInt(entry.importance)
        && reader.readString(entry.title) && reader.readString(entry.detail);
}

void writeGameState(BufferWriter& writer, const GameState& state) {
    writeEnum(writer, state.mode);
    writeEnum(writer, state.phase);
    writer.writeU32(state.seed);
    writer.writeInt(state.season);
    writer.writeInt(state.seasonLimit);
    writer.writeInt(state.actionsLeft);
    writer.writeInt(state.population);
    writer.writeInt(state.food);
    writer.writeInt(state.wood);
    writer.writeInt(state.stone);
    writer.writeInt(state.herbs);
    writer.writeInt(state.hides);
    writer.writeInt(state.warriors);
    writer.writeInt(state.morale);
    writer.writeInt(state.campDurability);
    writer.writeInt(state.stability);
    writer.writeInt(state.shells);
    writer.writeInt(state.tradeCount);
    writer.writeInt(state.warsWon);
    writer.writeInt(state.warsLost);
    writer.writeInt(state.missionCount);
    writer.writeInt(state.missionDeaths);
    writer.writeInt(state.highestLevel);
    writer.writeString(state.tribeName);
    writer.writeString(state.leaderName);
    writer.writeString(state.actingLeaderName);
    writer.writeString(state.leaderFocus);
    writeBoolArray(writer, state.discovered);
    writeBoolArray(writer, state.outposts);
    writeBoolArray(writer, state.buildings);
    writeBoolArray(writer, state.technologies);
    for (const TribeProfile& profile : state.tribes) writeTribeProfile(writer, profile);
    for (const DiplomacyRelation& relation : state.relations) writeRelation(writer, relation);
    for (const FactionState& faction : state.playerFactions) writeFaction(writer, faction);
    writeBoolArray(writer, state.tradePartners);

    writer.writeU32(static_cast<std::uint32_t>(state.roster.size()));
    for (const Character& character : state.roster) writeCharacter(writer, character);
    writer.writeU32(static_cast<std::uint32_t>(state.squads.size()));
    for (const PermanentSquad& squad : state.squads) writePermanentSquad(writer, squad);

    writer.writeBool(state.activeMission.has_value());
    if (state.activeMission) writeExpansionState(writer, *state.activeMission);
    writeWar(writer, state.war);
    writer.writeBool(state.currencyUnlocked);
    writer.writeBool(state.longModeFinalShown);
    writeEnum(writer, state.ending);

    writer.writeU32(static_cast<std::uint32_t>(state.leadershipHistory.size()));
    for (const std::string& entry : state.leadershipHistory) writer.writeString(entry);
    writer.writeU32(static_cast<std::uint32_t>(state.chronicle.size()));
    for (const ChronicleEntry& entry : state.chronicle) writeChronicle(writer, entry);

    writer.writeInt(state.workforce.foodCrew); writer.writeInt(state.workforce.woodCrew); writer.writeInt(state.workforce.stoneCrew); writer.writeInt(state.workforce.herbCrew);
    writer.writeInt(state.workforce.crafters); writer.writeInt(state.workforce.healers); writer.writeInt(state.workforce.scouts); writer.writeInt(state.workforce.envoys); writer.writeInt(state.workforce.campGuards);
    writer.writeBool(state.pendingEvent.active); writer.writeString(state.pendingEvent.name); writer.writeString(state.pendingEvent.optionOne); writer.writeString(state.pendingEvent.optionTwo);
    writer.writeString(state.workshopSupervisor); writer.writeString(state.healerSupervisor);
    for (const int guard : state.workforce.outpostGuards) writer.writeInt(guard);
    for (const int idle : state.workforce.outpostIdleSeasons) writer.writeInt(idle);
    writer.writeU32(static_cast<std::uint32_t>(state.stockpile.size())); for (const Item& item : state.stockpile) writeItem(writer, item);
    for (const OccupationState& site : state.occupations) { writer.writeBool(site.occupied); writer.writeInt(site.garrison); writer.writeInt(site.unrest); }
}

bool readGameState(BufferReader& reader, GameState& state, std::string& error) {
    if (!readEnum(reader, state.mode, GameMode::Quick, GameMode::Long)
        || !readEnum(reader, state.phase, GamePhase::Managing, GamePhase::Sandbox)
        || !reader.readU32(state.seed) || !reader.readInt(state.season)
        || !reader.readInt(state.seasonLimit) || !reader.readInt(state.actionsLeft)
        || !reader.readInt(state.population) || !reader.readInt(state.food)
        || !reader.readInt(state.wood) || !reader.readInt(state.stone)
        || !reader.readInt(state.herbs) || !reader.readInt(state.hides) || !reader.readInt(state.warriors)
        || !reader.readInt(state.morale) || !reader.readInt(state.campDurability)
        || !reader.readInt(state.stability) || !reader.readInt(state.shells)
        || !reader.readInt(state.tradeCount) || !reader.readInt(state.warsWon)
        || !reader.readInt(state.warsLost) || !reader.readInt(state.missionCount)
        || !reader.readInt(state.missionDeaths) || !reader.readInt(state.highestLevel)
        || !reader.readString(state.tribeName)
        || !reader.readString(state.leaderName) || !reader.readString(state.actingLeaderName)
        || !reader.readString(state.leaderFocus) || !readBoolArray(reader, state.discovered)
        || !readBoolArray(reader, state.outposts)
        || !readBoolArray(reader, state.buildings) || !readBoolArray(reader, state.technologies)) {
        error = "存档基础字段损坏或不完整。";
        return false;
    }
    for (TribeProfile& profile : state.tribes) {
        if (!readTribeProfile(reader, profile)) {
            error = "存档的部落档案损坏。";
            return false;
        }
    }
    for (DiplomacyRelation& relation : state.relations) {
        if (!readRelation(reader, relation)) {
            error = "存档的外交关系损坏。";
            return false;
        }
    }
    for (FactionState& faction : state.playerFactions) {
        if (!readFaction(reader, faction)) {
            error = "存档的玩家派系损坏。";
            return false;
        }
    }
    if (!readBoolArray(reader, state.tradePartners)) {
        error = "存档的贸易伙伴字段损坏。";
        return false;
    }

    std::uint32_t rosterCount = 0;
    if (!reader.readU32(rosterCount) || rosterCount < 2U || rosterCount > kMaximumRoster) {
        error = "存档的角色数量无效。";
        return false;
    }
    state.roster.clear();
    state.roster.reserve(rosterCount);
    for (std::uint32_t index = 0; index < rosterCount; ++index) {
        Character character;
        if (!readCharacter(reader, character)) {
            error = "存档的角色字段损坏。";
            return false;
        }
        state.roster.push_back(std::move(character));
    }

    std::uint32_t squadCount = 0;
    if (!reader.readU32(squadCount) || squadCount == 0U || squadCount > kMaximumSquads) {
        error = "存档的永久小队数量无效。";
        return false;
    }
    state.squads.clear();
    state.squads.reserve(squadCount);
    for (std::uint32_t index = 0; index < squadCount; ++index) {
        PermanentSquad squad;
        if (!readPermanentSquad(reader, squad)) {
            error = "存档的永久小队字段损坏。";
            return false;
        }
        state.squads.push_back(std::move(squad));
    }

    bool hasMission = false;
    if (!reader.readBool(hasMission)) {
        error = "存档的任务标志损坏。";
        return false;
    }
    if (hasMission) {
        ExpansionState mission;
        if (!readExpansionState(reader, mission)) {
            error = "存档的活动任务损坏。";
            return false;
        }
        state.activeMission = std::move(mission);
    } else {
        state.activeMission.reset();
    }
    if (!readWar(reader, state.war) || !reader.readBool(state.currencyUnlocked)
        || !reader.readBool(state.longModeFinalShown)
        || !readEnum(reader, state.ending, GameEnding::None, GameEnding::Extinction)) {
        error = "存档的任务、战争或结局字段损坏。";
        return false;
    }

    std::uint32_t leadershipCount = 0;
    if (!reader.readU32(leadershipCount) || leadershipCount == 0U
        || leadershipCount > kMaximumLeadershipEntries) {
        error = "存档的首领历史数量无效。";
        return false;
    }
    state.leadershipHistory.clear();
    state.leadershipHistory.reserve(leadershipCount);
    for (std::uint32_t index = 0; index < leadershipCount; ++index) {
        std::string entry;
        if (!reader.readString(entry)) {
            error = "存档的首领历史损坏。";
            return false;
        }
        state.leadershipHistory.push_back(std::move(entry));
    }

    std::uint32_t chronicleCount = 0;
    if (!reader.readU32(chronicleCount) || chronicleCount == 0U
        || chronicleCount > kMaximumChronicleEntries) {
        error = "存档的编年史数量无效。";
        return false;
    }
    state.chronicle.clear();
    state.chronicle.reserve(chronicleCount);
    for (std::uint32_t index = 0; index < chronicleCount; ++index) {
        ChronicleEntry entry;
        if (!readChronicle(reader, entry)) {
            error = "存档的编年史字段损坏。";
            return false;
        }
        state.chronicle.push_back(std::move(entry));
    }

    if (!reader.readInt(state.workforce.foodCrew) || !reader.readInt(state.workforce.woodCrew) || !reader.readInt(state.workforce.stoneCrew) || !reader.readInt(state.workforce.herbCrew)
        || !reader.readInt(state.workforce.crafters) || !reader.readInt(state.workforce.healers) || !reader.readInt(state.workforce.scouts) || !reader.readInt(state.workforce.envoys) || !reader.readInt(state.workforce.campGuards)
        || !reader.readBool(state.pendingEvent.active) || !reader.readString(state.pendingEvent.name) || !reader.readString(state.pendingEvent.optionOne) || !reader.readString(state.pendingEvent.optionTwo)
        || !reader.readString(state.workshopSupervisor) || !reader.readString(state.healerSupervisor)) { error = "存档的当前玩法字段损坏。"; return false; }
    for (int& guard : state.workforce.outpostGuards) if (!reader.readInt(guard)) { error = "存档前哨守卫字段损坏。"; return false; }
    for (int& idle : state.workforce.outpostIdleSeasons) if (!reader.readInt(idle)) { error = "存档前哨维护字段损坏。"; return false; }
    std::uint32_t stockCount = 0; if (!reader.readU32(stockCount) || stockCount > kMaximumInventoryItems) { error = "存档仓库数量无效。"; return false; }
    state.stockpile.clear(); for (std::uint32_t i=0;i<stockCount;++i) { Item item; if(!readItem(reader,item)){error="存档仓库物品损坏。";return false;} state.stockpile.push_back(std::move(item)); }
    for (OccupationState& site : state.occupations) if (!reader.readBool(site.occupied) || !reader.readInt(site.garrison) || !reader.readInt(site.unrest)) { error="存档占领字段损坏。";return false; }
    if (!GameEngine::validateState(state, error)) return false;
    error.clear();
    return true;
}

bool serializeFile(const GameState& state, std::string& fileData, std::string& error) {
    BufferWriter payloadWriter;
    writeGameState(payloadWriter, state);
    if (!payloadWriter.valid()
        || payloadWriter.data().size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        error = "存档内容过大，无法安全写入。";
        return false;
    }
    const std::string payload = payloadWriter.data();
    BufferWriter fileWriter;
    fileWriter.writeRaw(kMagic.data(), kMagic.size());
    fileWriter.writeU32(static_cast<std::uint32_t>(kSaveVersion));
    fileWriter.writeU32(static_cast<std::uint32_t>(payload.size()));
    fileWriter.writeU32(checksum(payload));
    fileWriter.writeRaw(payload.data(), payload.size());
    if (!fileWriter.valid()) {
        error = "存档内容过大，无法安全写入。";
        return false;
    }
    fileData = fileWriter.take();
    error.clear();
    return true;
}

bool deserializeFile(const std::string_view fileData, GameState& candidate, std::string& error) {
    BufferReader fileReader(fileData);
    std::string_view magic;
    std::uint32_t version = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t storedChecksum = 0;
    if (!fileReader.readBytes(kMagic.size(), magic)
        || !std::equal(kMagic.begin(), kMagic.end(), magic.begin(), magic.end())) {
        error = "不是《燧火纪》游戏存档。";
        return false;
    }
    if (!fileReader.readU32(version) || version != static_cast<std::uint32_t>(kSaveVersion)) {
        error = "旧版本存档不支持，需要新开局；原文件未被修改。";
        return false;
    }
    if (!fileReader.readU32(payloadSize) || payloadSize > kMaximumSaveBytes
        || !fileReader.readU32(storedChecksum)) {
        error = "存档头损坏。";
        return false;
    }
    std::string_view payload;
    if (!fileReader.readBytes(payloadSize, payload) || !fileReader.finished()) {
        error = "存档长度与文件内容不一致。";
        return false;
    }
    if (checksum(payload) != storedChecksum) {
        error = "存档校验和不一致，文件可能已损坏。";
        return false;
    }
    BufferReader payloadReader(payload);
    GameState parsed;
    if (!readGameState(payloadReader, parsed, error)) return false;
    if (!payloadReader.finished()) {
        error = "存档包含未识别的尾部字段。";
        return false;
    }
    candidate = std::move(parsed);
    error.clear();
    return true;
}

enum class LoadFileStatus { Loaded, Missing, Invalid, Unavailable };

LoadFileStatus loadFile(const std::filesystem::path& path, GameState& candidate, std::string& error) {
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查文件" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (!exists) {
        error = "文件不存在：" + path.filename().string();
        return LoadFileStatus::Missing;
    }
    if (!std::filesystem::is_regular_file(path, code) || code) {
        error = "存档路径不是可读取的普通文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "无法读取文件大小：" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (size == 0U || size > kMaximumSaveBytes) {
        error = "存档为空或超过安全大小限制：" + path.filename().string();
        return LoadFileStatus::Invalid;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        error = "读取存档内容失败：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    return deserializeFile(data, candidate, error) ? LoadFileStatus::Loaded : LoadFileStatus::Invalid;
}

bool restoreRecoveredFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::filesystem::path recovery = destination;
    recovery += ".recover";
    std::error_code code;
    std::filesystem::remove(recovery, code);
    if (code) return false;
    std::filesystem::copy_file(source, recovery, std::filesystem::copy_options::none, code);
    if (code) return false;
    if (std::filesystem::exists(destination, code)) {
        if (code) return false;
        std::filesystem::remove(destination, code);
        if (code) return false;
    } else if (code) {
        return false;
    }
    std::filesystem::rename(recovery, destination, code);
    if (code) {
        std::filesystem::remove(recovery, code);
        return false;
    }
    return true;
}

bool validSlot(const SaveSlot slot) {
    const int value = static_cast<int>(slot);
    return value >= static_cast<int>(SaveSlot::Slot1)
        && value <= static_cast<int>(SaveSlot::Autosave);
}

std::string modificationTime(const std::filesystem::path& path) {
    std::error_code code;
    const auto fileTime = std::filesystem::last_write_time(path, code);
    if (code) return "时间未知";
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t raw = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &raw) != 0) return "时间未知";
#else
    if (localtime_r(&raw, &local) == nullptr) return "时间未知";
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M");
    return output.str();
}

SaveSummary summaryFor(const SaveSlot slot, const SaveStatus status,
    const std::filesystem::path& source, const GameState& state) {
    SaveSummary summary;
    summary.slot = slot;
    summary.status = status;
    summary.modifiedAt = modificationTime(source);
    summary.mode = GameEngine::modeName(state.mode);
    summary.phase = GameEngine::phaseName(state.phase);
    summary.tribeName = state.tribeName;
    summary.leaderName = state.actingLeaderName.empty() ? state.leaderName : state.actingLeaderName;
    summary.season = state.season;
    summary.seasonLimit = state.seasonLimit;
    summary.population = state.population;
    summary.food = state.food;
    summary.wood = state.wood;
    summary.stone = state.stone;
    summary.herbs = state.herbs;
    return summary;
}

} // namespace

SaveRepository::SaveRepository(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path SaveRepository::pathFor(const SaveSlot slot) const {
    switch (slot) {
    case SaveSlot::Slot1: return root_ / "slot1.sav";
    case SaveSlot::Slot2: return root_ / "slot2.sav";
    case SaveSlot::Slot3: return root_ / "slot3.sav";
    case SaveSlot::Slot4: return root_ / "slot4.sav";
    case SaveSlot::Slot5: return root_ / "slot5.sav";
    case SaveSlot::Slot6: return root_ / "slot6.sav";
    case SaveSlot::Autosave: return root_ / "autosave.sav";
    }
    return root_ / "invalid.sav";
}

bool SaveRepository::save(const GameState& state, const SaveSlot slot,
    std::string& error) const {
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    if (!GameEngine::validateState(state, error)) return false;

    std::string fileData;
    if (!serializeFile(state, fileData, error)) return false;
    GameState memoryRoundTrip;
    if (!deserializeFile(fileData, memoryRoundTrip, error)) {
        error = "存档写入前的解析校验失败：" + error;
        return false;
    }

    const std::filesystem::path path = pathFor(slot);
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
        error = "无法创建存档目录：" + code.message();
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理旧的临时存档：" + code.message();
        return false;
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法打开临时存档文件。";
            return false;
        }
        output.write(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        output.flush();
        if (!output) {
            error = "写入临时存档失败。";
            return false;
        }
        output.close();
        if (!output) {
            error = "关闭临时存档失败。";
            return false;
        }
    }

    GameState diskRoundTrip;
    std::string temporaryError;
    if (loadFile(temporary, diskRoundTrip, temporaryError) != LoadFileStatus::Loaded) {
        std::filesystem::remove(temporary, code);
        error = "临时存档解析校验失败：" + temporaryError;
        return false;
    }

    const bool hadOriginal = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查旧存档：" + code.message();
        std::filesystem::remove(temporary, code);
        return false;
    }
    if (hadOriginal) {
        std::filesystem::remove(backup, code);
        if (code) {
            error = "无法清理旧备份：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
        std::filesystem::rename(path, backup, code);
        if (code) {
            error = "无法备份旧存档：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
    }

    code.clear();
    std::filesystem::rename(temporary, path, code);
    if (code) {
        const std::string renameError = code.message();
        if (hadOriginal) {
            std::error_code restoreCode;
            std::filesystem::rename(backup, path, restoreCode);
            if (restoreCode) {
                std::filesystem::remove(temporary, code);
                error = "无法替换正式存档：" + renameError
                    + "；恢复旧存档也失败：" + restoreCode.message()
                    + "。旧数据仍保留在" + backup.filename().string() + "。";
                return false;
            }
        }
        std::filesystem::remove(temporary, code);
        error = "无法替换正式存档：" + renameError;
        return false;
    }

    // Keep the previous committed primary as .bak. It is the recovery point if
    // the new primary is later truncated or damaged.
    error.clear();
    return true;
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate,
    std::string& error) const {
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    const std::filesystem::path path = pathFor(slot);
    GameState parsed;
    std::string primaryError;
    const LoadFileStatus primaryStatus = loadFile(path, parsed, primaryError);
    if (primaryStatus == LoadFileStatus::Loaded) {
        candidate = std::move(parsed);
        error.clear();
        return true;
    }
    if (primaryStatus == LoadFileStatus::Unavailable) {
        error = "读取" + slotName(slot) + "失败。主文件暂时不可用：" + primaryError
            + "。为避免误读旧备份，本次没有自动回退。";
        return false;
    }

    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    std::string backupError;
    if (loadFile(backup, parsed, backupError) == LoadFileStatus::Loaded) {
        restoreRecoveredFile(backup, path);
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    std::string temporaryError;
    if (loadFile(temporary, parsed, temporaryError) == LoadFileStatus::Loaded) {
        restoreRecoveredFile(temporary, path);
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    error = "读取" + slotName(slot) + "失败。主文件：" + primaryError
        + "；备份文件：" + backupError + "；临时文件：" + temporaryError;
    return false;
}

std::vector<SaveSummary> SaveRepository::inspect() const {
    // 列表只解析文件，不调用可能修复主档的 load；候选顺序与实际读取一致。
    constexpr std::array<SaveSlot, 7> slots{{SaveSlot::Autosave, SaveSlot::Slot1,
        SaveSlot::Slot2, SaveSlot::Slot3, SaveSlot::Slot4, SaveSlot::Slot5, SaveSlot::Slot6}};
    std::vector<SaveSummary> summaries;
    summaries.reserve(slots.size());
    for (const SaveSlot slot : slots) {
        const std::filesystem::path primary = pathFor(slot);
        std::filesystem::path backup = primary;
        backup += ".bak";
        std::filesystem::path temporary = primary;
        temporary += ".tmp";

        GameState state;
        std::string primaryError;
        const LoadFileStatus primaryStatus = loadFile(primary, state, primaryError);
        if (primaryStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Ready, primary, state));
            continue;
        }
        if (primaryStatus == LoadFileStatus::Unavailable) {
            SaveSummary summary;
            summary.slot = slot;
            summary.status = SaveStatus::Corrupt;
            summaries.push_back(std::move(summary));
            continue;
        }

        std::string backupError;
        const LoadFileStatus backupStatus = loadFile(backup, state, backupError);
        if (backupStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, backup, state));
            continue;
        }
        std::string temporaryError;
        const LoadFileStatus temporaryStatus = loadFile(temporary, state, temporaryError);
        if (temporaryStatus == LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, temporary, state));
            continue;
        }

        SaveSummary summary;
        summary.slot = slot;
        const bool allMissing = primaryStatus == LoadFileStatus::Missing
            && backupStatus == LoadFileStatus::Missing
            && temporaryStatus == LoadFileStatus::Missing;
        summary.status = allMissing ? SaveStatus::Empty : SaveStatus::Corrupt;
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

std::optional<SaveSlot> SaveRepository::parseSlot(const std::string_view text) {
    if (text == "1" || text == "slot1" || text == "存档1") return SaveSlot::Slot1;
    if (text == "2" || text == "slot2" || text == "存档2") return SaveSlot::Slot2;
    if (text == "3" || text == "slot3" || text == "存档3") return SaveSlot::Slot3;
    if (text == "4" || text == "slot4" || text == "存档4") return SaveSlot::Slot4;
    if (text == "5" || text == "slot5" || text == "存档5") return SaveSlot::Slot5;
    if (text == "6" || text == "slot6" || text == "存档6") return SaveSlot::Slot6;
    if (text == "auto" || text == "autosave" || text == "自动" || text == "自动档") {
        return SaveSlot::Autosave;
    }
    return std::nullopt;
}

std::string SaveRepository::slotName(const SaveSlot slot) {
    switch (slot) {
    case SaveSlot::Slot1: return "手动存档1";
    case SaveSlot::Slot2: return "手动存档2";
    case SaveSlot::Slot3: return "手动存档3";
    case SaveSlot::Slot4: return "手动存档4";
    case SaveSlot::Slot5: return "手动存档5";
    case SaveSlot::Slot6: return "手动存档6";
    case SaveSlot::Autosave: return "自动存档";
    }
    return "未知存档";
}

} // namespace tribe
