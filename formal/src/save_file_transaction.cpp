#include "save_file_transaction.hpp"
#include "save_codec.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace tribe::save_file_transaction {

/// 用途：按“临时写入→验证→备份→替换”的顺序提交存档字节。输入：目标路径、数据、验证器和错误。
/// 状态影响：成功轮换 .bak 与主档。失败：主档不变或恢复为原字节；不变量：验证未通过的数据绝不替换主档。
bool writeAndVerify(const std::filesystem::path& destination, const std::string_view data, const FileVerifier verifier,
                    std::string& error) {
    if (verifier == nullptr) {
        error = "存档事务缺少临时文件验证器。";
        return false;
    }
    std::error_code code;
    std::filesystem::create_directories(destination.parent_path(), code);
    if (code) {
        error = "无法创建存档目录：" + code.message();
        return false;
    }

    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    std::filesystem::path backup = destination;
    backup += ".bak";
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理旧的临时存档：" + code.message();
        return false;
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法打开临时存档文件。";
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            error = "写入临时存档失败。";
            return false;
        }
        output.close();
        if (!output) {
            error = "关闭临时存档失败。";
            return false;
        }
    }

    std::string temporaryError;
    if (!verifier(temporary, temporaryError)) {
        std::filesystem::remove(temporary, code);
        error = "临时存档解析校验失败：" + temporaryError;
        return false;
    }

    const bool hadOriginal = std::filesystem::exists(destination, code);
    if (code) {
        error = "无法检查旧存档：" + code.message();
        std::filesystem::remove(temporary, code);
        return false;
    }
    if (hadOriginal) {
        std::filesystem::remove(backup, code);
        if (code) {
            error = "无法清理旧备份：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
        std::filesystem::rename(destination, backup, code);
        if (code) {
            error = "无法备份旧存档：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
    }

    code.clear();
    std::filesystem::rename(temporary, destination, code);
    if (code) {
        const std::string renameError = code.message();
        if (hadOriginal) {
            std::error_code restoreCode;
            std::filesystem::rename(backup, destination, restoreCode);
            if (restoreCode) {
                std::filesystem::remove(temporary, code);
                error = "无法替换正式存档：" + renameError + "；恢复旧存档也失败：" + restoreCode.message() +
                        "。旧数据仍保留在" + backup.filename().string() + "。";
                return false;
            }
        }
        std::filesystem::remove(temporary, code);
        error = "无法替换正式存档：" + renameError;
        return false;
    }

    // 新主档已验证并成功提交；.bak 保留上一份主档，以便后续读取恢复。
    error.clear();
    return true;
}

} // namespace tribe::save_file_transaction
namespace tribe {
namespace {

using save_file_transaction::LoadFileStatus;
constexpr std::size_t kMaximumSaveBytes = 16U * 1024U * 1024U;

/// 用途：读取单个文件并区分缺失、损坏与不可用。输入：路径、候选状态、错误及可选版本。输出：文件状态。
/// 状态影响：仅 Loaded 时写 candidate。失败：路径或读写错误保持候选不变；不变量：读取大小受安全上限约束。
LoadFileStatus loadFile(const std::filesystem::path& path, GameState& candidate, std::string& error,
                        std::uint32_t* sourceVersion = nullptr) {
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查文件" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (!exists) {
        error = "文件不存在：" + path.filename().string();
        return LoadFileStatus::Missing;
    }
    if (!std::filesystem::is_regular_file(path, code) || code) {
        error = "存档路径不是可读取的普通文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "无法读取文件大小：" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (size == 0U || size > kMaximumSaveBytes) {
        error = "存档为空或超过安全大小限制：" + path.filename().string();
        return LoadFileStatus::Invalid;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        error = "读取存档内容失败：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    return save_codec::deserialize(data, candidate, error, sourceVersion) ? LoadFileStatus::Loaded
                                                                          : LoadFileStatus::Invalid;
}

/// 用途：复解析刚写入磁盘的 v6 临时文件。输入：临时路径与错误。输出：是否完整可读取。
/// 状态影响：无。失败：不修改主档；不变量：存档替换前必须通过与正常读取相同的校验链。
bool verifyTemporarySave(const std::filesystem::path& temporary, std::string& error) {
    GameState diskRoundTrip;
    return loadFile(temporary, diskRoundTrip, error) == LoadFileStatus::Loaded;
}

/// 用途：将已验证的 .bak/.tmp 副本原子恢复为主档。输入：来源和目标路径。输出：是否成功。
/// 状态影响：成功时替换目标主档。失败：清理本次 .recover 临时文件；不变量：只接受调用方已解析的来源。
bool restoreRecoveredFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::filesystem::path recovery = destination;
    recovery += ".recover";
    std::filesystem::path previous = destination;
    previous += ".recover-old";
    std::error_code code;
    std::filesystem::remove(recovery, code);
    if (code) return false;
    std::filesystem::remove(previous, code);
    if (code) return false;
    std::filesystem::copy_file(source, recovery, std::filesystem::copy_options::none, code);
    if (code) return false;
    const bool hadDestination = std::filesystem::exists(destination, code);
    if (code) {
        std::filesystem::remove(recovery, code);
        return false;
    }
    // 先把原主档移到回滚名，再以 rename 提交恢复副本；恢复失败时仍能把旧主档改回原名。
    if (hadDestination) {
        std::filesystem::rename(destination, previous, code);
        if (code) {
            std::filesystem::remove(recovery, code);
            return false;
        }
    }
    std::filesystem::rename(recovery, destination, code);
    if (code) {
        std::error_code restoreCode;
        if (hadDestination) std::filesystem::rename(previous, destination, restoreCode);
        std::filesystem::remove(recovery, restoreCode);
        return false;
    }
    if (hadDestination) std::filesystem::remove(previous, code);
    return true;
}

/// 用途：将旧档原始字节归档为独立 .v5.bak。输入：源、目标及错误。输出：是否成功。
/// 状态影响：成功时新建历史归档。失败：不覆盖已有归档；不变量：归档文件不参与 .bak/.tmp 自动恢复。
// 步骤：确认历史归档不存在→清理专用临时文件→复制原始字节→原子改名；任一步失败均不覆盖历史档。
bool copyFileAtomically(const std::filesystem::path& source, const std::filesystem::path& destination,
                        std::string& error) {
    std::error_code code;
    if (std::filesystem::exists(destination, code)) {
        if (code) {
            error = "无法检查 v5 历史备份：" + code.message();
        } else {
            error = "v5 历史备份已存在，拒绝覆盖：" + destination.filename().string();
        }
        return false;
    }
    if (code) {
        error = "无法检查 v5 历史备份：" + code.message();
        return false;
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理 v5 备份临时文件：" + code.message();
        return false;
    }
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::none, code);
    if (code) {
        error = "无法复制 v5 原始存档：" + code.message();
        return false;
    }
    std::filesystem::rename(temporary, destination, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        error = "无法提交 v5 历史备份：" + code.message();
        return false;
    }
    return true;
}

/// 用途：以验证过的 v6 临时档替换主档。输入：临时路径、目标路径和错误。输出：是否成功。
/// 状态影响：成功时提交目标主档。失败：尝试复原原主档；不变量：替换失败不得把有效主档静默丢失。
// 步骤：清理旧回滚副本→暂存旧主档→提交临时 v6→删除回滚副本；提交失败时立即把旧主档改回原名。
bool replaceMigratedPrimary(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                            std::string& error) {
    std::filesystem::path previous = destination;
    previous += ".v5-migration.old";
    std::error_code code;
    std::filesystem::remove(previous, code);
    if (code) {
        error = "无法清理迁移回滚文件：" + code.message();
        return false;
    }
    const bool hadPrimary = std::filesystem::exists(destination, code);
    if (code) {
        error = "无法检查迁移前主档：" + code.message();
        return false;
    }
    if (hadPrimary) {
        std::filesystem::rename(destination, previous, code);
        if (code) {
            error = "无法暂存迁移前主档：" + code.message();
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, code);
    if (code) {
        const std::string replaceError = code.message();
        if (hadPrimary) {
            std::error_code restoreCode;
            std::filesystem::rename(previous, destination, restoreCode);
            if (restoreCode) {
                error = "迁移替换失败且无法恢复原主档：" + replaceError + "；" + restoreCode.message();
                return false;
            }
        }
        error = "迁移替换主档失败：" + replaceError;
        return false;
    }
    if (hadPrimary) {
        std::filesystem::remove(previous, code);
        // 主档已成功提交时不能再把迁移报告为失败；遗留的回滚副本是可恢复的冗余文件。
    }
    return true;
}

/// 用途：将完整合法的 v5 文件安全升级为 v6。输入：来源、目标、已解析状态、错误和归档路径。输出：是否成功。
/// 状态影响：成功时替换目标并写 parsed/legacyBackup。失败：主档和 parsed 不变；不变量：历史归档保留原始字节。
// 步骤：内存序列化/复解析 v6→原子归档原始 v5→写入 v6 临时档→磁盘复解析→原子替换主档。
// 回滚点：任何替换前失败均删除本次临时档和归档；直到最后替换成功前，主档与 parsed 均保持旧值。
bool migrateV5File(const std::filesystem::path& source, const std::filesystem::path& destination, GameState& parsed,
                   std::string& error, std::filesystem::path& legacyBackup) {
    std::string v6Data;
    if (!save_codec::serialize(parsed, v6Data, error)) return false;
    GameState memoryRoundTrip;
    if (!save_codec::deserialize(v6Data, memoryRoundTrip, error) ||
        memoryRoundTrip.nextItemSerial != parsed.nextItemSerial) {
        error = "v5 升级前的 v6 内存复解析失败：" + error;
        return false;
    }

    legacyBackup = destination;
    legacyBackup += ".v5.bak";
    if (!copyFileAtomically(source, legacyBackup, error)) return false;

    std::filesystem::path temporary = destination;
    temporary += ".v6-migrate.tmp";
    std::error_code code;
    std::filesystem::remove(temporary, code);
    if (code) {
        error = "无法清理 v6 升级临时文件：" + code.message();
        std::filesystem::remove(legacyBackup, code);
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法写入 v6 升级临时文件。";
            std::filesystem::remove(legacyBackup, code);
            return false;
        }
        output.write(v6Data.data(), static_cast<std::streamsize>(v6Data.size()));
        output.close();
        if (!output) {
            error = "关闭 v6 升级临时文件失败。";
            std::filesystem::remove(temporary, code);
            std::filesystem::remove(legacyBackup, code);
            return false;
        }
    }
    GameState diskRoundTrip;
    std::string temporaryError;
    std::uint32_t version = 0U;
    if (loadFile(temporary, diskRoundTrip, temporaryError, &version) != LoadFileStatus::Loaded ||
        version != static_cast<std::uint32_t>(kSaveVersion)) {
        std::filesystem::remove(temporary, code);
        std::filesystem::remove(legacyBackup, code);
        error = "v6 升级临时文件复解析失败：" + temporaryError;
        return false;
    }
    if (!replaceMigratedPrimary(temporary, destination, error)) {
        std::filesystem::remove(temporary, code);
        std::filesystem::remove(legacyBackup, code);
        return false;
    }
    parsed = std::move(diskRoundTrip);
    return true;
}

} // namespace

namespace save_file_transaction {

/// 用途：读取并完整校验一个候选文件。输入：路径、候选状态、错误和可选版本。输出：文件状态。
/// 状态影响：仅 Loaded 时写 candidate。失败：候选状态保持原值；不变量：读取安全上限和编解码校验与正式加载一致。
LoadFileStatus load(const std::filesystem::path& path, GameState& candidate, std::string& error,
                    std::uint32_t* const sourceVersion) {
    return loadFile(path, candidate, error, sourceVersion);
}

/// 用途：将已验证的备份或临时文件恢复为主档。输入：来源、目标。输出：是否成功。
/// 状态影响：成功替换主档。失败：清理本次 .recover；不变量：来源必须由 load 成功验证。
bool restoreRecovered(const std::filesystem::path& source, const std::filesystem::path& destination) {
    return restoreRecoveredFile(source, destination);
}

/// 用途：复解析刚写入的 v6 临时文件。输入：临时路径和错误。输出：是否可提交。
/// 状态影响：无。失败：不修改主档；不变量：替换前必须通过正式读取链。
bool verifyV6Temporary(const std::filesystem::path& temporary, std::string& error) {
    return verifyTemporarySave(temporary, error);
}

/// 用途：把完整合法 v5 文件升级为 v6。输入：来源、目标、已解析状态、错误和归档路径。输出：是否成功。
/// 状态影响：成功提交 v6 主档并创建 .v5.bak。失败：主档和 parsed 保持原值；不变量：历史归档保留原始 v5 字节。
bool migrateV5(const std::filesystem::path& source, const std::filesystem::path& destination, GameState& parsed,
               std::string& error, std::filesystem::path& legacyBackup) {
    return migrateV5File(source, destination, parsed, error, legacyBackup);
}

} // namespace save_file_transaction
} // namespace tribe
