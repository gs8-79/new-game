#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

class TemporarySaveDirectory {
   public:
    explicit TemporarySaveDirectory(const std::string& label) {
        static unsigned int sequence = 0U;
        root_ = std::filesystem::temp_directory_path() / ("tribe-formal-" + label + "-" + std::to_string(++sequence));
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
        if (error) throw std::runtime_error("could not create test save directory: " + error.message());
    }

    ~TemporarySaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& root() const { return root_; }

   private:
    std::filesystem::path root_;
};

const tribe::SaveSummary& summaryFor(const std::vector<tribe::SaveSummary>& summaries, const tribe::SaveSlot slot) {
    for (const tribe::SaveSummary& summary : summaries)
        if (summary.slot == slot) return summary;
    throw std::runtime_error("save summary was missing a slot");
}

tribe::GameState gameState(const std::uint32_t seed) {
    return tribe::GameEngine{{tribe::GameMode::Quick, seed}}.state();
}

} // namespace

TEST_CASE("save repository reports all seven empty slots and a failed load leaves its candidate untouched") {
    TemporarySaveDirectory directory{"empty"};
    tribe::SaveRepository saves{directory.root()};
    const std::vector<tribe::SaveSummary> summaries = saves.inspect();
    REQUIRE(summaries.size() == 7U);
    for (const tribe::SaveSummary& summary : summaries) REQUIRE(summary.status == tribe::SaveStatus::Empty);

    tribe::GameState candidate = gameState(201U);
    const std::uint32_t originalSeed = candidate.seed;
    std::string error;
    REQUIRE(!saves.load(tribe::SaveSlot::Slot1, candidate, error));
    REQUIRE(candidate.seed == originalSeed);
    REQUIRE(!error.empty());
}

TEST_CASE("save repository round-trips every manual slot and the autosave") {
    TemporarySaveDirectory directory{"all-slots"};
    tribe::SaveRepository saves{directory.root()};
    constexpr std::array<tribe::SaveSlot, 7> slots{
        {tribe::SaveSlot::Autosave, tribe::SaveSlot::Slot1, tribe::SaveSlot::Slot2, tribe::SaveSlot::Slot3,
         tribe::SaveSlot::Slot4, tribe::SaveSlot::Slot5, tribe::SaveSlot::Slot6}};
    std::string error;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const tribe::GameState state = gameState(static_cast<std::uint32_t>(210U + index));
        REQUIRE(saves.save(state, slots[index], error));
    }

    const std::vector<tribe::SaveSummary> summaries = saves.inspect();
    for (std::size_t index = 0; index < slots.size(); ++index) {
        REQUIRE(summaryFor(summaries, slots[index]).status == tribe::SaveStatus::Ready);
        tribe::GameState loaded = gameState(299U);
        REQUIRE(saves.load(slots[index], loaded, error));
        REQUIRE(loaded.seed == static_cast<std::uint32_t>(210U + index));
    }
}

TEST_CASE("a corrupt primary recovers its previous backup and a temporary file when it is the only valid copy") {
    TemporarySaveDirectory directory{"recovery"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;

    const tribe::GameState first = gameState(221U);
    const tribe::GameState second = gameState(222U);
    REQUIRE(saves.save(first, tribe::SaveSlot::Slot1, error));
    REQUIRE(saves.save(second, tribe::SaveSlot::Slot1, error));
    const std::filesystem::path backupPrimary = saves.pathFor(tribe::SaveSlot::Slot1);
    {
        std::ofstream damaged(backupPrimary, std::ios::binary | std::ios::trunc);
        damaged << "damaged";
        REQUIRE(static_cast<bool>(damaged));
    }
    REQUIRE(summaryFor(saves.inspect(), tribe::SaveSlot::Slot1).status == tribe::SaveStatus::Recoverable);
    tribe::GameState recovered = gameState(299U);
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, recovered, error));
    REQUIRE(recovered.seed == first.seed);
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, recovered, error));
    REQUIRE(recovered.seed == first.seed);

    const tribe::GameState temporaryState = gameState(223U);
    REQUIRE(saves.save(temporaryState, tribe::SaveSlot::Slot2, error));
    const std::filesystem::path temporaryPrimary = saves.pathFor(tribe::SaveSlot::Slot2);
    std::filesystem::path temporary = temporaryPrimary;
    temporary += ".tmp";
    std::filesystem::copy_file(temporaryPrimary, temporary, std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream damaged(temporaryPrimary, std::ios::binary | std::ios::trunc);
        damaged << "damaged";
        REQUIRE(static_cast<bool>(damaged));
    }
    REQUIRE(summaryFor(saves.inspect(), tribe::SaveSlot::Slot2).status == tribe::SaveStatus::Recoverable);
    REQUIRE(saves.load(tribe::SaveSlot::Slot2, recovered, error));
    REQUIRE(recovered.seed == temporaryState.seed);
}
