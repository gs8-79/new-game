#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>

#include "tribe/game_state.hpp"

namespace tribe::save_file_transaction {

/// 用途：在临时文件写入完成后验证其字节可恢复。输入：临时路径与错误输出。输出：是否可提交。
/// 状态影响：验证器不得修改主档。失败：返回 false 并写入 error；不变量：调用方只把已验证数据换入正式路径。
using FileVerifier = bool (*)(const std::filesystem::path& temporary, std::string& error);

/// 用途：描述单个候选文件的读取结论。输入/输出：恢复顺序内部使用；无游戏状态修改。
enum class LoadFileStatus { Loaded, Missing, Invalid, Unavailable };

/// 用途：读取并完整校验一个候选文件。输入：路径、候选状态、错误和可选版本。输出：文件状态。
/// 状态影响：仅 Loaded 时写 candidate。失败：候选状态保持原值；不变量：读取安全上限和编解码校验与正式加载一致。
LoadFileStatus load(const std::filesystem::path& path, GameState& candidate, std::string& error,
                    std::uint32_t* sourceVersion = nullptr);
/// 用途：将已验证的备份或临时文件恢复为主档。输入：来源、目标。输出：是否成功。
/// 状态影响：成功替换主档。失败：清理本次 .recover；不变量：来源必须由 load 成功验证。
bool restoreRecovered(const std::filesystem::path& source, const std::filesystem::path& destination);
/// 用途：复解析刚写入的 v6 临时文件。输入：临时路径和错误。输出：是否可提交。
/// 状态影响：无。失败：不修改主档；不变量：替换前必须通过正式读取链。
bool verifyV6Temporary(const std::filesystem::path& temporary, std::string& error);
/// 用途：把完整合法 v5 文件升级为 v6。输入：来源、目标、已解析状态、错误和归档路径。输出：是否成功。
/// 状态影响：成功提交 v6 主档并创建 .v5.bak。失败：主档和 parsed 保持原值；不变量：历史归档保留原始 v5 字节。
bool migrateV5(const std::filesystem::path& source, const std::filesystem::path& destination, GameState& parsed,
               std::string& error, std::filesystem::path& legacyBackup);

/// 用途：将已在内存完成编码的存档写入临时文件、验证并原子替换主档。输入：目标、字节、验证器和错误。
/// 状态影响：成功时轮换 .bak 并提交主档。失败：尽力恢复原主档且清理本次 .tmp；不变量：从不直接覆写唯一有效主档。
bool writeAndVerify(const std::filesystem::path& destination, std::string_view data, FileVerifier verifier,
                    std::string& error);

} // namespace tribe::save_file_transaction
