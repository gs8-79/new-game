#pragma once

#include "tribe/game_engine.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tribe {

enum class SaveSlot { Slot1 = 0, Slot2, Slot3, Slot4, Slot5, Slot6, Autosave };
enum class SaveStatus { Empty, Ready, Recoverable, Corrupt };

struct SaveSummary {
    SaveSlot slot = SaveSlot::Slot1;
    SaveStatus status = SaveStatus::Empty;
    std::string modifiedAt;
    std::string mode;
    std::string phase;
    std::string tribeName;
    std::string leaderName;
    int season = 0;
    int seasonLimit = 0;
    int population = 0;
    int food = 0;
    int wood = 0;
    int stone = 0;
    int herbs = 0;
};

// 读取成功时附带的非状态信息；迁移提示不影响加载出的游戏状态。
struct SaveLoadInfo {
    bool migratedFromV5 = false;
    std::filesystem::path legacyBackupPath;
};

class SaveRepository {
   public:
    explicit SaveRepository(std::filesystem::path root);

    bool save(const GameState& state, SaveSlot slot, std::string& error) const;
    // 保留原有三参数接口，避免调用方因新增迁移提示而改变命令或 ABI。
    bool load(SaveSlot slot, GameState& candidate, std::string& error) const;
    // 读取槽位；仅在完整合法的 v5 档成功升级后设置 migrationInfo。
    bool load(SaveSlot slot, GameState& candidate, std::string& error, SaveLoadInfo* migrationInfo) const;
    std::vector<SaveSummary> inspect() const;
    std::filesystem::path pathFor(SaveSlot slot) const;

    static std::optional<SaveSlot> parseSlot(std::string_view text);
    static std::string slotName(SaveSlot slot);

   private:
    std::filesystem::path root_;
};

} // namespace tribe
