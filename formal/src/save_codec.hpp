#pragma once

#include "tribe/game_state.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace tribe::save_codec {

/// 用途：按固定 v6 字段顺序编码完整状态。输入：已验证状态、字节输出和错误。输出：是否成功。
/// 状态影响：仅成功写入 fileData。失败：大小或状态异常时返回 false；不变量：输出字节必须确定且可复解析。
bool serialize(const GameState& state, std::string& fileData, std::string& error);
/// 用途：校验魔数、版本、长度和校验和后解码 v5/v6 文件。输入：文件字节、候选状态、错误和可选来源版本。
/// 状态影响：仅成功写入 candidate。失败：candidate 保持原值；不变量：拒绝尾随字节、非法文本和不完整状态。
bool deserialize(std::string_view fileData, GameState& candidate, std::string& error,
                 std::uint32_t* sourceVersion = nullptr);

} // namespace tribe::save_codec
