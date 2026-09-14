#pragma once

#include "tribe/game_engine.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace tribe {

struct EndingPresentationOptions {
    bool animated = true;
    bool ansiEnabled = false;
    bool clearBetweenFrames = true;
    /// 用途：结局文本和动画帧的输出列宽。输入：终端列数。输出：按列折行；无游戏状态修改。
    std::size_t width = 80U;
    std::chrono::milliseconds frameDelay{180};
    std::function<void(std::chrono::milliseconds)> wait;
    /// 用途：由调用方请求跳过结局动画。输入：无。输出：true 时停止动画并渲染最终帧和摘要。
    /// 状态影响：无；缺失时交互终端使用平台非阻塞按键轮询。不变量：回调不得修改 GameEngine 状态。
    std::function<bool()> skipRequested;
};

class EndingPresentation {
   public:
    /// 用途：生成结局动画帧。输入：结局。输出：逐帧 UTF-8 文本。
    /// 状态影响：无。失败：未知结局生成保守帧。不变量：不修改 GameEngine 或输出流。
    static std::vector<std::string> framesFor(GameEnding ending);
    /// 用途：生成无动画环境的结局静态文本。输入：结局。输出：UTF-8 文本；无状态修改。
    /// 失败：未知结局返回保守提示。不变量：文本不含 ANSI 控制序列。
    static std::string renderStatic(GameEnding ending);
    /// 用途：格式化重要编年史。输入：编年史列表。输出：展示文本；无状态修改。
    /// 失败：空列表返回提示。不变量：不修改条目顺序或内容。
    static std::string formatChronicle(const std::vector<ChronicleEntry>& entries);
    /// 用途：格式化结局统计摘要。输入：结局摘要。输出：展示文本；无状态修改。
    /// 失败：缺失统计以保守占位显示。不变量：不修改 summary。
    static std::string formatSummary(const EndingSummary& summary);

    /// 用途：向输出流播放或静态渲染结局。输入：摘要、流和选项。输出：无。
    /// 状态影响：仅输出流和选项回调；失败：流错误不改变游戏状态；不变量：非 ANSI 模式不得输出控制序列。
    static void play(const EndingSummary& summary, std::ostream& output, EndingPresentationOptions options = {});
};

} // namespace tribe
