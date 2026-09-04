#include "tribe/campaign.hpp"
#include "test_harness.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace {

std::size_t tribeIndex(const tribe::TribeIdV2 tribe) {
    return static_cast<std::size_t>(tribe);
}

tribe::CampaignState initialState(const std::uint32_t seed) {
    return tribe::CampaignGame{{tribe::CampaignMode::Course, seed, "燧火", "炎角", "生存"}}.state();
}

tribe::CampaignActionResult endSeason(tribe::CampaignGame& game) {
    const tribe::CampaignActionResult result = game.execute("endturn");
    if (!result.success) throw std::runtime_error("endturn failed: " + result.message);
    return result;
}

void discoverContact(tribe::CampaignState& state, const tribe::WorldLocationId location) {
    state.discovered[static_cast<std::size_t>(location)] = true;
}

bool contains(const std::string& text, const std::string& expected) {
    return text.find(expected) != std::string::npos;
}

} // namespace

TEST_CASE("autonomous tribe decisions are reproducible for a fixed seed") {
    tribe::CampaignGame first{{tribe::CampaignMode::Course, 211U, "燧火", "炎角", "生存"}};
    tribe::CampaignGame repeat{{tribe::CampaignMode::Course, 211U, "燧火", "炎角", "生存"}};

    const auto firstResult = endSeason(first);
    const auto repeatResult = endSeason(repeat);

    REQUIRE(firstResult.message == repeatResult.message);
    REQUIRE(first.diplomacyText() == repeat.diplomacyText());
    REQUIRE(first.state().food == repeat.state().food);
    REQUIRE(first.state().campDurability == repeat.state().campDurability);
}

TEST_CASE("different seeds can produce different autonomous tribe decisions") {
    std::set<std::string> diplomaticOutcomes;
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        tribe::CampaignGame game{{tribe::CampaignMode::Course, seed, "燧火", "炎角", "生存"}};
        endSeason(game);
        diplomaticOutcomes.insert(game.diplomacyText());
    }
    REQUIRE(diplomaticOutcomes.size() >= 2U);
}

TEST_CASE("leader personality and dominant faction demand change autonomous action") {
    constexpr std::uint32_t seedSelectingRiverDeer = 7U;
    const std::size_t riverIndex = tribeIndex(tribe::TribeIdV2::RiverDeer);

    auto aggressiveState = initialState(seedSelectingRiverDeer);
    auto& aggressiveProfile = aggressiveState.tribes[riverIndex];
    aggressiveProfile.personality = "强硬好战";
    aggressiveProfile.factions[0].influence = 80;
    aggressiveProfile.factions[0].demand = "取得战利品";
    aggressiveState.relations[riverIndex].relation = -20;
    aggressiveState.relations[riverIndex].trust = 0;
    discoverContact(aggressiveState, tribe::WorldLocationId::RiverFord);
    const int durabilityBefore = aggressiveState.campDurability;
    tribe::CampaignGame aggressive{aggressiveState};
    const auto aggressiveResult = endSeason(aggressive);

    REQUIRE(contains(aggressiveResult.message, "强硬好战"));
    REQUIRE(contains(aggressiveResult.message, "取得战利品"));
    REQUIRE(contains(aggressiveResult.message, "边境骚扰"));
    REQUIRE(aggressive.state().relations[riverIndex].fear == aggressiveState.relations[riverIndex].fear + 4);
    REQUIRE(aggressive.state().campDurability == durabilityBefore - 2);

    auto conciliatoryState = initialState(seedSelectingRiverDeer);
    auto& conciliatoryProfile = conciliatoryState.tribes[riverIndex];
    conciliatoryProfile.personality = "谨慎救助";
    conciliatoryProfile.factions[0].influence = 80;
    conciliatoryProfile.factions[0].demand = "救助伤者";
    conciliatoryState.relations[riverIndex].relation = -20;
    conciliatoryState.relations[riverIndex].trust = 0;
    discoverContact(conciliatoryState, tribe::WorldLocationId::RiverFord);
    tribe::CampaignGame conciliatory{conciliatoryState};
    const auto conciliatoryResult = endSeason(conciliatory);

    REQUIRE(contains(conciliatoryResult.message, "谨慎救助"));
    REQUIRE(contains(conciliatoryResult.message, "救助伤者"));
    REQUIRE(contains(conciliatoryResult.message, "派来使者"));
    REQUIRE(conciliatory.state().relations[riverIndex].relation == -17);
    REQUIRE(conciliatory.state().relations[riverIndex].trust == 2);
}

TEST_CASE("war overrides personality and an established trade route drives commerce") {
    constexpr std::uint32_t seedSelectingRiverDeer = 7U;
    const std::size_t riverIndex = tribeIndex(tribe::TribeIdV2::RiverDeer);

    auto warState = initialState(seedSelectingRiverDeer);
    auto& warRelation = warState.relations[riverIndex];
    warRelation.atWar = true;
    warRelation.tradeRoute = true;
    const int fearBefore = warRelation.fear;
    const int dependenceBeforeWar = warRelation.tradeDependence;
    tribe::CampaignGame wartime{warState};
    const auto warResult = endSeason(wartime);

    REQUIRE(contains(warResult.message, "双方仍处战争"));
    REQUIRE(contains(warResult.message, "优先集结兵力"));
    REQUIRE(wartime.state().relations[riverIndex].fear == fearBefore + 2);
    REQUIRE(wartime.state().relations[riverIndex].tradeDependence == dependenceBeforeWar);

    auto tradeState = initialState(seedSelectingRiverDeer);
    tradeState.relations[riverIndex].tradeRoute = true;
    const int dependenceBeforeTrade = tradeState.relations[riverIndex].tradeDependence;
    tribe::CampaignGame trading{tradeState};
    const auto tradeResult = endSeason(trading);

    REQUIRE(contains(tradeResult.message, "固定商路畅通"));
    REQUIRE(contains(tradeResult.message, "商队送来2食物"));
    REQUIRE(trading.state().relations[riverIndex].tradeDependence == dependenceBeforeTrade + 2);
}

TEST_CASE("external faction intelligence is revealed by contact trust and trade dependence") {
    const std::size_t tideIndex = tribeIndex(tribe::TribeIdV2::Tidesalt);
    auto hiddenState = initialState(17U);
    hiddenState.relations[tideIndex] = {};
    tribe::CampaignGame hidden{hiddenState};
    const std::string hiddenText = hidden.diplomacyText();

    REQUIRE(contains(hiddenText, "尚未充分接触"));
    REQUIRE(!contains(hiddenText, "澜母"));
    REQUIRE(!contains(hiddenText, "精明航运"));
    REQUIRE(!contains(hiddenText, "船主"));
    REQUIRE(!contains(hiddenText, "保护航路"));

    auto contactedState = hiddenState;
    discoverContact(contactedState, tribe::WorldLocationId::TidesaltHarbor);
    tribe::CampaignGame contacted{contactedState};
    const std::string contactedText = contacted.diplomacyText();
    REQUIRE(contains(contactedText, "澜母"));
    REQUIRE(contains(contactedText, "精明航运"));
    REQUIRE(contains(contactedText, "主导派系：船主"));
    REQUIRE(contains(contactedText, "诉求待查"));
    REQUIRE(!contains(contactedText, "保护航路"));

    auto trustedState = contactedState;
    trustedState.relations[tideIndex].trust = 20;
    tribe::CampaignGame trusted{trustedState};
    const std::string trustedText = trusted.diplomacyText();
    REQUIRE(contains(trustedText, "主导派系：船主"));
    REQUIRE(contains(trustedText, "保护航路"));
    REQUIRE(!contains(trustedText, "盐工"));

    auto dependentState = contactedState;
    dependentState.relations[tideIndex].tradeDependence = 30;
    tribe::CampaignGame dependent{dependentState};
    const std::string dependentText = dependent.diplomacyText();
    REQUIRE(contains(dependentText, "主导派系：船主"));
    REQUIRE(contains(dependentText, "诉求：保护航路"));
    REQUIRE(contains(dependentText, "其他派系：盐工"));
    REQUIRE(contains(dependentText, "提高盐价"));
}

TEST_CASE("undiscovered tribes stay hidden even when an inherited relation is nonzero") {
    const std::size_t riverIndex = tribeIndex(tribe::TribeIdV2::RiverDeer);
    auto state = initialState(31U);
    REQUIRE(state.relations[riverIndex].relation != 0);
    tribe::CampaignGame game{state};
    const std::string text = game.diplomacyText();
    REQUIRE(contains(text, "尚未充分接触"));
    REQUIRE(!contains(text, "禾角"));
    REQUIRE(!contains(text, "农耕者"));
}
