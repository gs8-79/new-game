#include "tribe/expansion_game.hpp"
#include "test_harness.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

tribe::ExpansionCommandResult requireSuccess(tribe::ExpansionGame& game, const std::string& command) {
    const auto result = game.execute(command);
    if (!result.success) throw std::runtime_error(command + " failed: " + result.message);
    return result;
}

void reachEncounter(tribe::ExpansionGame& game) {
    requireSuccess(game, "move forest");
    requireSuccess(game, "move deep");
    requireSuccess(game, "move clearing");
    REQUIRE(game.state().phase == tribe::ExpansionPhase::ForeignEncounter);
    REQUIRE(game.state().foreignStance == tribe::ForeignStance::Neutral);
}

void winThreeFrontlines(tribe::ExpansionGame& game) {
    std::array<bool, 4> seen{{false, false, false, false}};
    int actions = 0;
    while (game.state().phase == tribe::ExpansionPhase::FrontlineCombat && actions < 24) {
        seen[static_cast<std::size_t>(game.state().frontline)] = true;
        requireSuccess(game, "attack");
        ++actions;
    }
    REQUIRE(actions < 24);
    REQUIRE(seen[1]);
    REQUIRE(seen[2]);
    REQUIRE(seen[3]);
    REQUIRE(game.state().battleWon);
    REQUIRE(game.state().phase == tribe::ExpansionPhase::ForeignEncounter);
}

} // namespace

TEST_CASE("expansion game creates deterministic named squads from two to eight members") {
    tribe::ExpansionGame defaultGame{41U};
    REQUIRE(defaultGame.state().squad.members.size() == 4U);
    REQUIRE(defaultGame.state().squad.members[0].name == "青枝");
    REQUIRE(defaultGame.state().squad.members[1].name == "石刃");
    REQUIRE(defaultGame.state().phase == tribe::ExpansionPhase::CampPreparation);
    REQUIRE(tribe::ExpansionGame::validateState(defaultGame.state()));

    tribe::ExpansionGame smallest{41U, 2U};
    tribe::ExpansionGame largest{41U, 8U};
    REQUIRE(smallest.state().squad.members.size() == 2U);
    REQUIRE(largest.state().squad.members.size() == 8U);
    REQUIRE(tribe::validateSquad(smallest.state().squad));
    REQUIRE(tribe::validateSquad(largest.state().squad));
}

TEST_CASE("expansion English and Chinese peaceful trade routes are equivalent") {
    tribe::ExpansionGame english{73U};
    tribe::ExpansionGame chinese{73U};
    const std::vector<std::pair<std::string, std::string>> commands{
        {"look", "查看"},
        {"move forest", "移动 苍林"},
        {"move deep", "移动 深林"},
        {"gather herbs", "采集 草药"},
        {"move clearing", "移动 空地"},
        {"talk", "交谈"},
        {"trade", "贸易"},
        {"return", "回营"},
    };
    for (const auto& [englishCommand, chineseCommand] : commands) {
        const auto englishResult = requireSuccess(english, englishCommand);
        const auto chineseResult = requireSuccess(chinese, chineseCommand);
        REQUIRE(englishResult.turnAdvanced == chineseResult.turnAdvanced);
        REQUIRE(english.stateFingerprint() == chinese.stateFingerprint());
    }
    REQUIRE(english.state().traded);
    REQUIRE(english.state().tradeGoods == 1);
    REQUIRE(english.state().phase == tribe::ExpansionPhase::ReturnSettlement);
    REQUIRE(english.state().settled);
}

TEST_CASE("expansion leader acts once and all squad members automatically follow each order") {
    tribe::ExpansionGame game{89U};
    requireSuccess(game, "移动 苍林");
    requireSuccess(game, "移动 深林");
    requireSuccess(game, "order advance");
    requireSuccess(game, "move hunting");
    const int suppliesBefore = game.state().supplies;
    requireSuccess(game, "采集 食物");

    REQUIRE(game.state().supplies > suppliesBefore);
    REQUIRE(game.state().order == tribe::SquadOrder::Advance);
    REQUIRE(game.state().leaderActions == game.state().turn);
    REQUIRE(game.state().followerActions
        == game.state().turn * static_cast<int>(game.state().squad.members.size() - 1U));
    REQUIRE(game.state().squad.members[game.state().squad.leaderIndex].fatigue > 0);
    REQUIRE(game.state().squad.members[1].fatigue > 0);
    REQUIRE(tribe::ExpansionGame::validateState(game.state()));
}

TEST_CASE("expansion combat commands are bilingual and retreat remains a valid route") {
    tribe::ExpansionGame english{97U};
    tribe::ExpansionGame chinese{97U};
    reachEncounter(english);
    reachEncounter(chinese);
    const std::vector<std::pair<std::string, std::string>> commands{
        {"raid", "劫掠"},
        {"order focus", "下令 集火"},
        {"defend", "防御"},
        {"attack", "攻击"},
        {"use ration", "使用 口粮"},
        {"retreat", "撤退"},
        {"return", "回营"},
    };
    for (const auto& [englishCommand, chineseCommand] : commands) {
        requireSuccess(english, englishCommand);
        requireSuccess(chinese, chineseCommand);
        REQUIRE(english.stateFingerprint() == chinese.stateFingerprint());
    }
    REQUIRE(english.state().retreated);
    REQUIRE(!english.state().battleWon);
    REQUIRE(english.state().phase == tribe::ExpansionPhase::ReturnSettlement);
}

TEST_CASE("expansion victory crosses three frontlines then allows free loot and return settlement") {
    constexpr std::uint32_t seed = 113U;
    tribe::ExpansionGame game{seed};
    reachEncounter(game);
    requireSuccess(game, "raid");
    REQUIRE(game.state().frontline == 1);
    winThreeFrontlines(game);

    const int turnBeforeLoot = game.state().turn;
    const int leaderFatigueBeforeLoot = game.state().squad.members[game.state().squad.leaderIndex].fatigue;
    const std::size_t itemsBeforeLoot = game.state().inventory.items().size();
    const auto loot = requireSuccess(game, "搜取");
    REQUIRE(!loot.turnAdvanced);
    REQUIRE(game.state().turn == turnBeforeLoot);
    REQUIRE(game.state().squad.members[game.state().squad.leaderIndex].fatigue == leaderFatigueBeforeLoot);
    REQUIRE(game.state().inventory.items().size() == itemsBeforeLoot + 1U);

    const std::string trophyId = "forest_trophy_" + std::to_string(seed % 997U);
    requireSuccess(game, "equip accessory " + trophyId);
    const auto& accessory = game.state().squad.members[game.state().squad.leaderIndex]
        .equipment[static_cast<std::size_t>(tribe::EquipmentSlot::Accessory)];
    REQUIRE(accessory);
    REQUIRE(accessory->id == trophyId);
    REQUIRE(game.state().inventory.items().size() == itemsBeforeLoot);
    const std::string afterEquip = game.stateFingerprint();
    REQUIRE(!game.execute("equip accessory " + trophyId).success);
    REQUIRE(game.stateFingerprint() == afterEquip);

    requireSuccess(game, "return");
    REQUIRE(game.state().phase == tribe::ExpansionPhase::ReturnSettlement);
    REQUIRE(game.state().battleWon);
    REQUIRE(game.state().settled);
    REQUIRE(game.state().squad.members.front().level >= 2);
    REQUIRE(tribe::ExpansionGame::validateState(game.state()));
}

TEST_CASE("expansion failed commands and combat equipment changes are atomic") {
    tribe::ExpansionGame game{127U};
    const std::vector<std::string> invalidAtCamp{
        "move clearing", "gather food", "trade", "attack", "loot", "retreat", "use medicine"};
    for (const std::string& command : invalidAtCamp) {
        const std::string before = game.stateFingerprint();
        const auto result = game.execute(command);
        REQUIRE(result.recognized);
        REQUIRE(!result.success);
        REQUIRE(!result.stateChanged);
        REQUIRE(game.stateFingerprint() == before);
    }

    reachEncounter(game);
    requireSuccess(game, "raid");
    const std::string beforeEquip = game.stateFingerprint();
    const auto equip = game.execute("装备 主手 spare_knife");
    REQUIRE(equip.recognized);
    REQUIRE(!equip.success);
    REQUIRE(equip.message.find("战斗中禁止") != std::string::npos);
    REQUIRE(game.stateFingerprint() == beforeEquip);

    const std::string beforeTrade = game.stateFingerprint();
    REQUIRE(!game.execute("trade").success);
    REQUIRE(game.stateFingerprint() == beforeTrade);

    tribe::ExpansionGame fatal{1U, 2U};
    reachEncounter(fatal);
    requireSuccess(fatal, "raid");
    int orders = 0;
    while (fatal.state().phase == tribe::ExpansionPhase::FrontlineCombat && orders < 80) {
        requireSuccess(fatal, orders % 2 == 0 ? "order advance" : "order focus");
        ++orders;
    }
    REQUIRE(orders < 80);
    REQUIRE(fatal.state().missionFailed);
    REQUIRE(fatal.state().phase == tribe::ExpansionPhase::ReturnSettlement);
    REQUIRE(fatal.state().squad.members[fatal.state().squad.leaderIndex].life == 0);
    const std::string afterDefeat = fatal.stateFingerprint();
    REQUIRE(!fatal.execute("attack").success);
    REQUIRE(fatal.stateFingerprint() == afterDefeat);
}

TEST_CASE("expansion fixed seeds reproduce every result and fatigue lowers initiative") {
    tribe::ExpansionGame first{211U};
    tribe::ExpansionGame repeat{211U};
    const std::vector<std::string> commands{
        "move forest", "move deep", "gather herbs", "move clearing", "raid",
        "order hold", "attack", "defend", "retreat", "return"};
    for (const std::string& command : commands) {
        const auto firstResult = requireSuccess(first, command);
        const auto repeatResult = requireSuccess(repeat, command);
        REQUIRE(firstResult.message == repeatResult.message);
        REQUIRE(first.stateFingerprint() == repeat.stateFingerprint());
    }

    tribe::ExpansionGame fresh{223U};
    tribe::ExpansionGame tired{223U};
    for (int index = 0; index < 20; ++index) {
        requireSuccess(tired, index % 2 == 0 ? "order advance" : "order follow");
    }
    reachEncounter(fresh);
    reachEncounter(tired);
    requireSuccess(fresh, "raid");
    requireSuccess(tired, "raid");
    requireSuccess(fresh, "attack");
    requireSuccess(tired, "attack");
    REQUIRE(tired.state().squad.members.front().fatigue > fresh.state().squad.members.front().fatigue);
    REQUIRE(tired.state().lastPlayerInitiative < fresh.state().lastPlayerInitiative);
    REQUIRE(fresh.state().lastEnemyInitiative == tired.state().lastEnemyInitiative);
}
