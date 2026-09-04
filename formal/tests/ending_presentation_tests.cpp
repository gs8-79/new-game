#include "tribe/ending_presentation.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <sstream>
#include <string>

namespace {

const std::array<tribe::CampaignEnding, 5> kCompletedEndings{{
    tribe::CampaignEnding::Alliance,
    tribe::CampaignEnding::Conquest,
    tribe::CampaignEnding::Prosperity,
    tribe::CampaignEnding::Migration,
    tribe::CampaignEnding::Extinction,
}};

tribe::EndingSummary sampleSummary() {
    return {tribe::CampaignEnding::Alliance,
        "联盟共主",
        "六族围坐在同一簇长火旁，旧日边界成为共同守望的道路。",
        {"人口：24", "盟友：3", "完成任务：8"},
        {"山河征服者：尚缺岩牙要塞", "燧火繁荣：尚缺一项技术"},
        {{2, 3, "河鹿来访", "双方交换了第一批种子。"},
            {12, 5, "长火盟誓", "六族共同守住了山口。"}}};
}

std::size_t occurrenceCount(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

} // namespace

TEST_CASE("ending presentation provides four to six distinct ASCII frames for every ending") {
    for (const auto ending : kCompletedEndings) {
        const auto frames = tribe::EndingPresentation::framesFor(ending);
        REQUIRE(frames.size() >= 4U);
        REQUIRE(frames.size() <= 6U);
        for (const auto& frame : frames) {
            REQUIRE(!frame.empty());
            for (const unsigned char character : frame) REQUIRE(character <= 0x7fU);
        }
        REQUIRE(frames.front() != frames.back());
        REQUIRE(tribe::EndingPresentation::renderStatic(ending) == frames.back());
    }
}

TEST_CASE("ending presentation static fallback emits only the final frame") {
    auto summary = sampleSummary();
    summary.ending = tribe::CampaignEnding::Migration;
    summary.title = "迁徙新生";
    const auto frames = tribe::EndingPresentation::framesFor(summary.ending);

    int waits = 0;
    int skipChecks = 0;
    tribe::EndingPresentationOptions options;
    options.animated = false;
    options.ansiEnabled = true;
    options.clearBetweenFrames = true;
    options.frameDelay = std::chrono::milliseconds{500};
    options.wait = [&waits](const std::chrono::milliseconds) { ++waits; };
    options.skipRequested = [&skipChecks]() {
        ++skipChecks;
        return true;
    };

    std::ostringstream output;
    tribe::EndingPresentation::play(summary, output, options);
    const std::string rendered = output.str();
    REQUIRE(rendered.find(frames.back()) != std::string::npos);
    REQUIRE(rendered.find(frames.front()) == std::string::npos);
    REQUIRE(rendered.find("\x1b[") == std::string::npos);
    REQUIRE(waits == 0);
    REQUIRE(skipChecks == 0);
}

TEST_CASE("ending presentation zero delay animation never waits") {
    const auto summary = sampleSummary();
    const auto frames = tribe::EndingPresentation::framesFor(summary.ending);
    int waits = 0;

    tribe::EndingPresentationOptions options;
    options.animated = true;
    options.ansiEnabled = false;
    options.frameDelay = std::chrono::milliseconds{0};
    options.wait = [&waits](const std::chrono::milliseconds) { ++waits; };

    std::ostringstream output;
    tribe::EndingPresentation::play(summary, output, options);
    const std::string rendered = output.str();
    REQUIRE(rendered.find(frames.front()) != std::string::npos);
    REQUIRE(rendered.find(frames.back()) != std::string::npos);
    REQUIRE(rendered.find("\x1b[") == std::string::npos);
    REQUIRE(waits == 0);
}

TEST_CASE("ending presentation skip immediately jumps from the first frame to the final frame") {
    const auto summary = sampleSummary();
    const auto frames = tribe::EndingPresentation::framesFor(summary.ending);
    int waits = 0;
    int skipChecks = 0;

    tribe::EndingPresentationOptions options;
    options.animated = true;
    options.ansiEnabled = false;
    options.frameDelay = std::chrono::milliseconds{100};
    options.wait = [&waits](const std::chrono::milliseconds) { ++waits; };
    options.skipRequested = [&skipChecks]() {
        ++skipChecks;
        return true;
    };

    std::ostringstream output;
    tribe::EndingPresentation::play(summary, output, options);
    const std::string rendered = output.str();
    REQUIRE(rendered.find(frames.front()) != std::string::npos);
    REQUIRE(rendered.find(frames[1]) == std::string::npos);
    REQUIRE(rendered.find(frames.back()) != std::string::npos);
    REQUIRE(rendered.find("结局：联盟共主") != std::string::npos);
    REQUIRE(waits == 0);
    REQUIRE(skipChecks == 1);
}

TEST_CASE("ending presentation polls for skip during a frame delay") {
    const auto summary = sampleSummary();
    const auto frames = tribe::EndingPresentation::framesFor(summary.ending);
    int skipChecks = 0;
    std::chrono::milliseconds waited{0};

    tribe::EndingPresentationOptions options;
    options.animated = true;
    options.ansiEnabled = true;
    options.clearBetweenFrames = true;
    options.frameDelay = std::chrono::milliseconds{100};
    options.wait = [&waited](const std::chrono::milliseconds duration) { waited += duration; };
    options.skipRequested = [&skipChecks]() { return ++skipChecks >= 2; };

    std::ostringstream output;
    tribe::EndingPresentation::play(summary, output, options);
    const std::string rendered = output.str();
    REQUIRE(waited.count() > 0);
    REQUIRE(waited < options.frameDelay);
    REQUIRE(skipChecks == 2);
    REQUIRE(rendered.find(frames[1]) == std::string::npos);
    REQUIRE(rendered.find(frames.back()) != std::string::npos);
    REQUIRE(occurrenceCount(rendered, "\x1b[2J\x1b[H") == 1U);
}

TEST_CASE("ending presentation applies distinct ANSI colors and honors the clear switch") {
    const std::array<std::string, 5> colors{{
        "\x1b[32m", "\x1b[31m", "\x1b[33m", "\x1b[36m", "\x1b[90m",
    }};

    for (std::size_t index = 0; index < kCompletedEndings.size(); ++index) {
        auto summary = sampleSummary();
        summary.ending = kCompletedEndings[index];
        const auto frames = tribe::EndingPresentation::framesFor(summary.ending);

        tribe::EndingPresentationOptions options;
        options.animated = true;
        options.ansiEnabled = true;
        options.clearBetweenFrames = true;
        options.frameDelay = std::chrono::milliseconds{0};

        std::ostringstream clearedOutput;
        tribe::EndingPresentation::play(summary, clearedOutput, options);
        REQUIRE(clearedOutput.str().find(colors[index]) != std::string::npos);
        REQUIRE(clearedOutput.str().find("\x1b[0m") != std::string::npos);
        REQUIRE(occurrenceCount(clearedOutput.str(), "\x1b[2J\x1b[H") == frames.size() - 1U);

        options.clearBetweenFrames = false;
        std::ostringstream scrollingOutput;
        tribe::EndingPresentation::play(summary, scrollingOutput, options);
        REQUIRE(scrollingOutput.str().find(colors[index]) != std::string::npos);
        REQUIRE(scrollingOutput.str().find("\x1b[2J\x1b[H") == std::string::npos);
    }
}

TEST_CASE("ending presentation summary contains title epilogue statistics roads and chronicle") {
    const std::string rendered = tribe::EndingPresentation::formatSummary(sampleSummary());
    REQUIRE(rendered.find("结局：联盟共主") != std::string::npos);
    REQUIRE(rendered.find("六族围坐在同一簇长火旁") != std::string::npos);
    REQUIRE(rendered.find("人口：24") != std::string::npos);
    REQUIRE(rendered.find("盟友：3") != std::string::npos);
    REQUIRE(rendered.find("山河征服者：尚缺岩牙要塞") != std::string::npos);
    REQUIRE(rendered.find("燧火繁荣：尚缺一项技术") != std::string::npos);
    REQUIRE(rendered.find("[季节 2 | 重要度 3] 河鹿来访") != std::string::npos);
    REQUIRE(rendered.find("双方交换了第一批种子。") != std::string::npos);
}

TEST_CASE("ending presentation chronicle format is stable and preserves every field") {
    const std::vector<tribe::ChronicleEntry> entries{
        {3, 4, "春祭", "燧火重新点燃。"},
        {16, 5, "终局", "部落选定新的道路。"},
    };
    const std::string expected =
        "[季节 3 | 重要度 4] 春祭\n"
        "  燧火重新点燃。\n"
        "[季节 16 | 重要度 5] 终局\n"
        "  部落选定新的道路。";
    REQUIRE(tribe::EndingPresentation::formatChronicle(entries) == expected);
    REQUIRE(tribe::EndingPresentation::formatChronicle({}) == "（暂无重要记录）");
}
