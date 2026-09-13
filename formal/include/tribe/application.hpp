#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>

namespace tribe {

/// 用途：驱动封面、存档菜单和主游戏的完整终端流程。输入：流、独立存档目录和终端选项。
/// 输出：进程退出码。状态影响：只经 SaveRepository 写入传入目录。失败：读写或命令失败显示提示且不污染游戏状态。
/// 不变量：空白和中英文命令统一解析，调用方已有存档目录不被替换。
int runApplication(std::istream& input, std::ostream& output, const std::filesystem::path& saveRoot, bool interactive,
                   bool ansiEnabled, std::size_t terminalWidth = 0U);

} // namespace tribe
