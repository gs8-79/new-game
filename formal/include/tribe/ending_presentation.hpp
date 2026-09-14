#pragma once

#include "tribe/game_engine.hpp"

#include <chrono>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace tribe {

/// 用途：控制结局动画的播放方式，并允许测试替换等待与跳过回调。
/// 输入/输出：由调用方构造后传入 EndingPresentation::play。状态影响：无，play 只读取这些选项。
/// 失败：缺失回调时回退到真实休眠和平台按键轮询。不变量：任何选项组合都不得让 play 修改 GameEngine。
struct EndingPresentationOptions {
    /// 用途：是否逐帧播放动画。false 时只输出静态结局画面。
    bool animated = true;
    /// 用途：是否允许写出 ANSI 颜色与清屏序列。输入：调用方的终端能力判断。
    /// 不变量：重定向或非交互输出必须传入 false；play 不会自行探测输出流类型。
    bool ansiEnabled = false;
    /// 用途：帧间是否清屏重画。false 时改用空行分隔，适配不支持清屏的终端。
    bool clearBetweenFrames = true;
    /// 用途：每帧停留时长。为零或负值时 waitForNextFrame 直接返回而不等待。
    std::chrono::milliseconds frameDelay{180};
    /// 用途：替换真实休眠的等待回调，供测试注入以跳过动画耗时。
    /// 状态影响：由调用方实现。失败：缺失时使用 std::this_thread::sleep_for。
    std::function<void(std::chrono::milliseconds)> wait;
    /// 用途：由调用方请求跳过结局动画。输入：无。输出：true 时停止动画并渲染最终帧和摘要。
    /// 状态影响：无；缺失时交互终端使用平台非阻塞按键轮询。不变量：回调不得修改 GameEngine 状态。
    std::function<bool()> skipRequested;
};

/// 用途：生成并播放各结局（联盟、征服、繁荣、迁徙、覆灭与未结算）的 ASCII 演出和结算摘要。
/// 状态影响：只写输出流与终端模式，不修改 GameEngine。失败：输出流错误由调用方处理。
/// 非交互降级：调用方须将 animated 设为 false；类不会根据流类型自动切换为静态输出。
/// 不变量：帧与摘要均为纯文本，非 ANSI 模式不得混入控制序列。
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
