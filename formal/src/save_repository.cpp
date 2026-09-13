#include "tribe/save_repository.hpp"

#include "tribe/game_engine.hpp"

#include "save_codec.hpp"
#include "save_file_transaction.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace tribe {
namespace {

using save_file_transaction::LoadFileStatus;

/// 用途：判断枚举是否为受支持的七个槽位。输入：槽位。输出：布尔值；无状态修改。
/// 失败：越界值返回 false。不变量：不会把未知枚举映射到磁盘路径。
bool validSlot(const SaveSlot slot) {
    const int value = static_cast<int>(slot);
    return value >= static_cast<int>(SaveSlot::Slot1) && value <= static_cast<int>(SaveSlot::Autosave);
}

/// 用途：生成文件最后修改时间的展示文本。输入：文件路径。输出：本地时间或“时间未知”；无状态修改。
/// 失败：读取或时区转换失败返回保守文本。不变量：不因展示失败阻断存档检查。
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

/// 用途：把已验证状态转换为存档菜单摘要。输入：槽位、状态、来源和状态快照。输出：摘要；无状态修改。
/// 失败：无。不变量：仅复制展示字段，不触发恢复、迁移或任何文件写入。
SaveSummary summaryFor(const SaveSlot slot, const SaveStatus status, const std::filesystem::path& source,
                       const GameState& state) {
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
        case SaveSlot::Slot1:
            return root_ / "slot1.sav";
        case SaveSlot::Slot2:
            return root_ / "slot2.sav";
        case SaveSlot::Slot3:
            return root_ / "slot3.sav";
        case SaveSlot::Slot4:
            return root_ / "slot4.sav";
        case SaveSlot::Slot5:
            return root_ / "slot5.sav";
        case SaveSlot::Slot6:
            return root_ / "slot6.sav";
        case SaveSlot::Autosave:
            return root_ / "autosave.sav";
    }
    return root_ / "invalid.sav";
}

bool SaveRepository::save(const GameState& state, const SaveSlot slot, std::string& error) const {
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    if (!GameEngine::validateState(state, error)) return false;

    std::string fileData;
    if (!save_codec::serialize(state, fileData, error)) return false;
    GameState memoryRoundTrip;
    if (!save_codec::deserialize(fileData, memoryRoundTrip, error)) {
        error = "存档写入前的解析校验失败：" + error;
        return false;
    }

    return save_file_transaction::writeAndVerify(pathFor(slot), fileData, save_file_transaction::verifyV6Temporary,
                                                 error);
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate, std::string& error) const {
    return load(slot, candidate, error, nullptr);
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate, std::string& error,
                          SaveLoadInfo* migrationInfo) const {
    if (migrationInfo != nullptr) *migrationInfo = {};
    if (!validSlot(slot)) {
        error = "存档槽编号无效。";
        return false;
    }
    const std::filesystem::path path = pathFor(slot);
    GameState parsed;
    std::string primaryError;
    std::uint32_t primaryVersion = 0U;
    const LoadFileStatus primaryStatus = save_file_transaction::load(path, parsed, primaryError, &primaryVersion);
    if (primaryStatus == save_file_transaction::LoadFileStatus::Loaded) {
        if (primaryVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!save_file_transaction::migrateV5(path, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo != nullptr) *migrationInfo = {true, std::move(legacyBackup)};
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }
    if (primaryStatus == save_file_transaction::LoadFileStatus::Unavailable) {
        error = "读取" + slotName(slot) + "失败。主文件暂时不可用：" + primaryError +
                "。为避免误读旧备份，本次没有自动回退。";
        return false;
    }

    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    std::string backupError;
    std::uint32_t backupVersion = 0U;
    if (save_file_transaction::load(backup, parsed, backupError, &backupVersion) ==
        save_file_transaction::LoadFileStatus::Loaded) {
        if (backupVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!save_file_transaction::migrateV5(backup, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo != nullptr) *migrationInfo = {true, std::move(legacyBackup)};
        } else if (!save_file_transaction::restoreRecovered(backup, path)) {
            error = "已验证" + backup.filename().string() + "，但恢复主档失败；当前游戏状态未改变。";
            return false;
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    std::string temporaryError;
    std::uint32_t temporaryVersion = 0U;
    if (save_file_transaction::load(temporary, parsed, temporaryError, &temporaryVersion) ==
        save_file_transaction::LoadFileStatus::Loaded) {
        if (temporaryVersion == 5U) {
            std::filesystem::path legacyBackup;
            if (!save_file_transaction::migrateV5(temporary, path, parsed, error, legacyBackup)) return false;
            if (migrationInfo != nullptr) *migrationInfo = {true, std::move(legacyBackup)};
        } else if (!save_file_transaction::restoreRecovered(temporary, path)) {
            error = "已验证" + temporary.filename().string() + "，但恢复主档失败；当前游戏状态未改变。";
            return false;
        }
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    error = "读取" + slotName(slot) + "失败。主文件：" + primaryError + "；备份文件：" + backupError + "；临时文件：" +
            temporaryError;
    return false;
}

std::vector<SaveSummary> SaveRepository::inspect() const {
    // 列表只解析文件，不调用可能修复主档的 load；候选顺序与实际读取一致。
    constexpr std::array<SaveSlot, 7> slots{{SaveSlot::Autosave, SaveSlot::Slot1, SaveSlot::Slot2, SaveSlot::Slot3,
                                             SaveSlot::Slot4, SaveSlot::Slot5, SaveSlot::Slot6}};
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
        const LoadFileStatus primaryStatus = save_file_transaction::load(primary, state, primaryError);
        if (primaryStatus == save_file_transaction::LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Ready, primary, state));
            continue;
        }
        if (primaryStatus == save_file_transaction::LoadFileStatus::Unavailable) {
            SaveSummary summary;
            summary.slot = slot;
            summary.status = SaveStatus::Corrupt;
            summaries.push_back(std::move(summary));
            continue;
        }

        std::string backupError;
        const LoadFileStatus backupStatus = save_file_transaction::load(backup, state, backupError);
        if (backupStatus == save_file_transaction::LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, backup, state));
            continue;
        }
        std::string temporaryError;
        const LoadFileStatus temporaryStatus = save_file_transaction::load(temporary, state, temporaryError);
        if (temporaryStatus == save_file_transaction::LoadFileStatus::Loaded) {
            summaries.push_back(summaryFor(slot, SaveStatus::Recoverable, temporary, state));
            continue;
        }

        SaveSummary summary;
        summary.slot = slot;
        const bool allMissing = primaryStatus == save_file_transaction::LoadFileStatus::Missing &&
                                backupStatus == save_file_transaction::LoadFileStatus::Missing &&
                                temporaryStatus == save_file_transaction::LoadFileStatus::Missing;
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
        case SaveSlot::Slot1:
            return "手动存档1";
        case SaveSlot::Slot2:
            return "手动存档2";
        case SaveSlot::Slot3:
            return "手动存档3";
        case SaveSlot::Slot4:
            return "手动存档4";
        case SaveSlot::Slot5:
            return "手动存档5";
        case SaveSlot::Slot6:
            return "手动存档6";
        case SaveSlot::Autosave:
            return "自动存档";
    }
    return "未知存档";
}

} // namespace tribe
