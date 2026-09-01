#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path reliabilityRoot(const std::string& name) {
    return std::filesystem::current_path() / ("formal-save-reliability-" + name);
}

void clean(const std::filesystem::path& root) {
    std::error_code code;
    std::filesystem::remove_all(root, code);
}

tribe::GameState changedState(const std::uint32_t seed) {
    tribe::GameEngine engine({tribe::GameMode::Standard, seed});
    const auto result = engine.execute("gather wood");
    REQUIRE(result.stateChanged);
    return engine.state();
}

} // namespace

TEST_CASE("formal save load recovers a valid backup when the primary is damaged") {
    const auto root = reliabilityRoot("backup");
    clean(root);
    tribe::SaveRepository repository(root);
    const tribe::GameState expected = changedState(101U);
    std::string error;
    REQUIRE(repository.save(expected, tribe::SaveSlot::Slot1, error));

    const auto primary = repository.pathFor(tribe::SaveSlot::Slot1);
    auto backup = primary;
    backup += ".bak";
    std::error_code code;
    std::filesystem::copy_file(primary, backup, std::filesystem::copy_options::overwrite_existing, code);
    REQUIRE(!code);
    {
        std::ofstream output(primary, std::ios::binary | std::ios::trunc);
        output << "format=tribe-dawn\nversion=1\npopulation=broken\n";
    }

    tribe::GameState recovered;
    REQUIRE(repository.load(tribe::SaveSlot::Slot1, recovered, error));
    REQUIRE(recovered == expected);
    REQUIRE(std::filesystem::is_regular_file(primary));

    tribe::GameState reloaded;
    REQUIRE(repository.load(tribe::SaveSlot::Slot1, reloaded, error));
    REQUIRE(reloaded == expected);
    clean(root);
}

TEST_CASE("formal save load recovers a complete temporary file after an interrupted replacement") {
    const auto root = reliabilityRoot("temporary");
    clean(root);
    tribe::SaveRepository repository(root);
    const tribe::GameState expected = changedState(103U);
    std::string error;
    REQUIRE(repository.save(expected, tribe::SaveSlot::Slot2, error));

    const auto primary = repository.pathFor(tribe::SaveSlot::Slot2);
    auto temporary = primary;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::rename(primary, temporary, code);
    REQUIRE(!code);

    tribe::GameState recovered;
    REQUIRE(repository.load(tribe::SaveSlot::Slot2, recovered, error));
    REQUIRE(recovered == expected);
    REQUIRE(std::filesystem::is_regular_file(primary));
    clean(root);
}

TEST_CASE("formal failed save leaves the last committed primary unchanged") {
    const auto root = reliabilityRoot("failure");
    clean(root);
    tribe::SaveRepository repository(root);
    tribe::GameEngine originalEngine({tribe::GameMode::Standard, 105U});
    const tribe::GameState original = originalEngine.state();
    std::string error;
    REQUIRE(repository.save(original, tribe::SaveSlot::Slot3, error));

    const auto primary = repository.pathFor(tribe::SaveSlot::Slot3);
    auto temporary = primary;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::create_directories(temporary, code);
    REQUIRE(!code);
    {
        std::ofstream guard(temporary / "keep.txt", std::ios::binary | std::ios::trunc);
        guard << "prevent temporary-directory removal";
    }

    const tribe::GameState replacement = changedState(105U);
    REQUIRE(!repository.save(replacement, tribe::SaveSlot::Slot3, error));
    REQUIRE(!error.empty());

    tribe::GameState loaded;
    REQUIRE(repository.load(tribe::SaveSlot::Slot3, loaded, error));
    REQUIRE(loaded == original);
    clean(root);
}
