#include "test_harness.hpp"

#include "tribe/campaign_save_repository.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

std::filesystem::path saveRoot(const std::string& name) {
    return std::filesystem::current_path() / ("campaign-save-tests-" + name);
}

void clean(const std::filesystem::path& root) {
    std::error_code code;
    std::filesystem::remove_all(root, code);
}

bool sameItem(const tribe::Item& left, const tribe::Item& right) {
    return left.id == right.id && left.name == right.name && left.quality == right.quality
        && left.condition == right.condition && left.weight == right.weight
        && left.slotCount == right.slotCount && left.equipmentSlot == right.equipmentSlot
        && left.bonuses.values == right.bonuses.values;
}

bool sameCharacter(const tribe::Character& left, const tribe::Character& right) {
    if (left.name != right.name || left.occupation != right.occupation || left.level != right.level
        || left.experience != right.experience || left.growthPoints != right.growthPoints
        || left.life != right.life || left.fatigue != right.fatigue || left.loyalty != right.loyalty
        || left.attributes.values != right.attributes.values) {
        return false;
    }
    for (std::size_t index = 0; index < left.equipment.size(); ++index) {
        if (left.equipment[index].has_value() != right.equipment[index].has_value()) return false;
        if (left.equipment[index] && !sameItem(*left.equipment[index], *right.equipment[index])) return false;
    }
    return true;
}

bool sameInventory(const tribe::Inventory& left, const tribe::Inventory& right) {
    if (left.weightLimit() != right.weightLimit() || left.slotLimit() != right.slotLimit()
        || left.items().size() != right.items().size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.items().size(); ++index) {
        if (!sameItem(left.items()[index], right.items()[index])) return false;
    }
    return true;
}

bool sameExpansionSquad(const tribe::Squad& left, const tribe::Squad& right) {
    if (left.name != right.name || left.leaderIndex != right.leaderIndex
        || left.residentMission != right.residentMission || left.cohesion != right.cohesion
        || left.members.size() != right.members.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.members.size(); ++index) {
        if (!sameCharacter(left.members[index], right.members[index])) return false;
    }
    return true;
}

bool sameExpansion(const tribe::ExpansionState& left, const tribe::ExpansionState& right) {
    return left.seed == right.seed && left.turn == right.turn && left.phase == right.phase
        && left.location == right.location && left.foreignStance == right.foreignStance
        && left.order == right.order && sameExpansionSquad(left.squad, right.squad)
        && sameInventory(left.inventory, right.inventory) && left.supplies == right.supplies
        && left.herbs == right.herbs && left.hides == right.hides && left.medicine == right.medicine
        && left.tradeGoods == right.tradeGoods && left.frontline == right.frontline
        && left.enemyLife == right.enemyLife && left.enemySpeed == right.enemySpeed
        && left.leaderActions == right.leaderActions && left.followerActions == right.followerActions
        && left.dodges == right.dodges && left.lastPlayerInitiative == right.lastPlayerInitiative
        && left.lastEnemyInitiative == right.lastEnemyInitiative && left.traded == right.traded
        && left.battleWon == right.battleWon && left.retreated == right.retreated
        && left.missionFailed == right.missionFailed && left.lootAvailable == right.lootAvailable
        && left.settled == right.settled;
}

bool sameFaction(const tribe::FactionState& left, const tribe::FactionState& right) {
    return left.name == right.name && left.influence == right.influence
        && left.satisfaction == right.satisfaction && left.demand == right.demand
        && left.candidate == right.candidate && left.crisis == right.crisis;
}

bool sameProfile(const tribe::TribeProfile& left, const tribe::TribeProfile& right) {
    if (left.id != right.id || left.name != right.name || left.leader != right.leader
        || left.actingLeader != right.actingLeader || left.successor != right.successor
        || left.personality != right.personality || left.factions.size() != right.factions.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.factions.size(); ++index) {
        if (!sameFaction(left.factions[index], right.factions[index])) return false;
    }
    return true;
}

bool sameRelation(const tribe::DiplomacyRelation& left, const tribe::DiplomacyRelation& right) {
    return left.relation == right.relation && left.trust == right.trust && left.fear == right.fear
        && left.tradeDependence == right.tradeDependence && left.atWar == right.atWar
        && left.truce == right.truce && left.alliance == right.alliance
        && left.marriage == right.marriage && left.playerPaysTribute == right.playerPaysTribute
        && left.otherPaysTribute == right.otherPaysTribute && left.tradeRoute == right.tradeRoute;
}

bool samePermanentSquad(const tribe::PermanentSquad& left, const tribe::PermanentSquad& right) {
    return left.name == right.name && left.captain == right.captain && left.members == right.members
        && left.residentMission == right.residentMission && left.fatigue == right.fatigue
        && left.eliteExperience == right.eliteExperience
        && left.personallyDeployedThisSeason == right.personallyDeployedThisSeason
        && left.refusingOrders == right.refusingOrders
        && sameInventory(left.backpack, right.backpack);
}

bool sameWar(const tribe::WarState& left, const tribe::WarState& right) {
    return left.active == right.active && left.enemy == right.enemy && left.commander == right.commander
        && left.warriors == right.warriors && left.militia == right.militia
        && left.playerPower == right.playerPower && left.enemyPower == right.enemyPower
        && left.front == right.front && left.order == right.order
        && left.riskConfirmed == right.riskConfirmed;
}

bool sameChronicle(const tribe::ChronicleEntry& left, const tribe::ChronicleEntry& right) {
    return left.season == right.season && left.importance == right.importance
        && left.title == right.title && left.detail == right.detail;
}

bool sameCampaign(const tribe::CampaignState& left, const tribe::CampaignState& right) {
    if (left.mode != right.mode || left.phase != right.phase || left.seed != right.seed
        || left.season != right.season || left.seasonLimit != right.seasonLimit
        || left.actionsLeft != right.actionsLeft || left.population != right.population
        || left.food != right.food || left.wood != right.wood || left.stone != right.stone
        || left.herbs != right.herbs || left.warriors != right.warriors
        || left.morale != right.morale || left.campDurability != right.campDurability
        || left.stability != right.stability || left.shells != right.shells
        || left.tradeCount != right.tradeCount || left.warsWon != right.warsWon
        || left.warsLost != right.warsLost || left.missionCount != right.missionCount
        || left.missionDeaths != right.missionDeaths || left.highestLevel != right.highestLevel
        || left.rockfangStrength != right.rockfangStrength || left.tribeName != right.tribeName
        || left.leaderName != right.leaderName || left.actingLeaderName != right.actingLeaderName
        || left.leaderFocus != right.leaderFocus || left.discovered != right.discovered
        || left.buildings != right.buildings || left.technologies != right.technologies
        || left.tradePartners != right.tradePartners || left.missionRewardClaimed != right.missionRewardClaimed
        || left.currencyUnlocked != right.currencyUnlocked
        || left.rockfangFortCaptured != right.rockfangFortCaptured
        || left.longModeFinalShown != right.longModeFinalShown || left.ending != right.ending
        || left.leadershipHistory != right.leadershipHistory || !sameWar(left.war, right.war)) {
        return false;
    }
    for (std::size_t index = 0; index < left.tribes.size(); ++index) {
        if (!sameProfile(left.tribes[index], right.tribes[index])) return false;
        if (!sameRelation(left.relations[index], right.relations[index])) return false;
    }
    for (std::size_t index = 0; index < left.playerFactions.size(); ++index) {
        if (!sameFaction(left.playerFactions[index], right.playerFactions[index])) return false;
    }
    if (left.roster.size() != right.roster.size() || left.squads.size() != right.squads.size()
        || left.chronicle.size() != right.chronicle.size()
        || left.activeMission.has_value() != right.activeMission.has_value()) {
        return false;
    }
    for (std::size_t index = 0; index < left.roster.size(); ++index) {
        if (!sameCharacter(left.roster[index], right.roster[index])) return false;
    }
    for (std::size_t index = 0; index < left.squads.size(); ++index) {
        if (!samePermanentSquad(left.squads[index], right.squads[index])) return false;
    }
    if (left.activeMission && !sameExpansion(*left.activeMission, *right.activeMission)) return false;
    for (std::size_t index = 0; index < left.chronicle.size(); ++index) {
        if (!sameChronicle(left.chronicle[index], right.chronicle[index])) return false;
    }
    return true;
}

tribe::CampaignState richState() {
    tribe::CampaignGame game({tribe::CampaignMode::Long, 0xC0FFEEU,
        "燧=火\n长名部落", "炎角·二世", "贸易与生存"});
    tribe::CampaignState state = game.state();
    state.season = 7;
    state.actionsLeft = 2;
    state.population = 27;
    state.food = 88;
    state.wood = 47;
    state.stone = 31;
    state.herbs = 19;
    state.warriors = 9;
    state.morale = 83;
    state.campDurability = 73;
    state.stability = 78;
    state.shells = 41;
    state.tradeCount = 6;
    state.warsWon = 2;
    state.warsLost = 1;
    state.missionCount = 4;
    state.missionDeaths = 1;
    state.highestLevel = 2;
    state.rockfangStrength = 17;
    state.actingLeaderName = "青枝（代理）";
    state.discovered[static_cast<std::size_t>(tribe::WorldLocationId::TidesaltHarbor)] = true;
    state.buildings[static_cast<std::size_t>(tribe::BuildingId::Granary)] = true;
    state.technologies[static_cast<std::size_t>(tribe::TechnologyId::FoodPreservation)] = true;

    for (std::size_t index = 0; index < state.tribes.size(); ++index) {
        state.tribes[index].actingLeader = "代理" + std::to_string(index);
        state.tribes[index].personality += "\n记录=" + std::to_string(index);
        state.tribes[index].factions.front().demand += "\n追加条件";
        state.relations[index].relation = static_cast<int>(index) * 10 - 20;
        state.relations[index].trust = static_cast<int>(index) * 7;
        state.relations[index].fear = static_cast<int>(index) * 5;
        state.relations[index].tradeDependence = static_cast<int>(index) * 6;
        state.relations[index].truce = index == 2U;
        state.relations[index].marriage = index == 1U;
        state.relations[index].playerPaysTribute = index == 4U;
        state.relations[index].otherPaysTribute = index == 5U;
        state.relations[index].tradeRoute = index >= 3U;
        state.tradePartners[index] = index % 2U == 1U;
    }
    state.playerFactions[0].crisis = tribe::FactionCrisis::Complaint;
    state.playerFactions[1].crisis = tribe::FactionCrisis::Slowdown;
    state.playerFactions[2].crisis = tribe::FactionCrisis::Refusal;

    tribe::Character& leader = state.roster.front();
    leader.level = 2;
    leader.experience = 77;
    leader.growthPoints = 1;
    leader.fatigue = 23;
    leader.loyalty = 91;
    tribe::Item charm;
    charm.id = "lineage_charm";
    charm.name = "刻有=与换行\n的骨饰";
    charm.quality = tribe::ItemQuality::Rare;
    charm.condition = tribe::ItemCondition::Damaged;
    charm.weight = 2;
    charm.slotCount = 1;
    charm.equipmentSlot = tribe::EquipmentSlot::Accessory;
    charm.bonuses[tribe::Attribute::Leadership] = 3;
    leader.equipment[static_cast<std::size_t>(tribe::EquipmentSlot::Accessory)] = charm;

    state.squads.front().residentMission = tribe::ResidentMission::Escort;
    state.squads.front().fatigue = 44;
    state.squads.front().eliteExperience = 87;
    state.squads.front().personallyDeployedThisSeason = true;
    state.squads.front().refusingOrders = true;
    state.war = {false, tribe::TribeIdV2::Tidesalt, "石刃=统帅", 7, 5, 31, 19, 2,
        tribe::WarOrder::Flank, true};
    state.currencyUnlocked = true;
    state.rockfangFortCaptured = false;
    state.longModeFinalShown = true;
    state.leadershipHistory.push_back("青枝\n代理纪年");
    state.chronicle.push_back({7, 5, "海路=开通", "潮盐来船。\n玄石送来工具。"});

    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(state, error));
    return state;
}

tribe::CampaignState missionState() {
    tribe::CampaignGame game({tribe::CampaignMode::Course, 307U, "任务部落", "任务首领", "侦察"});
    REQUIRE(game.execute("mission forest").success);
    REQUIRE(game.execute("equip mainhand spare_knife").success);
    REQUIRE(game.execute("move forest").success);
    REQUIRE(game.execute("move deep").success);
    REQUIRE(game.execute("gather herbs").success);
    REQUIRE(game.state().phase == tribe::CampaignPhase::Mission);
    REQUIRE(game.state().activeMission.has_value());
    return game.state();
}

tribe::CampaignState warState() {
    tribe::CampaignGame game({tribe::CampaignMode::Course, 401U, "战役部落", "石刃", "战争"});
    tribe::CampaignState state = game.state();
    state.phase = tribe::CampaignPhase::War;
    auto& relation = state.relations[static_cast<std::size_t>(tribe::TribeIdV2::Rockfang)];
    relation.atWar = true;
    relation.alliance = false;
    relation.truce = false;
    state.war = {true, tribe::TribeIdV2::Rockfang, "石刃", 3, 4, 21, 20, 2,
        tribe::WarOrder::Focus, true};
    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(state, error));
    return state;
}

void writeCorrupt(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output));
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(static_cast<bool>(output));
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(static_cast<bool>(input));
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint32_t readU32(const std::string& bytes, const std::size_t offset) {
    REQUIRE(offset + 4U <= bytes.size());
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(bytes[offset + static_cast<std::size_t>(shift / 8)])) << shift;
    }
    return value;
}

void writeU32(std::string& bytes, const std::size_t offset, const std::uint32_t value) {
    REQUIRE(offset + 4U <= bytes.size());
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)]
            = static_cast<char>((value >> shift) & 0xFFU);
    }
}

std::uint32_t payloadChecksum(const std::string_view payload) {
    std::uint32_t value = 2166136261U;
    for (const unsigned char byte : payload) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

std::string withoutSquadBackpackExtension(std::string bytes) {
    constexpr std::size_t headerSize = 20U;
    REQUIRE(bytes.size() >= headerSize);
    REQUIRE(readU32(bytes, 12U) == bytes.size() - headerSize);
    const std::size_t extension = bytes.rfind("SPK1");
    REQUIRE(extension != std::string::npos);
    REQUIRE(extension >= headerSize);
    bytes.resize(extension);
    const std::size_t payloadSize = bytes.size() - headerSize;
    REQUIRE(payloadSize <= std::numeric_limits<std::uint32_t>::max());
    writeU32(bytes, 12U, static_cast<std::uint32_t>(payloadSize));
    writeU32(bytes, 16U, payloadChecksum(std::string_view(bytes).substr(headerSize)));
    return bytes;
}

} // namespace

TEST_CASE("campaign save repository exposes six manual slots and one autosave") {
    const auto root = saveRoot("slots");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const std::array<tribe::CampaignSaveSlot, 7> slots{{
        tribe::CampaignSaveSlot::Slot1, tribe::CampaignSaveSlot::Slot2,
        tribe::CampaignSaveSlot::Slot3, tribe::CampaignSaveSlot::Slot4,
        tribe::CampaignSaveSlot::Slot5, tribe::CampaignSaveSlot::Slot6,
        tribe::CampaignSaveSlot::Autosave,
    }};
    const tribe::CampaignState expected = richState();
    std::string error;
    REQUIRE(repository.saveNew(expected, tribe::CampaignSaveSlot::Slot1, error));
    const std::string firstCopy = readBytes(repository.pathFor(tribe::CampaignSaveSlot::Slot1));
    REQUIRE(!repository.saveNew(expected, tribe::CampaignSaveSlot::Slot1, error));
    REQUIRE(readBytes(repository.pathFor(tribe::CampaignSaveSlot::Slot1)) == firstCopy);
    clean(root);
    for (const auto slot : slots) {
        REQUIRE(repository.save(expected, slot, error));
        REQUIRE(error.empty());
        REQUIRE(std::filesystem::is_regular_file(repository.pathFor(slot)));
        tribe::CampaignState loaded;
        REQUIRE(repository.load(slot, loaded, error));
        REQUIRE(error.empty());
        REQUIRE(sameCampaign(loaded, expected));
    }
    REQUIRE(tribe::CampaignSaveRepository::parseSlot("1") == tribe::CampaignSaveSlot::Slot1);
    REQUIRE(tribe::CampaignSaveRepository::parseSlot("slot6") == tribe::CampaignSaveSlot::Slot6);
    REQUIRE(tribe::CampaignSaveRepository::parseSlot("自动档") == tribe::CampaignSaveSlot::Autosave);
    REQUIRE(!tribe::CampaignSaveRepository::parseSlot("7"));
    clean(root);
}

TEST_CASE("campaign save round trip preserves managing mission and war states completely") {
    const auto root = saveRoot("complete");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const std::array<tribe::CampaignState, 3> states{{richState(), missionState(), warState()}};
    const std::array<tribe::CampaignSaveSlot, 3> slots{{
        tribe::CampaignSaveSlot::Slot1, tribe::CampaignSaveSlot::Slot2, tribe::CampaignSaveSlot::Slot3}};
    std::string error;
    for (std::size_t index = 0; index < states.size(); ++index) {
        REQUIRE(repository.save(states[index], slots[index], error));
        tribe::CampaignState loaded;
        REQUIRE(repository.load(slots[index], loaded, error));
        REQUIRE(sameCampaign(loaded, states[index]));
    }
    clean(root);
}

TEST_CASE("campaign save reads released V2 files without the optional squad backpack extension") {
    const auto root = saveRoot("released-v2-compatibility");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    tribe::CampaignState expected = richState();
    std::string error;
    REQUIRE(repository.save(expected, tribe::CampaignSaveSlot::Slot1, error));
    const auto path = repository.pathFor(tribe::CampaignSaveSlot::Slot1);
    writeCorrupt(path, withoutSquadBackpackExtension(readBytes(path)));

    for (tribe::PermanentSquad& squad : expected.squads) {
        squad.backpack = tribe::Inventory{80, 20};
    }
    tribe::CampaignState loaded;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot1, loaded, error));
    REQUIRE(error.empty());
    REQUIRE(sameCampaign(loaded, expected));
    clean(root);
}

TEST_CASE("campaign save recovers the last committed backup when primary is corrupt") {
    const auto root = saveRoot("backup");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const tribe::CampaignState previous = richState();
    tribe::CampaignState current = previous;
    ++current.food;
    current.chronicle.push_back({current.season, 2, "第二次保存", "这是新的正式主档。"});
    std::string error;
    REQUIRE(repository.save(previous, tribe::CampaignSaveSlot::Slot4, error));
    REQUIRE(repository.save(current, tribe::CampaignSaveSlot::Slot4, error));

    const auto primary = repository.pathFor(tribe::CampaignSaveSlot::Slot4);
    auto backup = primary;
    backup += ".bak";
    REQUIRE(std::filesystem::is_regular_file(backup));
    writeCorrupt(primary, "damaged campaign primary");

    tribe::CampaignState recovered = current;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot4, recovered, error));
    REQUIRE(sameCampaign(recovered, previous));
    REQUIRE(std::filesystem::is_regular_file(primary));
    tribe::CampaignState reloaded;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot4, reloaded, error));
    REQUIRE(sameCampaign(reloaded, previous));
    clean(root);
}

TEST_CASE("campaign save recovers a validated temporary file after interrupted replacement") {
    const auto root = saveRoot("temporary");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const tribe::CampaignState expected = missionState();
    std::string error;
    REQUIRE(repository.save(expected, tribe::CampaignSaveSlot::Slot5, error));
    const auto primary = repository.pathFor(tribe::CampaignSaveSlot::Slot5);
    auto temporary = primary;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::rename(primary, temporary, code);
    REQUIRE(!code);

    tribe::CampaignState recovered;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot5, recovered, error));
    REQUIRE(sameCampaign(recovered, expected));
    REQUIRE(std::filesystem::is_regular_file(primary));
    clean(root);
}

TEST_CASE("campaign invalid save and corrupt load are atomic") {
    const auto root = saveRoot("atomic");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const tribe::CampaignState expected = warState();
    std::string error;
    REQUIRE(repository.save(expected, tribe::CampaignSaveSlot::Slot6, error));

    tribe::CampaignState invalid = expected;
    invalid.actionsLeft = 99;
    REQUIRE(!repository.save(invalid, tribe::CampaignSaveSlot::Slot6, error));
    tribe::CampaignState loaded;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot6, loaded, error));
    REQUIRE(sameCampaign(loaded, expected));

    const auto primary = repository.pathFor(tribe::CampaignSaveSlot::Slot6);
    writeCorrupt(primary, "wrong-version-or-checksum");
    tribe::CampaignState destination = richState();
    const tribe::CampaignState before = destination;
    REQUIRE(!repository.load(tribe::CampaignSaveSlot::Slot6, destination, error));
    REQUIRE(!error.empty());
    REQUIRE(sameCampaign(destination, before));
    clean(root);
}

TEST_CASE("campaign save rejects a wrong version and checksum without mutating destination") {
    const auto root = saveRoot("format-validation");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const tribe::CampaignState expected = richState();
    std::string error;
    REQUIRE(repository.save(expected, tribe::CampaignSaveSlot::Slot3, error));
    const auto primary = repository.pathFor(tribe::CampaignSaveSlot::Slot3);
    const std::string validBytes = readBytes(primary);
    REQUIRE(validBytes.size() > 20U);

    std::string wrongVersion = validBytes;
    wrongVersion[8] = 3;
    wrongVersion[9] = 0;
    wrongVersion[10] = 0;
    wrongVersion[11] = 0;
    writeCorrupt(primary, wrongVersion);
    tribe::CampaignState destination = warState();
    const tribe::CampaignState before = destination;
    REQUIRE(!repository.load(tribe::CampaignSaveSlot::Slot3, destination, error));
    REQUIRE(error.find("版本") != std::string::npos);
    REQUIRE(sameCampaign(destination, before));

    std::string wrongChecksum = validBytes;
    wrongChecksum.back() = static_cast<char>(wrongChecksum.back() ^ 0x01);
    writeCorrupt(primary, wrongChecksum);
    REQUIRE(!repository.load(tribe::CampaignSaveSlot::Slot3, destination, error));
    REQUIRE(error.find("校验和") != std::string::npos);
    REQUIRE(sameCampaign(destination, before));
    clean(root);
}

TEST_CASE("campaign failed temporary replacement leaves committed primary unchanged") {
    const auto root = saveRoot("temporary-failure");
    clean(root);
    tribe::CampaignSaveRepository repository(root);
    const tribe::CampaignState expected = richState();
    std::string error;
    REQUIRE(repository.save(expected, tribe::CampaignSaveSlot::Slot2, error));

    const auto primary = repository.pathFor(tribe::CampaignSaveSlot::Slot2);
    auto temporary = primary;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::create_directories(temporary, code);
    REQUIRE(!code);
    {
        std::ofstream guard(temporary / "keep.txt", std::ios::binary | std::ios::trunc);
        guard << "prevent removal";
    }
    tribe::CampaignState replacement = expected;
    ++replacement.wood;
    REQUIRE(!repository.save(replacement, tribe::CampaignSaveSlot::Slot2, error));
    tribe::CampaignState loaded;
    REQUIRE(repository.load(tribe::CampaignSaveSlot::Slot2, loaded, error));
    REQUIRE(sameCampaign(loaded, expected));
    clean(root);
}
