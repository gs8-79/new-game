#include "tribe/application.hpp"

#include "tribe/console_ui.hpp"
#include "tribe/ending_presentation.hpp"
#include "tribe/game_engine.hpp"
#include "tribe/save_repository.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// 为端到端脚本创建唯一临时存档根目录；析构仅删除该测试创建的目录，避免触碰玩家 saves。
class TemporarySaveDirectory {
   public:
    explicit TemporarySaveDirectory(const std::string& label) {
        static unsigned int sequence = 0U;
        root_ = std::filesystem::temp_directory_path() /
                ("tribe-formal-application-" + label + "-" + std::to_string(++sequence));
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
        if (error) throw std::runtime_error("could not create test save directory: " + error.message());
    }

    ~TemporarySaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& root() const { return root_; }

   private:
    std::filesystem::path root_;
};

bool validUtf8(const std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first < 0x80U) {
            ++index;
            continue;
        }
        std::size_t continuation = 0U;
        if ((first & 0xE0U) == 0xC0U)
            continuation = 1U;
        else if ((first & 0xF0U) == 0xE0U)
            continuation = 2U;
        else if ((first & 0xF8U) == 0xF0U)
            continuation = 3U;
        else
            return false;
        if (index + continuation >= text.size()) return false;
        for (std::size_t offset = 1U; offset <= continuation; ++offset)
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0U) != 0x80U) return false;
        index += continuation + 1U;
    }
    return true;
}

std::size_t displayWidth(const std::string_view text) {
    std::size_t width = 0U;
    for (std::size_t index = 0U; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        std::size_t bytes = 1U;
        unsigned int code = first;
        if ((first & 0xE0U) == 0xC0U) {
            bytes = 2U;
            code = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U) {
            bytes = 3U;
            code = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U) {
            bytes = 4U;
            code = first & 0x07U;
        }
        for (std::size_t offset = 1U; offset < bytes && index + offset < text.size(); ++offset)
            code = (code << 6U) | (static_cast<unsigned char>(text[index + offset]) & 0x3FU);
        const bool zeroWidth = (code >= 0x300U && code <= 0x36FU) || (code >= 0xFE00U && code <= 0xFE0FU);
        const bool wide = (code >= 0x1100U && code <= 0x115FU) || (code >= 0x2E80U && code <= 0xA4CFU) ||
                          (code >= 0xAC00U && code <= 0xD7A3U) || (code >= 0xF900U && code <= 0xFAFFU) ||
                          (code >= 0xFE10U && code <= 0xFE6FU) || (code >= 0xFF01U && code <= 0xFF60U) ||
                          (code >= 0xFFE0U && code <= 0xFFE6U);
        width += zeroWidth ? 0U : wide ? 2U : 1U;
        index += bytes;
    }
    return width;
}

void requireWithinWidth(const std::string_view page, const std::string& output, const std::size_t width) {
    if (!validUtf8(output)) throw std::runtime_error(std::string{page} + " contains invalid UTF-8");
    std::istringstream lines{output};
    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(lines, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (displayWidth(line) > width)
            throw std::runtime_error(std::string{page} + " line " + std::to_string(lineNumber) + " exceeds width");
    }
}

tribe::EndingSummary layoutSampleEnding() {
    tribe::EndingSummary summary;
    summary.ending = tribe::GameEnding::Alliance;
    summary.title = "联盟共主";
    summary.epilogue =
        "多年以后，燧火、河鹿与白羽的孩子们在同一片草地上追逐。曾经分隔部落的界石，被改造成传递消息的路标。";
    summary.statistics = {"治理季节：16", "部落人口：31", "同盟部落：3", "已完成贸易：8"};
    summary.otherRoads = {"征服之路：军队足够强大，但部落选择了谈判。"};
    summary.importantChronicle = {{3, 1, "共同语言", "河鹿使者第一次用燧火语讲述远方的故事。"}};
    return summary;
}

} // namespace

TEST_CASE("application supports a scripted new-game map-save-load-return workflow in an isolated directory") {
    TemporarySaveDirectory directory{"main-flow"};
    std::istringstream input{
        "  seed QUICK 301  \n"
        "\n"
        " assign 木材 2\n"
        " mission 木材\n"
        " move 苍林\n"
        " gather 木材\n"
        " move 营地\n"
        "settle\n"
        "save 1\n"
        "back\n"
        "2\n"
        "1\n"
        "back\n"
        "4\n"};
    std::ostringstream output;

    REQUIRE(tribe::runApplication(input, output, directory.root(), false, false, 80U) == 0);
    const std::string transcript = output.str();
    REQUIRE(transcript.find("小队在燧火营地结算") != std::string::npos);
    REQUIRE(transcript.find("已保存到手动存档1") != std::string::npos);
    REQUIRE(transcript.find("已读取手动存档1") != std::string::npos);
    REQUIRE(transcript.find("燧火未熄，感谢游玩") != std::string::npos);

    tribe::SaveRepository saves{directory.root()};
    tribe::GameState loaded;
    std::string error;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(loaded.phase == tribe::GamePhase::Managing);
    REQUIRE(loaded.wood > 12);
}

TEST_CASE("application asks before overwriting a manual save and honours cancellation") {
    TemporarySaveDirectory directory{"overwrite"};
    std::istringstream input{"seed quick 302\nsave 1\nsave 1\nn\nback\n4\n"};
    std::ostringstream output;

    REQUIRE(tribe::runApplication(input, output, directory.root(), false, false, 80U) == 0);
    REQUIRE(output.str().find("已取消覆盖存档") != std::string::npos);
    tribe::SaveRepository saves{directory.root()};
    REQUIRE(saves.inspect()[1].status == tribe::SaveStatus::Ready);
}

TEST_CASE("console rendering preserves UTF-8 output at the classroom 80-column baseline and a narrow window") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 303U}};
    std::ostringstream wideOutput;
    tribe::ConsoleUI wide{wideOutput, false, false, 80U};
    wide.renderGame(game, "界面验证");
    REQUIRE(validUtf8(wideOutput.str()));
    REQUIRE(wideOutput.str().find("首季目标") != std::string::npos);
    REQUIRE(wideOutput.str().find('\x1b') == std::string::npos);

    std::ostringstream narrowOutput;
    tribe::ConsoleUI narrow{narrowOutput, false, false, 24U};
    narrow.renderGame(game, "窄窗口验证");
    REQUIRE(validUtf8(narrowOutput.str()));
    REQUIRE(narrowOutput.str().find("首季目标") != std::string::npos);
}

TEST_CASE("mission road overview keeps coloring optional and text safe at classroom widths") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 304U}};
    REQUIRE(game.execute("assign wood 2").success);
    REQUIRE(game.execute("mission wood").success);

    std::ostringstream plainOutput;
    tribe::ConsoleUI plain{plainOutput, false, false, 80U};
    plain.renderGame(game, "地图界面验证");
    REQUIRE(validUtf8(plainOutput.str()));
    REQUIRE(plainOutput.str().find("道路总览") != std::string::npos);
    REQUIRE(plainOutput.str().find("[1 营地]") != std::string::npos);
    REQUIRE(plainOutput.str().find('\x1b') == std::string::npos);

    std::ostringstream colouredOutput;
    tribe::ConsoleUI coloured{colouredOutput, false, true, 80U};
    coloured.renderGame(game, "地图界面验证");
    REQUIRE(colouredOutput.str().find('\x1b') != std::string::npos);

    std::ostringstream narrowOutput;
    tribe::ConsoleUI narrow{narrowOutput, false, false, 24U};
    narrow.renderGame(game, "窄窗口地图验证");
    REQUIRE(validUtf8(narrowOutput.str()));
    REQUIRE(narrowOutput.str().find("道路总览") != std::string::npos);
}

TEST_CASE("mission look exposes explored resources and corrected road neighbours") {
    tribe::GameEngine game{{tribe::GameMode::Quick, 305U}};
    REQUIRE(game.execute("assign food 2").success);
    REQUIRE(game.execute("mission food").success);
    REQUIRE(game.execute("move forest").success);
    const tribe::ActionResult look = game.execute("look");
    REQUIRE(look.success);

    std::ostringstream output;
    tribe::ConsoleUI ui{output, false, false, 80U};
    ui.renderGame(game, look.message);
    const std::string transcript = output.str();
    REQUIRE(transcript.find("已探索资源：食物 2苍林") != std::string::npos);
    REQUIRE(transcript.find("[11 潮盐]") != std::string::npos);
    REQUIRE(transcript.find("[15 山前]") != std::string::npos);
    requireWithinWidth("mission look", transcript, 80U);
}

TEST_CASE("fixed-width pages keep UTF-8 layout within requested columns") {
    for (const std::size_t width : {40U, 80U}) {
        std::ostringstream menuOutput;
        tribe::ConsoleUI menu{menuOutput, false, false, width};
        menu.renderMainMenu();
        requireWithinWidth("main menu", menuOutput.str(), width);

        std::ostringstream helpOutput;
        tribe::ConsoleUI help{helpOutput, false, false, width};
        help.renderHelpPage(2);
        requireWithinWidth("help page", helpOutput.str(), width);

        tribe::GameEngine game{{tribe::GameMode::Quick, 306U}};
        std::ostringstream gameOutput;
        tribe::ConsoleUI gameUi{gameOutput, false, false, width};
        gameUi.renderGame(game, "固定宽度验证");
        requireWithinWidth("game page", gameOutput.str(), width);
    }

    for (const std::size_t width : {40U, 80U}) {
        std::ostringstream output;
        tribe::EndingPresentationOptions options;
        options.animated = false;
        options.ansiEnabled = false;
        options.width = width;
        tribe::EndingPresentation::play(layoutSampleEnding(), output, options);
        requireWithinWidth("ending page", output.str(), width);
    }
}
