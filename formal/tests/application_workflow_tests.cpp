#include "tribe/application.hpp"

#include "tribe/console_ui.hpp"
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

} // namespace

TEST_CASE("application supports a scripted new-game map-save-load-return workflow in an isolated directory") {
    TemporarySaveDirectory directory{"main-flow"};
    std::istringstream input{
        "seed quick 301\n"
        "assign wood 2\n"
        "mission wood\n"
        "move forest\n"
        "gather wood\n"
        "move camp\n"
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
