#include "tribe/application.hpp"
#include "tribe/console_ui.hpp"
#include "tribe/save_repository.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

class TempDirectory {
  public:
    TempDirectory() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("tribe-interface-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

const tribe::SaveSummary &findSummary(const std::vector<tribe::SaveSummary> &summaries,
                                      const tribe::SaveSlot slot) {
    for (const auto &summary : summaries) {
        if (summary.slot == slot)
            return summary;
    }
    throw std::runtime_error("save summary not found");
}

} // namespace

TEST_CASE("cover centers Chinese titles and long text wraps without splitting UTF8") {
    for (const std::size_t width : {40U, 80U, 120U}) {
        std::ostringstream output;
        tribe::ConsoleUI ui(output, false, false, width);
        ui.renderMainMenu();
        REQUIRE(output.str().find("\n" + std::string((width - 20U) / 2U, ' ') + "《燧火纪：部落黎明》\n") !=
                std::string::npos);
    }
    std::ostringstream output;
    tribe::ConsoleUI narrow(output, false, false, 8U);
    narrow.showStandalone("标题", "中文abc中文abc");
    REQUIRE(output.str().find("中文abc\n中文abc") != std::string::npos);
}

TEST_CASE("all three mode choices and fixed seeds reach the promised game state") {
    const tribe::GameMode modes[] = {tribe::GameMode::Quick, tribe::GameMode::Standard,
                                     tribe::GameMode::Long};
    for (int option = 1; option <= 3; ++option) {
        TempDirectory root;
        std::istringstream input("1\n" + std::to_string(option) + "\nquit\n");
        std::ostringstream output;
        REQUIRE(tribe::runApplication(input, output, root.path(), false, false, 80U) == 0);
        tribe::SaveRepository saves(root.path());
        tribe::GameState loaded;
        std::string error;
        REQUIRE(saves.load(tribe::SaveSlot::Autosave, loaded, error));
        REQUIRE(loaded.mode == modes[option - 1]);
        REQUIRE(loaded.season == (option == 1 ? 9 : 1));
        REQUIRE(loaded.seasonLimit == (option == 3 ? 32 : 16));
    }
    TempDirectory root;
    std::istringstream input("seed standard 123\nquit\n");
    std::ostringstream output;
    tribe::runApplication(input, output, root.path(), false, false, 80U);
    tribe::GameState loaded;
    std::string error;
    REQUIRE(tribe::SaveRepository(root.path()).load(tribe::SaveSlot::Autosave, loaded, error));
    REQUIRE(loaded.seed == 123U);
}

TEST_CASE("menu errors returns and EOF do not create a game or loop forever") {
    TempDirectory root;
    std::istringstream input("invalid\n1\nb argument\n0\nb\n2\n7\nb\n3\n2\n\n\n4\n");
    std::ostringstream output;
    tribe::runApplication(input, output, root.path(), false, false, 40U);
    REQUIRE(output.str().find("无效选择") != std::string::npos);
    REQUIRE(output.str().find("无效模式") != std::string::npos);
    REQUIRE(output.str().find("无效档位") != std::string::npos);
    REQUIRE(!std::filesystem::exists(root.path() / "autosave.sav"));
    for (const std::string script : {"1\n", "2\n", "3\n"}) {
        std::istringstream eof(script);
        std::ostringstream sink;
        REQUIRE(tribe::runApplication(eof, sink, root.path(), false, false) == 0);
    }
}

TEST_CASE("seven save summaries preserve bytes times and recovery candidates") {
    TempDirectory root;
    tribe::SaveRepository saves(root.path());
    tribe::GameEngine game({tribe::GameMode::Long, 531U});
    std::string error;
    for (int index = 0; index < 7; ++index) {
        REQUIRE(saves.save(game.state(), static_cast<tribe::SaveSlot>(index), error));
    }
    const auto primary = saves.pathFor(tribe::SaveSlot::Slot3);
    auto temporary = primary;
    temporary += ".tmp";
    std::filesystem::rename(primary, temporary);
    const auto beforeTime = std::filesystem::last_write_time(temporary);
    std::ifstream beforeFile(temporary, std::ios::binary);
    const std::string before{std::istreambuf_iterator<char>(beforeFile), {}};
    beforeFile.close();
    const auto summaries = saves.inspect();
    REQUIRE(summaries.size() == 7U);
    for (const auto &summary : summaries) {
        REQUIRE(summary.tribeName == game.state().tribeName);
        REQUIRE(summary.leaderName == game.state().leaderName);
        REQUIRE(summary.food == game.state().food);
        REQUIRE(summary.wood == game.state().wood);
        REQUIRE(summary.stone == game.state().stone);
        REQUIRE(summary.herbs == game.state().herbs);
        REQUIRE(summary.modifiedAt.size() == 16U);
    }
    REQUIRE(findSummary(summaries, tribe::SaveSlot::Slot3).status == tribe::SaveStatus::Recoverable);
    REQUIRE(!std::filesystem::exists(primary));
    REQUIRE(std::filesystem::last_write_time(temporary) == beforeTime);
    std::ifstream afterFile(temporary, std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(afterFile), {}) == before);
    afterFile.close();
    // 主档是目录时不可访问，和 load 一样不得选用较旧的恢复候选。
    std::filesystem::create_directory(primary);
    REQUIRE(findSummary(saves.inspect(), tribe::SaveSlot::Slot3).status == tribe::SaveStatus::Corrupt);
    tribe::GameState destination = game.state();
    REQUIRE(!saves.load(tribe::SaveSlot::Slot3, destination, error));
    REQUIRE(destination.seed == game.state().seed);
}

TEST_CASE("manual overwrite cancellation and save failure retain committed progress") {
    TempDirectory root;
    tribe::SaveRepository saves(root.path());
    tribe::GameEngine previous({tribe::GameMode::Long, 222U});
    std::string error;
    REQUIRE(saves.save(previous.state(), tribe::SaveSlot::Slot1, error));
    std::istringstream input("seed standard 333\nsave 1\nn\nback\n2\na\nquit\n");
    std::ostringstream output;
    tribe::runApplication(input, output, root.path(), false, false);
    tribe::GameState loaded;
    REQUIRE(saves.load(tribe::SaveSlot::Slot1, loaded, error));
    REQUIRE(loaded.seed == 222U);
    REQUIRE(saves.load(tribe::SaveSlot::Autosave, loaded, error));
    REQUIRE(loaded.seed == 333U);

    auto blocked = root.path() / "blocked";
    {
        std::ofstream file(blocked);
        file << "not a directory";
    }
    std::istringstream failing("seed standard 444\nback\nquit\nforcequit\n");
    std::ostringstream failureOutput;
    tribe::runApplication(failing, failureOutput, blocked, false, false);
    REQUIRE(failureOutput.str().find("仍留在游戏中") != std::string::npos);
    REQUIRE(failureOutput.str().find("退出前保存失败") != std::string::npos);
}

TEST_CASE("cover mode help and game pages use the unified plain layout") {
    std::ostringstream output;
    tribe::ConsoleUI ui(output, false, false, 80U);
    ui.renderMainMenu();
    const std::string cover = output.str();
    REQUIRE(cover.find("《燧火纪：部落黎明》") < cover.find("[ 1 ]  开始游戏"));
    REQUIRE(cover.find("读取存档") != std::string::npos);
    REQUIRE(cover.find("V1") == std::string::npos);
    REQUIRE(cover.find("V2") == std::string::npos);
    REQUIRE(cover.find("\x1b[") == std::string::npos);

    output.str({});
    output.clear();
    ui.renderModeMenu();
    REQUIRE(output.str().find("快速游戏") != std::string::npos);
    REQUIRE(output.str().find("正式游戏") != std::string::npos);
    REQUIRE(output.str().find("长期游戏") != std::string::npos);

    output.str({});
    output.clear();
    ui.renderHelpPage();
    REQUIRE(output.str().find("首次游玩") != std::string::npos);
    REQUIRE(output.str().find("探索小队") != std::string::npos);
    REQUIRE(output.str().find("存档退出") != std::string::npos);

    output.str({});
    output.clear();
    tribe::GameEngine game({tribe::GameMode::Standard, 37U});
    ui.renderGame(game, "测试消息");
    REQUIRE(output.str().find("[ 部落 ]") != std::string::npos);
    REQUIRE(output.str().find("[ 资源 ]") != std::string::npos);
    REQUIRE(output.str().find("[ 当前局面 ]") != std::string::npos);
    REQUIRE(output.str().find("测试消息") != std::string::npos);
}

TEST_CASE("ANSI layout colors the ember theme only when enabled") {
    std::ostringstream output;
    tribe::ConsoleUI ui(output, true, true, 80U);
    ui.renderMainMenu();
    REQUIRE(output.str().find("\x1b[") != std::string::npos);
    REQUIRE(output.str().find("38;5;208m") != std::string::npos);
}

TEST_CASE("save inspection reports all slots without repairing recoverable files") {
    TempDirectory root;
    tribe::SaveRepository saves(root.path());
    REQUIRE(saves.inspect().size() == 7U);
    REQUIRE(findSummary(saves.inspect(), tribe::SaveSlot::Slot1).status == tribe::SaveStatus::Empty);

    std::string error;
    tribe::GameEngine first({tribe::GameMode::Standard, 41U});
    REQUIRE(saves.save(first.state(), tribe::SaveSlot::Slot1, error));
    const auto ready = findSummary(saves.inspect(), tribe::SaveSlot::Slot1);
    REQUIRE(ready.status == tribe::SaveStatus::Ready);
    REQUIRE(ready.season == 1);
    REQUIRE(!ready.modifiedAt.empty());

    tribe::GameEngine second({tribe::GameMode::Long, 42U});
    REQUIRE(saves.save(second.state(), tribe::SaveSlot::Slot1, error));
    const auto primary = saves.pathFor(tribe::SaveSlot::Slot1);
    {
        std::ofstream damaged(primary, std::ios::binary | std::ios::trunc);
        damaged << "damaged";
    }
    const auto sizeBefore = std::filesystem::file_size(primary);
    const auto recoverable = findSummary(saves.inspect(), tribe::SaveSlot::Slot1);
    REQUIRE(recoverable.status == tribe::SaveStatus::Recoverable);
    REQUIRE(std::filesystem::file_size(primary) == sizeBefore);

    const auto corruptPath = saves.pathFor(tribe::SaveSlot::Slot2);
    {
        std::ofstream damaged(corruptPath, std::ios::binary | std::ios::trunc);
        damaged << "damaged";
    }
    REQUIRE(findSummary(saves.inspect(), tribe::SaveSlot::Slot2).status == tribe::SaveStatus::Corrupt);
}

TEST_CASE("application navigates start help load and autosave flows with injected streams") {
    TempDirectory root;
    std::istringstream firstRun("3\n\n1\n2\nback\n4\n");
    std::ostringstream firstOutput;
    REQUIRE(tribe::runApplication(firstRun, firstOutput, root.path(), false, false, 80U) == 0);
    REQUIRE(firstOutput.str().find("游戏帮助") != std::string::npos);
    REQUIRE(firstOutput.str().find("选择旅程") != std::string::npos);
    REQUIRE(std::filesystem::exists(root.path() / "autosave.sav"));

    tribe::SaveRepository saves(root.path());
    std::string error;
    tribe::GameEngine saved({tribe::GameMode::Quick, 99U});
    REQUIRE(saves.save(saved.state(), tribe::SaveSlot::Slot1, error));
    std::istringstream secondRun("2\n1\nback\n4\n");
    std::ostringstream secondOutput;
    REQUIRE(tribe::runApplication(secondRun, secondOutput, root.path(), false, false, 80U) == 0);
    REQUIRE(secondOutput.str().find("读取存档") != std::string::npos);
    REQUIRE(secondOutput.str().find("手动存档1") != std::string::npos);
}
