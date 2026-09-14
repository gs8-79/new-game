#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

// 为存档恢复夹具隔离唯一临时目录；析构只清理本测试根目录，保证不接触玩家存档。
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

void writeU32(std::string& bytes, const std::size_t offset, const std::uint32_t value) {
    if (offset + 4U > bytes.size()) throw std::runtime_error("save fixture header was truncated");
    for (std::size_t index = 0U; index < 4U; ++index)
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
}

std::uint32_t checksum(const std::string_view bytes) {
    std::uint32_t value = 2166136261U;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not read save fixture");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not write save fixture");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("could not close save fixture");
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

TEST_CASE("old v5 and v6 saves are rejected without migration or file changes") {
    TemporarySaveDirectory directory{"reject-old-saves"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;
    const std::filesystem::path path = saves.pathFor(tribe::SaveSlot::Slot1);
    REQUIRE(saves.save(gameState(231U), tribe::SaveSlot::Slot1, error));
    const std::string current = readBytes(path);
    for (const std::uint32_t version : {5U, 6U}) {
        std::string old = current;
        writeU32(old, 8U, version);
        writeU32(old, 16U, checksum(std::string_view{old}.substr(20U)));
        writeBytes(path, old);
        tribe::GameState loaded = gameState(299U);
        const std::uint32_t originalSeed = loaded.seed;
        tribe::SaveLoadInfo info;
        REQUIRE(!saves.load(tribe::SaveSlot::Slot1, loaded, error, &info));
        REQUIRE(loaded.seed == originalSeed);
        REQUIRE(!info.migratedFromV5);
        REQUIRE(error.find("需要新开局") != std::string::npos);
        REQUIRE(readBytes(path) == old);
    }
}

TEST_CASE("damaged v7 and unsafe text are rejected without changing caller state") {
    TemporarySaveDirectory directory{"reject-damaged"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;
    const std::filesystem::path legacyPath = saves.pathFor(tribe::SaveSlot::Slot4);
    REQUIRE(saves.save(gameState(241U), tribe::SaveSlot::Slot4, error));
    std::string damaged = readBytes(legacyPath);
    damaged[20] = static_cast<char>(static_cast<unsigned char>(damaged[20]) ^ 0x01U);
    writeBytes(legacyPath, damaged);
    tribe::GameState candidate = gameState(299U);
    const std::uint32_t originalSeed = candidate.seed;
    REQUIRE(!saves.load(tribe::SaveSlot::Slot4, candidate, error));
    REQUIRE(candidate.seed == originalSeed);
    REQUIRE(readBytes(legacyPath) == damaged);

    const std::filesystem::path unsafePath = saves.pathFor(tribe::SaveSlot::Slot5);
    REQUIRE(saves.save(gameState(242U), tribe::SaveSlot::Slot5, error));
    std::string unsafe = readBytes(unsafePath);
    const std::string tribeName = "燧火";
    const std::size_t nameOffset = unsafe.find(tribeName);
    REQUIRE(nameOffset != std::string::npos);
    unsafe[nameOffset] = '\x1b';
    writeU32(unsafe, 16U, checksum(std::string_view{unsafe}.substr(20U)));
    writeBytes(unsafePath, unsafe);
    REQUIRE(!saves.load(tribe::SaveSlot::Slot5, candidate, error));
    REQUIRE(candidate.seed == originalSeed);
    REQUIRE(error.find("控制字符") != std::string::npos);
}
