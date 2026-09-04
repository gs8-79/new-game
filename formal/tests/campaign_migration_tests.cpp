#include "test_harness.hpp"

#include "tribe/campaign_migration.hpp"
#include "tribe/campaign_save_repository.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class TempRoot {
public:
    explicit TempRoot(const std::string& label) {
        static unsigned long counter = 0;
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("tribe-campaign-migration-" + label + "-" + std::to_string(stamp)
                + "-" + std::to_string(++counter));
        std::error_code code;
        std::filesystem::create_directories(path_, code);
        REQUIRE(!code);
    }

    ~TempRoot() {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(static_cast<bool>(input));
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(static_cast<bool>(output));
}

std::string replaceLine(
    const std::string& text, const std::string& prefix, const std::string& replacement) {
    const std::size_t start = text.find(prefix);
    REQUIRE(start != std::string::npos);
    const std::size_t end = text.find('\n', start);
    REQUIRE(end != std::string::npos);
    std::string changed = text;
    changed.replace(start, end - start, replacement);
    return changed;
}

void setTurn(tribe::GameState& state, const int turn) {
    state.turn = turn;
    state.currentEvent = state.eventSchedule.at(static_cast<std::size_t>(turn - 1));
}

void requireValidLegacy(const tribe::GameState& state) {
    std::string error;
    REQUIRE(tribe::GameEngine::validateState(state, error));
}

void requireValidCampaign(const tribe::CampaignState& state) {
    std::string error;
    REQUIRE(tribe::CampaignGame::validateState(state, error));
}

} // namespace

TEST_CASE("legacy migration preserves compatible campaign progress") {
    tribe::GameState legacy = tribe::GameEngine({tribe::GameMode::Quick, 73U}).state();
    legacy.actionsLeft = 2;
    legacy.population = 23;
    legacy.food = 57;
    legacy.wood = 31;
    legacy.stone = 17;
    legacy.herbs = 8;
    legacy.warriors = 6;
    legacy.morale = 74;
    legacy.campDurability = 19;
    legacy.rockfangStrength = 8;
    legacy.buildings[tribe::indexOf(tribe::BuildingId::Workshop)] = true;
    legacy.technologies[tribe::indexOf(tribe::TechnologyId::HerbalKnowledge)] = true;
    legacy.relations = {{66, 42, 10}};
    legacy.quests = {{2, 1, 3}};
    legacy.rockfangTruce = true;
    requireValidLegacy(legacy);

    tribe::CampaignState migrated;
    std::string error;
    REQUIRE(tribe::CampaignMigration::convert(legacy, migrated, error));
    REQUIRE(error.empty());
    requireValidCampaign(migrated);
    REQUIRE(migrated.mode == tribe::CampaignMode::Quick);
    REQUIRE(migrated.season == legacy.turn);
    REQUIRE(migrated.seasonLimit == 16);
    REQUIRE(migrated.actionsLeft == legacy.actionsLeft);
    REQUIRE(migrated.population == legacy.population);
    REQUIRE(migrated.food == legacy.food);
    REQUIRE(migrated.wood == legacy.wood);
    REQUIRE(migrated.stone == legacy.stone);
    REQUIRE(migrated.herbs == legacy.herbs);
    REQUIRE(migrated.warriors == legacy.warriors);
    REQUIRE(migrated.morale == legacy.morale);
    REQUIRE(migrated.campDurability == legacy.campDurability);
    REQUIRE(migrated.rockfangStrength == legacy.rockfangStrength);
    REQUIRE(migrated.buildings == legacy.buildings);
    REQUIRE(migrated.technologies == legacy.technologies);
    for (std::size_t index = 0; index < tribe::kLocationCount; ++index) {
        REQUIRE(migrated.discovered[index] == legacy.discovered[index]);
    }
    for (std::size_t index = tribe::kLocationCount; index < tribe::kWorldLocationCount; ++index) {
        REQUIRE(!migrated.discovered[index]);
    }
    const auto& riverDeer = migrated.relations[tribe::indexOf(tribe::TribeIdV2::RiverDeer)];
    const auto& whiteFeather = migrated.relations[tribe::indexOf(tribe::TribeIdV2::WhiteFeather)];
    const auto& rockfang = migrated.relations[tribe::indexOf(tribe::TribeIdV2::Rockfang)];
    REQUIRE(riverDeer.relation == legacy.relations[0]);
    REQUIRE(whiteFeather.relation == legacy.relations[1]);
    REQUIRE(rockfang.relation == legacy.relations[2]);
    REQUIRE(riverDeer.trust > whiteFeather.trust);
    REQUIRE(rockfang.truce);
    REQUIRE(!rockfang.atWar);
    REQUIRE(migrated.chronicle.back().title == "旧正式版存档升级");
}

TEST_CASE("legacy phases migrate to valid V2 phases and preserve a finished ending") {
    std::string error;

    tribe::GameState playing = tribe::GameEngine({tribe::GameMode::Standard, 101U}).state();
    tribe::CampaignState converted;
    REQUIRE(tribe::CampaignMigration::convert(playing, converted, error));
    REQUIRE(converted.phase == tribe::CampaignPhase::Managing);
    requireValidCampaign(converted);

    tribe::GameState raid = tribe::GameEngine({tribe::GameMode::Standard, 102U}).state();
    setTurn(raid, 12);
    raid.phase = tribe::Phase::AwaitingRaid;
    raid.pendingRaid = true;
    raid.actionsLeft = 0;
    requireValidLegacy(raid);
    REQUIRE(tribe::CampaignMigration::convert(raid, converted, error));
    REQUIRE(converted.phase == tribe::CampaignPhase::War);
    REQUIRE(converted.war.active);
    REQUIRE(converted.war.enemy == tribe::TribeIdV2::Rockfang);
    REQUIRE(!converted.war.commander.empty());
    REQUIRE(converted.war.enemyPower > 0);
    REQUIRE(converted.relations[tribe::indexOf(tribe::TribeIdV2::Rockfang)].atWar);
    requireValidCampaign(converted);

    tribe::GameState choice = tribe::GameEngine({tribe::GameMode::Standard, 103U}).state();
    setTurn(choice, 16);
    choice.phase = tribe::Phase::FinalChoice;
    choice.actionsLeft = 0;
    requireValidLegacy(choice);
    REQUIRE(tribe::CampaignMigration::convert(choice, converted, error));
    REQUIRE(converted.phase == tribe::CampaignPhase::EndingChoice);
    REQUIRE(converted.ending == tribe::CampaignEnding::None);
    REQUIRE(converted.actionsLeft == 0);
    requireValidCampaign(converted);

    tribe::GameState finished = choice;
    finished.phase = tribe::Phase::Finished;
    finished.ending = tribe::Ending::Migration;
    requireValidLegacy(finished);
    REQUIRE(tribe::CampaignMigration::convert(finished, converted, error));
    REQUIRE(converted.phase == tribe::CampaignPhase::Finished);
    REQUIRE(converted.ending == tribe::CampaignEnding::Migration);
    requireValidCampaign(converted);
}

TEST_CASE("invalid legacy conversion is atomic for the complete V2 candidate") {
    TempRoot root("atomic-convert");
    tribe::CampaignState candidate = tribe::CampaignGame(
        {tribe::CampaignMode::Long, 907U, "原状态", "原首领", "稳定"}).state();
    tribe::CampaignSaveRepository snapshots(root.path());
    std::string error;
    REQUIRE(snapshots.save(candidate, tribe::CampaignSaveSlot::Slot1, error));

    tribe::GameState invalid = tribe::GameEngine({tribe::GameMode::Standard, 77U}).state();
    invalid.actionsLeft = 99;
    REQUIRE(!tribe::CampaignMigration::convert(invalid, candidate, error));
    REQUIRE(!error.empty());

    REQUIRE(snapshots.save(candidate, tribe::CampaignSaveSlot::Slot2, error));
    REQUIRE(readBytes(snapshots.pathFor(tribe::CampaignSaveSlot::Slot1))
        == readBytes(snapshots.pathFor(tribe::CampaignSaveSlot::Slot2)));
}

TEST_CASE("legacy migration reads primary and backup without modifying either legacy file") {
    TempRoot root("readonly-files");
    tribe::SaveRepository legacySaves(root.path() / "legacy");
    tribe::GameState expected = tribe::GameEngine({tribe::GameMode::Quick, 808U}).state();
    std::string error;
    REQUIRE(legacySaves.save(expected, tribe::SaveSlot::Slot1, error));
    const auto primary = legacySaves.pathFor(tribe::SaveSlot::Slot1);
    const std::string primaryBefore = readBytes(primary);

    tribe::CampaignState migrated;
    REQUIRE(tribe::CampaignMigration::loadAndConvertReadOnly(primary, migrated, error));
    REQUIRE(readBytes(primary) == primaryBefore);
    requireValidCampaign(migrated);

    auto backup = primary;
    backup += ".bak";
    std::error_code code;
    std::filesystem::rename(primary, backup, code);
    REQUIRE(!code);
    writeBytes(primary, "corrupt primary must remain corrupt");
    const std::string corruptPrimaryBefore = readBytes(primary);
    const std::string backupBefore = readBytes(backup);

    tribe::CampaignState recovered;
    REQUIRE(tribe::CampaignMigration::loadAndConvertReadOnly(primary, recovered, error));
    REQUIRE(readBytes(primary) == corruptPrimaryBefore);
    REQUIRE(readBytes(backup) == backupBefore);
    requireValidCampaign(recovered);
}

TEST_CASE("legacy read-only parser rejects schema version and array corruption atomically") {
    TempRoot root("strict-parser");
    tribe::SaveRepository legacySaves(root.path());
    tribe::GameState valid = tribe::GameEngine({tribe::GameMode::Standard, 909U}).state();
    std::string error;
    REQUIRE(legacySaves.save(valid, tribe::SaveSlot::Slot2, error));
    const auto primary = legacySaves.pathFor(tribe::SaveSlot::Slot2);
    const std::string validText = readBytes(primary);

    tribe::GameState destination = tribe::GameEngine({tribe::GameMode::Quick, 910U}).state();
    const tribe::GameState before = destination;

    writeBytes(primary, replaceLine(validText, "version=", "version=2"));
    REQUIRE(!tribe::CampaignMigration::loadLegacyReadOnly(primary, destination, error));
    REQUIRE(destination == before);

    writeBytes(primary, replaceLine(validText, "format=", "format=other-game"));
    REQUIRE(!tribe::CampaignMigration::loadLegacyReadOnly(primary, destination, error));
    REQUIRE(destination == before);

    writeBytes(primary, replaceLine(validText, "discovered=", "discovered=1,1"));
    REQUIRE(!tribe::CampaignMigration::loadLegacyReadOnly(primary, destination, error));
    REQUIRE(destination == before);

    writeBytes(primary, std::string(1024U * 1024U + 1U, 'x'));
    REQUIRE(!tribe::CampaignMigration::loadLegacyReadOnly(primary, destination, error));
    REQUIRE(error.find("1 MiB") != std::string::npos);
    REQUIRE(destination == before);
}
