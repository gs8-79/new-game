#include "seasonal_event_rules.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace tribe::seasonal_event_rules {

int extortionDamage(const GameState& state) {
    const bool tower = state.buildings[indexOf(BuildingId::Watchtower)] && state.workforce.scouts > 0;
    const bool wall = state.buildings[indexOf(BuildingId::Wall)] && state.workforce.campGuards > 0;
    return std::max(0, 4 - (tower ? 1 : 0) - (wall ? 1 : 0));
}

EventResolution resolveChoice(GameState& state, const int option) {
    if (!state.pendingEvent.active && state.pendingEvents.empty()) return {false, "本季没有待选择事件。 "};
    if (option != 1 && option != 2) return {false, "事件只能选择1或2。 "};

    const PendingEventKind kind = state.pendingEvents.empty() ? state.pendingEvent.kind : state.pendingEvents.front();
    std::string outcome;
    switch (kind) {
        case PendingEventKind::Refugees:
            if (option == 1) {
                if (state.food < 4) return {false, "接纳难民需要4食物，物资不足，事件未改变。 "};
                state.food -= 4;
                ++state.population;
                state.stability = std::max(0, state.stability - 2);
                outcome = "接纳难民：食物-4、人口+1、稳定-2。";
            } else {
                state.stability = std::max(0, state.stability - 3);
                outcome = "拒绝难民：稳定-3。";
            }
            break;
        case PendingEventKind::Disease:
            if (option == 1) {
                const int herbCost = state.buildings[indexOf(BuildingId::HealerHut)] && state.workforce.healers > 0
                                         ? 1
                                         : state.buildings[indexOf(BuildingId::HealerHut)] ? 3 : 2;
                if (state.herbs < herbCost) return {false, "医治疾病的草药不足，事件未改变。 "};
                state.herbs -= herbCost;
                state.stability = std::min(100, state.stability + 2);
                outcome = "医治疾病：草药-" + std::to_string(herbCost) + "、稳定+2。";
            } else {
                const int loss = state.buildings[indexOf(BuildingId::HealerHut)] ? 2 : 1;
                state.population = std::max(0, state.population - loss);
                state.warriors = std::min(state.warriors, state.population);
                const int stabilityLoss = state.buildings[indexOf(BuildingId::HealerHut)] ? 6 : 4;
                state.stability = std::max(0, state.stability - stabilityLoss);
                outcome = "隔离失败：人口-" + std::to_string(loss) + "、稳定-" + std::to_string(stabilityLoss) + "。";
            }
            break;
        case PendingEventKind::Extortion:
            if (option == 1) {
                if (state.food < 4) return {false, "缴纳勒索需要4食物，物资不足，事件未改变。 "};
                state.food -= 4;
                state.stability = std::min(100, state.stability + 3);
                outcome = "缴纳勒索：食物-4、稳定+3。";
            } else {
                const int damage = extortionDamage(state);
                state.stability = std::max(0, state.stability - 2);
                state.campDurability = std::max(0, state.campDurability - damage);
                outcome = "抵抗勒索：稳定-2、营地耐久-" + std::to_string(damage) + "。";
            }
            break;
        case PendingEventKind::FactionDemand:
            if (option == 1) {
                if (state.food < 3) return {false, "让步需要3食物，物资不足，事件未改变。 "};
                const bool council = state.buildings[indexOf(BuildingId::CouncilFire)] && state.workforce.envoys > 0;
                const int satisfaction = council ? 8 : 5;
                state.food -= 3;
                for (FactionState& faction : state.playerFactions)
                    faction.satisfaction = std::min(100, faction.satisfaction + satisfaction);
                outcome = "接受派系诉求：食物-3、全派系满意+" + std::to_string(satisfaction) + "。";
            } else {
                const int satisfactionLoss = state.buildings[indexOf(BuildingId::CouncilFire)] ? 10 : 6;
                const int stabilityLoss = state.buildings[indexOf(BuildingId::CouncilFire)] ? 4 : 2;
                for (FactionState& faction : state.playerFactions)
                    faction.satisfaction = std::max(0, faction.satisfaction - satisfactionLoss);
                state.stability = std::max(0, state.stability - stabilityLoss);
                outcome = "拒绝派系诉求：全派系满意-" + std::to_string(satisfactionLoss) + "、稳定-" +
                          std::to_string(stabilityLoss) + "。";
            }
            break;
    }
    if (!state.pendingEvents.empty()) state.pendingEvents.erase(state.pendingEvents.begin());
    state.pendingEvent.active = !state.pendingEvents.empty();
    if (state.pendingEvent.active) {
        state.pendingEvent.kind = state.pendingEvents.front();
        ++state.pendingEventIndex;
    } else {
        state.pendingEventIndex = 0;
    }
    return {true, std::move(outcome)};
}

} // namespace tribe::seasonal_event_rules
