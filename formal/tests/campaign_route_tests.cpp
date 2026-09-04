#include "tribe/campaign.hpp"
#include "test_harness.hpp"

#include <stdexcept>
#include <string>

namespace {

std::size_t tribeIndex(const tribe::TribeIdV2 tribe) {
    return static_cast<std::size_t>(tribe);
}

tribe::CampaignActionResult mustExecute(tribe::CampaignGame& game, const std::string& command) {
    const tribe::CampaignActionResult result = game.execute(command);
    if (!result.recognized || !result.success) {
        throw std::runtime_error(command + " failed at season " + std::to_string(game.state().season)
            + ": " + result.message);
    }
    return result;
}

void performAction(tribe::CampaignGame& game, const std::string& command) {
    if (game.state().phase != tribe::CampaignPhase::Managing) {
        throw std::runtime_error("player action requested outside managing phase: " + command);
    }
    if (game.state().actionsLeft == 0) mustExecute(game, "endturn");
    if (game.state().phase != tribe::CampaignPhase::Managing) {
        throw std::runtime_error("route reached ending choice before action: " + command);
    }
    mustExecute(game, command);
}

void ensureFood(tribe::CampaignGame& game, const int minimum) {
    int guard = 0;
    while (game.state().food < minimum && guard++ < 40) performAction(game, "gather food");
    if (game.state().food < minimum) throw std::runtime_error("could not gather enough food");
}

void ensureWood(tribe::CampaignGame& game, const int minimum) {
    int guard = 0;
    while (game.state().wood < minimum && guard++ < 40) performAction(game, "gather wood");
    if (game.state().wood < minimum) throw std::runtime_error("could not gather enough wood");
}

void ensureStone(tribe::CampaignGame& game, const int minimum) {
    int guard = 0;
    while (game.state().stone < minimum && guard++ < 40) performAction(game, "gather stone");
    if (game.state().stone < minimum) throw std::runtime_error("could not gather enough stone");
}

void waitForEndingChoice(tribe::CampaignGame& game, const int foodReserve) {
    int guard = 0;
    while (game.state().phase == tribe::CampaignPhase::Managing && guard++ < 80) {
        if (game.state().food < foodReserve && game.state().actionsLeft > 0) {
            mustExecute(game, "gather food");
        } else {
            mustExecute(game, "endturn");
        }
    }
    if (game.state().phase != tribe::CampaignPhase::EndingChoice) {
        throw std::runtime_error("campaign did not reach ending choice");
    }
}

} // namespace

TEST_CASE("campaign player route reaches the alliance ending from a new game") {
    tribe::CampaignGame game{{tribe::CampaignMode::Long, 17U, "燧火", "炎角", "外交"}};

    performAction(game, "build wall");
    performAction(game, "scout quarry");
    performAction(game, "gather stone");
    ensureWood(game, 8);
    performAction(game, "build workshop");
    ensureFood(game, 18);
    ensureWood(game, 9);
    performAction(game, "research gift");
    performAction(game, "research language");
    performAction(game, "research confederation");

    performAction(game, "scout marsh");
    performAction(game, "scout whitecamp");
    performAction(game, "scout ford");

    const auto establishAlliance = [&](const tribe::TribeIdV2 tribe, const std::string& name) {
        int guard = 0;
        const std::size_t index = tribeIndex(tribe);
        while ((game.state().relations[index].relation < 90
                || game.state().relations[index].trust < 70)
            && guard++ < 24) {
            ensureFood(game, 12);
            performAction(game, "gift " + name);
        }
        performAction(game, "ally " + name);
    };
    establishAlliance(tribe::TribeIdV2::RiverDeer, "river");
    establishAlliance(tribe::TribeIdV2::WhiteFeather, "white");

    int waitGuard = 0;
    while (game.state().phase == tribe::CampaignPhase::Managing && waitGuard++ < 100) {
        const auto& river = game.state().relations[tribeIndex(tribe::TribeIdV2::RiverDeer)];
        const auto& white = game.state().relations[tribeIndex(tribe::TribeIdV2::WhiteFeather)];
        if (game.state().actionsLeft > 0 && game.state().food >= 8 && river.relation < 85) {
            mustExecute(game, "gift river");
        } else if (game.state().actionsLeft > 0 && game.state().food >= 8 && white.relation < 85) {
            mustExecute(game, "gift white");
        } else if (game.state().actionsLeft > 0 && game.state().food < 50) {
            mustExecute(game, "gather food");
        } else {
            mustExecute(game, "endturn");
        }
    }

    REQUIRE(game.state().phase == tribe::CampaignPhase::EndingChoice);
    REQUIRE(game.state().relations[tribeIndex(tribe::TribeIdV2::RiverDeer)].alliance);
    REQUIRE(game.state().relations[tribeIndex(tribe::TribeIdV2::WhiteFeather)].alliance);
    mustExecute(game, "choose alliance");
    REQUIRE(game.state().phase == tribe::CampaignPhase::Finished);
    REQUIRE(game.state().ending == tribe::CampaignEnding::Alliance);
}

TEST_CASE("campaign player route reaches the conquest ending from a new game") {
    tribe::CampaignGame game{{tribe::CampaignMode::Long, 29U, "燧火", "炎角", "战争"}};

    performAction(game, "build wall");
    performAction(game, "scout quarry");
    performAction(game, "scout valley");
    performAction(game, "scout workshop");
    performAction(game, "scout road");
    performAction(game, "scout pass");
    performAction(game, "scout fort");
    ensureFood(game, 6);
    ensureWood(game, 2);
    performAction(game, "research spear");
    performAction(game, "squadtask train");

    int trainingGuard = 0;
    while (game.state().warriors < 8 && trainingGuard++ < 16) {
        if (game.state().food < 20 && game.state().actionsLeft > 0) mustExecute(game, "gather food");
        else mustExecute(game, "endturn");
    }
    REQUIRE(game.state().warriors >= 8);

    performAction(game, "declare rockfang");
    performAction(game, "formarmy 8 6");
    mustExecute(game, "war rockfang");
    mustExecute(game, "order advance");
    int battleGuard = 0;
    while (game.state().phase == tribe::CampaignPhase::War && battleGuard++ < 24) {
        mustExecute(game, "attack");
    }
    REQUIRE(game.state().phase == tribe::CampaignPhase::Managing);
    REQUIRE(game.state().rockfangFortCaptured);
    REQUIRE(game.state().warriors >= 5);
    REQUIRE(game.state().morale >= 55);

    performAction(game, "squadtask gather");
    waitForEndingChoice(game, 50);
    mustExecute(game, "choose conquest");
    REQUIRE(game.state().phase == tribe::CampaignPhase::Finished);
    REQUIRE(game.state().ending == tribe::CampaignEnding::Conquest);
}

TEST_CASE("campaign player route reaches the prosperity ending from a new game") {
    tribe::CampaignGame game{{tribe::CampaignMode::Long, 4U, "燧火", "炎角", "生存"}};

    performAction(game, "scout quarry");
    performAction(game, "gather stone");
    performAction(game, "build healer");
    ensureWood(game, 10);
    ensureStone(game, 2);
    performAction(game, "build wall");
    ensureWood(game, 8);
    ensureStone(game, 2);
    performAction(game, "build granary");
    ensureWood(game, 6);
    ensureStone(game, 4);
    performAction(game, "build council");

    ensureFood(game, 18);
    ensureWood(game, 9);
    performAction(game, "research preservation");
    performAction(game, "research herbal");
    performAction(game, "research spear");
    performAction(game, "research gift");

    waitForEndingChoice(game, 60);
    REQUIRE(game.state().population >= 20);
    REQUIRE(game.state().food >= 40);
    mustExecute(game, "choose prosperity");
    REQUIRE(game.state().phase == tribe::CampaignPhase::Finished);
    REQUIRE(game.state().ending == tribe::CampaignEnding::Prosperity);
}

TEST_CASE("campaign player route reaches extinction from a new game") {
    tribe::CampaignGame game{{tribe::CampaignMode::Long, 1U, "燧火", "炎角", "放弃维护"}};
    performAction(game, "squadtask none");

    int guard = 0;
    while (game.state().phase != tribe::CampaignPhase::Finished && guard++ < 40) {
        mustExecute(game, "endturn");
    }
    REQUIRE(game.state().phase == tribe::CampaignPhase::Finished);
    REQUIRE(game.state().ending == tribe::CampaignEnding::Extinction);
    REQUIRE(game.state().population == 0 || game.state().campDurability == 0);
}
