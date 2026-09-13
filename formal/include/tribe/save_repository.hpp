#pragma once

#include "tribe/game_state.hpp"

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

/// 用途：承载读取成功时的非状态信息。输入/输出：由存档仓库写入、界面只读展示。
/// 状态影响：无。失败：未迁移时保持默认值。不变量：迁移提示不影响加载出的游戏状态。
struct SaveLoadInfo {
    bool migratedFromV5 = false;
    std::filesystem::path legacyBackupPath;
};

class SaveRepository {
   public:
    /// 用途：绑定一个独立存档根目录。输入：目录路径。输出：仓库对象。
    /// 状态影响：保存根路径。失败：文件错误由后续读写返回。不变量：槽位路径始终位于根目录内。
    explicit SaveRepository(std::filesystem::path root);

    /// 用途：将完整合法状态原子写入槽位。输入：状态、槽位和错误输出。输出：是否成功。
    /// 状态影响：成功时轮换 .bak 并替换主档。失败：保留已有可恢复副本。不变量：不写入未通过校验的数据。
    bool save(const GameState& state, SaveSlot slot, std::string& error) const;
    /// 用途：读取槽位的最佳可恢复副本，并保留不含迁移提示的兼容入口。输出：是否成功；成功仅写 candidate。
    /// 失败：candidate 保持原值。不变量：只返回完整合法状态，不修改当前引擎或改变既有调用方式。
    bool load(SaveSlot slot, GameState& candidate, std::string& error) const;
    /// 用途：读取槽位并可选报告 v5→v6 迁移信息。输出：是否成功和迁移位置。
    /// 状态影响：成功迁移时创建 .v5.bak。失败：主档和 candidate 均不污染；仅完整合法的 v5 档会设置 migrationInfo。
    bool load(SaveSlot slot, GameState& candidate, std::string& error, SaveLoadInfo* migrationInfo) const;
    /// 用途：只读检查七个槽位及可恢复来源。输出：摘要列表；无游戏状态修改。
    std::vector<SaveSummary> inspect() const;
    /// 用途：计算指定槽位主档路径。输出：根目录内路径；无文件和状态修改。
    std::filesystem::path pathFor(SaveSlot slot) const;

    /// 用途：解析 1 至 7 的槽位文本。输出：槽位或空值；无状态修改。
    static std::optional<SaveSlot> parseSlot(std::string_view text);
    /// 用途：生成槽位显示名称。输出：文本；无状态修改。
    static std::string slotName(SaveSlot slot);

   private:
    std::filesystem::path root_;
};

} // namespace tribe
