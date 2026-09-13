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

std::uint32_t readU32(const std::string& bytes, const std::size_t offset) {
    if (offset + 4U > bytes.size()) throw std::runtime_error("save fixture header was truncated");
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8U);
    return value;
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

// v5 与 v6 仅在 GameState 尾部的 nextItemSerial 不同；该夹具保留 v5 的原始布局和校验和。
// 以 v6 序列化字节构造历史 v5 样本：移除 nextItemSerial、改写版本并重算校验和，保留其余布局。
std::string asV5(const std::string& v6) {
    constexpr std::size_t kHeaderBytes = 20U;
    if (v6.size() < kHeaderBytes + 4U || readU32(v6, 8U) != static_cast<std::uint32_t>(tribe::kSaveVersion))
        throw std::runtime_error("fixture was not a v6 save");
    const std::uint32_t payloadSize = readU32(v6, 12U);
    if (v6.size() != kHeaderBytes + payloadSize || payloadSize < 4U)
        throw std::runtime_error("v6 fixture payload was malformed");
    std::string legacy = v6.substr(0U, kHeaderBytes + payloadSize - 4U);
    const std::uint32_t legacyPayloadSize = payloadSize - 4U;
    writeU32(legacy, 8U, 5U);
    writeU32(legacy, 12U, legacyPayloadSize);
    writeU32(legacy, 16U, checksum(std::string_view{legacy}.substr(kHeaderBytes)));
    return legacy;
}

void writeV5Fixture(const tribe::SaveRepository& saves, const tribe::SaveSlot slot, const std::filesystem::path& target,
                    const tribe::GameState& state) {
    std::string error;
    REQUIRE(saves.save(state, slot, error));
    writeBytes(target, asV5(readBytes(saves.pathFor(slot))));
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

TEST_CASE("v6 codec keeps deterministic bytes after load and re-save through a different slot") {
    TemporarySaveDirectory directory{"v6-byte-stability"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;
    const tribe::GameState original = gameState(218U);

    REQUIRE(saves.save(original, tribe::SaveSlot::Slot1, error));
    const std::string fixture = readBytes(saves.pathFor(tribe::SaveSlot::Slot1));
    REQUIRE(readU32(fixture, 8U) == static_cast<std::uint32_t>(tribe::kSaveVersion));
    tribe::GameState loaded = gameState(299U);
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(saves.save(loaded, tribe::SaveSlot::Slot2, error));
    REQUIRE(readBytes(saves.pathFor(tribe::SaveSlot::Slot2)) == fixture);
}

TEST_CASE("stockpile growth beyond the legacy 64-item decoder limit remains saveable") {
    TemporarySaveDirectory directory{"stockpile-growth"};
    tribe::SaveRepository saves{directory.root()};
    tribe::GameState state = gameState(219U);
    for (int index = 0; index < 65; ++index) {
        tribe::Item item;
        item.id = "bulk_" + std::to_string(index);
        item.name = "批量制造物" + std::to_string(index);
        item.weight = 1;
        state.stockpile.push_back(std::move(item));
    }

    std::string error;
    REQUIRE(saves.save(state, tribe::SaveSlot::Slot1, error));
    tribe::GameState loaded = gameState(299U);
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(loaded.stockpile.size() == state.stockpile.size());
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

TEST_CASE("valid v5 primary backup and temporary saves migrate atomically to v6 and preserve original bytes") {
    TemporarySaveDirectory directory{"v5-migration"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;

    const tribe::GameState primaryState = gameState(231U);
    const std::filesystem::path primary = saves.pathFor(tribe::SaveSlot::Slot1);
    writeV5Fixture(saves, tribe::SaveSlot::Slot1, primary, primaryState);
    const std::string primaryV5 = readBytes(primary);
    tribe::GameState loaded = gameState(299U);
    tribe::SaveLoadInfo primaryInfo;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error, &primaryInfo));
    REQUIRE(primaryInfo.migratedFromV5);
    REQUIRE(loaded.seed == primaryState.seed);
    REQUIRE(readBytes(primaryInfo.legacyBackupPath) == primaryV5);
    REQUIRE(readU32(readBytes(primary), 8U) == static_cast<std::uint32_t>(tribe::kSaveVersion));

    const tribe::GameState backupState = gameState(232U);
    const std::filesystem::path backupPrimary = saves.pathFor(tribe::SaveSlot::Slot2);
    std::filesystem::path backup = backupPrimary;
    backup += ".bak";
    writeV5Fixture(saves, tribe::SaveSlot::Slot2, backup, backupState);
    writeBytes(backupPrimary, "damaged primary");
    const std::string backupV5 = readBytes(backup);
    tribe::SaveLoadInfo backupInfo;
    REQUIRE(saves.load(tribe::SaveSlot::Slot2, loaded, error, &backupInfo));
    REQUIRE(backupInfo.migratedFromV5);
    REQUIRE(loaded.seed == backupState.seed);
    REQUIRE(readBytes(backupInfo.legacyBackupPath) == backupV5);
    REQUIRE(readU32(readBytes(backupPrimary), 8U) == static_cast<std::uint32_t>(tribe::kSaveVersion));

    const tribe::GameState temporaryState = gameState(233U);
    const std::filesystem::path temporaryPrimary = saves.pathFor(tribe::SaveSlot::Slot3);
    std::filesystem::path temporary = temporaryPrimary;
    temporary += ".tmp";
    writeV5Fixture(saves, tribe::SaveSlot::Slot3, temporary, temporaryState);
    writeBytes(temporaryPrimary, "damaged primary");
    const std::string temporaryV5 = readBytes(temporary);
    tribe::SaveLoadInfo temporaryInfo;
    REQUIRE(saves.load(tribe::SaveSlot::Slot3, loaded, error, &temporaryInfo));
    REQUIRE(temporaryInfo.migratedFromV5);
    REQUIRE(loaded.seed == temporaryState.seed);
    REQUIRE(readBytes(temporaryInfo.legacyBackupPath) == temporaryV5);
    REQUIRE(readU32(readBytes(temporaryPrimary), 8U) == static_cast<std::uint32_t>(tribe::kSaveVersion));
}

TEST_CASE(
    "damaged legacy and unsafe v6 text are rejected without changing caller state or creating migration backups") {
    TemporarySaveDirectory directory{"reject-damaged"};
    tribe::SaveRepository saves{directory.root()};
    std::string error;
    const std::filesystem::path legacyPath = saves.pathFor(tribe::SaveSlot::Slot4);
    writeV5Fixture(saves, tribe::SaveSlot::Slot4, legacyPath, gameState(241U));
    std::string damaged = readBytes(legacyPath);
    damaged[20] = static_cast<char>(static_cast<unsigned char>(damaged[20]) ^ 0x01U);
    writeBytes(legacyPath, damaged);
    tribe::GameState candidate = gameState(299U);
    const std::uint32_t originalSeed = candidate.seed;
    REQUIRE(!saves.load(tribe::SaveSlot::Slot4, candidate, error));
    REQUIRE(candidate.seed == originalSeed);
    std::filesystem::path legacyArchive = legacyPath;
    legacyArchive += ".v5.bak";
    REQUIRE(!std::filesystem::exists(legacyArchive));
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
