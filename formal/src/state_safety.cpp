#include "state_safety.hpp"

#include <array>

namespace tribe::state_safety {
namespace {

/// 用途：验证 UTF-8 编码和不可显示控制字符。输入：文本。输出：是否安全；无状态修改。
/// 失败：截断、过长编码、代理区、非法码点或 C0/DEL 返回 false；不变量：循环每步都推进至少一字节。
bool validUtf8(const std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first < 0x80U) {
            // ASCII C0、DEL 和 ESC 都可能控制终端，不能保存或显示。
            if (first < 0x20U || first == 0x7FU) return false;
            ++index;
            continue;
        }
        std::size_t bytes = 0;
        std::uint32_t codePoint = 0;
        if (first >= 0xC2U && first <= 0xDFU) {
            bytes = 2U;
            codePoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            bytes = 3U;
            codePoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            bytes = 4U;
            codePoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + bytes > text.size()) return false;
        for (std::size_t offset = 1; offset < bytes; ++offset) {
            const unsigned char next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        const bool overlong = (bytes == 2U && codePoint < 0x80U) || (bytes == 3U && codePoint < 0x800U) ||
                              (bytes == 4U && codePoint < 0x10000U);
        if (overlong || (codePoint >= 0xD800U && codePoint <= 0xDFFFU) || codePoint > 0x10FFFFU) return false;
        index += bytes;
    }
    return true;
}

/// 用途：为单个持久化字段生成可读的安全性错误。输入：文本、字段标签和错误输出。输出：是否安全。
/// 状态影响：失败时写 error。失败：UTF-8 或终端安全检查失败；不变量：不修改原始文本。
bool validateText(const std::string_view text, const std::string_view label, std::string& error) {
    if (isSafeDisplayText(text)) return true;
    error = std::string(label) + "包含非法 UTF-8、控制字符或 ANSI 转义序列。";
    return false;
}

/// 用途：验证物品标识与名称。输入：物品、标签和错误输出。输出：是否安全；无游戏状态修改。
/// 失败：任一文本非法返回 false。不变量：复用 validateText 保持错误边界一致。
bool validateItemText(const Item& item, const std::string_view label, std::string& error) {
    return validateText(item.id, std::string(label) + "编号", error) &&
           validateText(item.name, std::string(label) + "名称", error);
}

/// 用途：验证角色姓名及全部装备文本。输入：角色、标签和错误输出。输出：是否安全；无游戏状态修改。
/// 失败：姓名或任一装备非法返回 false。不变量：空装备槽不参与校验。
bool validateCharacterText(const Character& character, const std::string_view label, std::string& error) {
    if (!validateText(character.name, std::string(label) + "姓名", error)) return false;
    for (const auto& equipment : character.equipment)
        if (equipment && !validateItemText(*equipment, label, error)) return false;
    return true;
}

/// 用途：验证活动任务的小队与背包文本。输入：任务和错误输出。输出：是否安全；无游戏状态修改。
/// 失败：任务名称、角色或背包物品非法返回 false。不变量：覆盖任务中所有玩家可见文本。
bool validateMissionText(const ExpansionState& mission, std::string& error) {
    if (!validateText(mission.squad.name, "任务小队名称", error)) return false;
    for (const Character& member : mission.squad.members)
        if (!validateCharacterText(member, "任务角色", error)) return false;
    for (const Item& item : mission.backpack.items())
        if (!validateItemText(item, "任务背包物品", error)) return false;
    return true;
}

} // namespace

bool isSafeDisplayText(const std::string_view text) { return validUtf8(text); }

bool validatePersistentText(const GameState& state, std::string& error) {
    if (!validateText(state.tribeName, "部落名", error) || !validateText(state.leaderName, "首领名", error) ||
        !validateText(state.actingLeaderName, "代理首领名", error) ||
        !validateText(state.leaderFocus, "首领方向", error) ||
        !validateText(state.workshopSupervisor, "工坊负责人", error) ||
        !validateText(state.healerSupervisor, "医者负责人", error) ||
        !validateText(state.war.commander, "军队统帅", error))
        return false;

    for (const TribeProfile& profile : state.tribes) {
        if (!validateText(profile.name, "部落档案名称", error) || !validateText(profile.leader, "部落首领", error) ||
            !validateText(profile.actingLeader, "部落代理首领", error) ||
            !validateText(profile.successor, "部落继承人", error) ||
            !validateText(profile.personality, "部落性格", error))
            return false;
        for (const FactionState& faction : profile.factions)
            if (!validateText(faction.name, "部落派系名称", error) ||
                !validateText(faction.demand, "部落派系诉求", error) ||
                !validateText(faction.candidate, "部落派系候选人", error))
                return false;
    }
    for (const FactionState& faction : state.playerFactions)
        if (!validateText(faction.name, "玩家派系名称", error) ||
            !validateText(faction.demand, "玩家派系诉求", error) ||
            !validateText(faction.candidate, "玩家派系候选人", error))
            return false;
    for (const Character& character : state.roster)
        if (!validateCharacterText(character, "角色", error)) return false;
    for (const PermanentSquad& squad : state.squads) {
        if (!validateText(squad.name, "永久小队名称", error) || !validateText(squad.captain, "永久小队队长", error))
            return false;
        for (const std::string& member : squad.members)
            if (!validateText(member, "永久小队成员", error)) return false;
    }
    for (const Item& item : state.stockpile)
        if (!validateItemText(item, "仓库物品", error)) return false;
    for (const Item& item : state.war.lockedEquipment)
        if (!validateItemText(item, "战争锁定物品", error)) return false;
    for (const std::string& entry : state.leadershipHistory)
        if (!validateText(entry, "首领历史", error)) return false;
    for (const ChronicleEntry& entry : state.chronicle)
        if (!validateText(entry.title, "编年史标题", error) || !validateText(entry.detail, "编年史内容", error))
            return false;
    return !state.activeMission || validateMissionText(*state.activeMission, error);
}

} // namespace tribe::state_safety
