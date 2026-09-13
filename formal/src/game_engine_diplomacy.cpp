#include "tribe/game_engine.hpp"

#include "command_parser.hpp"
#include "game_engine_internal.hpp"
#include "population_rules.hpp"
#include "seasonal_event_rules.hpp"
#include "state_safety.hpp"
#include "war_rules.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace tribe {

using namespace game_engine_detail;

using command_parser::Command;
using command_parser::equalsAny;
using command_parser::parse;
using command_parser::verbIs;
bool GameEngine::diplomacyUsedThisSeason(const TribeId tribe) const {
    const std::string marker = "本季外交：" + tribeName(tribe);
    return std::any_of(state_.chronicle.begin(), state_.chronicle.end(), [&](const ChronicleEntry& entry) {
        return entry.season == state_.season && entry.title == marker;
    });
}

void GameEngine::finalizeDiplomacy(GameState& candidate, const TribeId tribe) const {
    // 编年史是候选状态的一部分，必须在 commit 校验前写入，不能绕过原子提交直接改写 state_。
    addChronicle(candidate, 1, "本季外交：" + tribeName(tribe), "该部落本季的主动外交已经完成。");
}

void GameEngine::spendAction(GameState& candidate, const int cost) const {
    candidate.actionsLeft = std::max(0, candidate.actionsLeft - cost);
}

ActionResult GameEngine::talk(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能普通交谈，请先谈停战。");
    GameState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::SharedLanguage)] ? 9 : 5;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 4);
    spendAction(candidate);
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "与" + tribeName(tribe) + "交谈：关系+" + std::to_string(bonus) + "，信任+4。",
                  true);
}

ActionResult GameEngine::gift(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能送礼，请先谈停战。");
    if (state_.food < 4) return rejected("送礼需要4食物。");
    GameState candidate = state_;
    candidate.food -= 4;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int bonus = candidate.technologies[indexOf(TechnologyId::GiftCustoms)] ? 14 : 9;
    relation.relation = relationClamp(relation.relation + bonus);
    relation.trust = percentClamp(relation.trust + 6);
    spendAction(candidate);
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "向" + tribeName(tribe) + "送礼：关系+" + std::to_string(bonus) + "。", true);
}

int GameEngine::resourceValue(const GameState& state, const ResourceKind resource) const {
    const int amount = resource == ResourceKind::Food    ? state.food
                       : resource == ResourceKind::Wood  ? state.wood
                       : resource == ResourceKind::Stone ? state.stone
                       : resource == ResourceKind::Herbs ? state.herbs
                                                         : state.hides;
    const int base = resource == ResourceKind::Food    ? 3
                     : resource == ResourceKind::Wood  ? 2
                     : resource == ResourceKind::Stone ? 4
                     : resource == ResourceKind::Herbs ? 5
                                                       : 4;
    return std::max(1, base + (amount < 10 ? 3 : amount < 20 ? 1 : 0));
}

int& GameEngine::resourceRef(GameState& state, const ResourceKind resource) const {
    switch (resource) {
        case ResourceKind::Food:
            return state.food;
        case ResourceKind::Wood:
            return state.wood;
        case ResourceKind::Stone:
            return state.stone;
        case ResourceKind::Herbs:
            return state.herbs;
        case ResourceKind::Hides:
            return state.hides;
    }
    return state.food;
}

ActionResult GameEngine::trade(const TribeId tribe, const ResourceKind offered, const ResourceKind requested) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点，不能贸易。");
    if (offered == requested) return rejected("以物易物必须选择两种不同资源。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (relation.atWar) return rejected("战争中不能贸易。");
    if (relation.relation < -10) return rejected("关系过低，对方拒绝贸易。");
    const int offeredAmount = 4;
    if (resourceRef(state_, offered) < offeredAmount) return rejected("给出的资源不足。");

    GameState candidate = state_;
    const int relationBonus = std::max(0, relation.relation) / 25;
    const int requestedAmount = std::clamp(
        offeredAmount * resourceValue(candidate, offered) / resourceValue(candidate, requested) + relationBonus, 1, 10);
    resourceRef(candidate, offered) -= offeredAmount;
    resourceRef(candidate, requested) += requestedAmount;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.relation = relationClamp(changed.relation + 2);
    changed.trust = percentClamp(changed.trust + 3);
    changed.tradeDependence = percentClamp(changed.tradeDependence + 8);
    ++candidate.tradeCount;
    candidate.tradePartners[indexOf(tribe)] = true;
    spendAction(candidate);
    std::string message = "与" + tribeName(tribe) + "以" + std::to_string(offeredAmount) + resourceName(offered) +
                          "换得" + std::to_string(requestedAmount) + resourceName(requested) +
                          "。价格受稀缺、关系和依赖影响。";
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), std::move(message), true);
}

ActionResult GameEngine::openTradeRoute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    auto required = WorldLocationId::MountainMarket;
    if (tribe == TribeId::RiverDeer)
        required = WorldLocationId::RiverFord;
    else if (tribe == TribeId::WhiteFeather)
        required = WorldLocationId::WhiteFeatherCamp;
    else if (tribe == TribeId::Rockfang)
        required = WorldLocationId::OldPass;
    else if (tribe == TribeId::Tidesalt)
        required = WorldLocationId::TidesaltHarbor;
    else if (tribe == TribeId::Blackstone)
        required = WorldLocationId::BlackstoneWorkshop;
    if (!locationDiscovered(state_, required)) return rejected("尚未发现连接该部落的贸易地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能开通商路。");
    if (state_.relations[indexOf(tribe)].tradeDependence < 16) return rejected("至少先完成两次有效贸易，建立依赖。");
    if (state_.relations[indexOf(tribe)].tradeRoute) return rejected("该商路已经开通。");
    GameState candidate = state_;
    candidate.relations[indexOf(tribe)].tradeRoute = true;
    candidate.stability = std::min(100, candidate.stability + 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "开通商路", state_.tribeName + "与" + tribeName(tribe) + "建立稳定商路。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "商路开通，稳定+3，后续贸易更可靠。", true);
}

ActionResult GameEngine::marriage(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.marriage) return rejected("双方已经存在联姻关系。");
    if (relation.atWar || relation.relation < 60 || relation.trust < 50)
        return rejected("联姻需要关系60、信任50且不在战争中。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.marriage = true;
    changed.relation = relationClamp(changed.relation + 15);
    changed.trust = percentClamp(changed.trust + 10);
    candidate.stability = std::min(100, candidate.stability + 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "联姻",
                 "具名使者在共同火坛前交换信物，也留下继承争议的可能。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "联姻完成：关系+15、信任+10、稳定+4。", true);
}

ActionResult GameEngine::offerTribute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (state_.relations[indexOf(tribe)].atWar) return rejected("战争中不能直接建立朝贡，请先停战。");
    if (state_.relations[indexOf(tribe)].otherPaysTribute) return rejected("对方正在向我方进贡，不能同时双向朝贡。");
    if (state_.relations[indexOf(tribe)].alliance) return rejected("盟友之间不能建立屈从式朝贡关系。");
    if (state_.food < 6) return rejected("建立朝贡需要先交6食物。");
    if (state_.relations[indexOf(tribe)].playerPaysTribute) return rejected("已经向该部落朝贡。");
    GameState candidate = state_;
    candidate.food -= 6;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.playerPaysTribute = true;
    relation.relation = relationClamp(relation.relation + 12);
    relation.fear = percentClamp(relation.fear - 5);
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "建立朝贡：换取和平，但内部稳定-3，每季继续支付2食物。", true);
}

ActionResult GameEngine::demandTribute(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.atWar) return rejected("战争中不能直接索贡，请先结束战斗并停战。");
    if (relation.playerPaysTribute) return rejected("我方正在朝贡，不能同时要求对方进贡。");
    if (relation.alliance || relation.marriage) return rejected("联盟或联姻关系下不能强行索贡。");
    if (relation.otherPaysTribute) return rejected("对方已经进贡。");
    if (relation.fear < 60 || state_.warriors < 6) return rejected("索贡需要恐惧60且至少6名战士。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.otherPaysTribute = true;
    changed.relation = relationClamp(changed.relation - 12);
    candidate.stability = std::max(0, candidate.stability - 2);
    spendAction(candidate);
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "对方同意每季进贡2食物，但关系和内部公平感下降。", true);
}

ActionResult GameEngine::alliance(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    const auto& relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点。");
    if (relation.alliance) return rejected("双方已经结盟。");
    if (relation.atWar || relation.relation < 70 || relation.trust < 60)
        return rejected("结盟需要关系70、信任60且不在战争中。");
    if (!state_.technologies[indexOf(TechnologyId::Confederation)]) return rejected("需要研究部落联盟技术。");
    GameState candidate = state_;
    candidate.relations[indexOf(tribe)].alliance = true;
    candidate.stability = std::min(100, candidate.stability + 5);
    spendAction(candidate);
    addChronicle(candidate, 4, "与" + tribeName(tribe) + "结盟", "双方在共同火坛前立誓互助。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "联盟成立，稳定+5。", true);
}

ActionResult GameEngine::declareWar(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    auto relation = state_.relations[indexOf(tribe)];
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现与该部落接触的地点，不能宣战。");
    if (relation.atWar) return rejected("双方已经处于战争状态。");
    GameState candidate = state_;
    auto& changed = candidate.relations[indexOf(tribe)];
    changed.atWar = true;
    changed.truce = false;
    changed.alliance = false;
    changed.marriage = false;
    changed.tradeRoute = false;
    changed.playerPaysTribute = false;
    changed.otherPaysTribute = false;
    changed.relation = relationClamp(changed.relation - 35);
    candidate.stability = std::max(0, candidate.stability - 4);
    spendAction(candidate);
    addChronicle(candidate, 3, "向" + tribeName(tribe) + "宣战", "战鼓响起，族人开始准备长期代价。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "宣战生效：关系-35、稳定-4。请组建军队后出征。", true);
}

ActionResult GameEngine::negotiateTruce(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!state_.relations[indexOf(tribe)].atWar) return rejected("双方并未交战。");
    if (state_.food < 5) return rejected("停战谈判需要5食物作为赔偿和宴席。");
    GameState candidate = state_;
    candidate.food -= 5;
    auto& relation = candidate.relations[indexOf(tribe)];
    relation.atWar = false;
    relation.truce = true;
    relation.relation = std::max(-30, relation.relation);
    relation.trust = std::max(10, relation.trust);
    spendAction(candidate);
    addChronicle(candidate, 3, "与" + tribeName(tribe) + "停战", "双方同意暂时放下武器，伤痕仍未消失。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "停战达成，战争状态解除。", true);
}

ActionResult GameEngine::raid(const TribeId tribe) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (diplomacyUsedThisSeason(tribe)) return rejected("本季已对该部落完成主动外交，请等待下一季。");
    if (!locationDiscovered(state_, contactLocation(tribe))) return rejected("尚未发现通往该部落的道路，不能劫掠。");
    if (state_.warriors < 2) return rejected("劫掠至少需要2名战士。");
    GameState candidate = state_;
    auto& relation = candidate.relations[indexOf(tribe)];
    const int gain = 5 + static_cast<int>((candidate.seed + candidate.season + indexOf(tribe)) % 4U);
    candidate.food += gain;
    relation.relation = relationClamp(relation.relation - 25);
    relation.fear = percentClamp(relation.fear + 15);
    relation.trust = percentClamp(relation.trust - 12);
    relation.alliance = false;
    relation.marriage = false;
    relation.tradeRoute = false;
    relation.playerPaysTribute = false;
    relation.otherPaysTribute = false;
    relation.truce = false;
    candidate.stability = std::max(0, candidate.stability - 3);
    spendAction(candidate);
    addChronicle(candidate, 2, "劫掠" + tribeName(tribe), "获得食物" + std::to_string(gain) + "，也播下新的仇恨。");
    finalizeDiplomacy(candidate, tribe);
    return commit(std::move(candidate), "劫掠获得" + std::to_string(gain) + "食物；关系-25、恐惧+15、稳定-3。", true);
}

ActionResult GameEngine::appeaseFaction(const std::size_t faction) {
    ActionResult result;
    if (!canSpendAction(result)) return result;
    if (state_.food < 4) return rejected("安抚派系需要公平分配4食物。");
    GameState candidate = state_;
    candidate.food -= 4;
    FactionState& changed = candidate.playerFactions[faction];
    changed.satisfaction = std::min(100, changed.satisfaction + 20);
    changed.crisis = static_cast<FactionCrisis>(std::max(0, static_cast<int>(changed.crisis) - 2));
    candidate.stability = std::min(100, candidate.stability + 8);
    const bool refusalRemains =
        std::any_of(candidate.playerFactions.begin(), candidate.playerFactions.end(),
                    [](const FactionState& state) { return state.crisis == FactionCrisis::Refusal; });
    for (PermanentSquad& squad : candidate.squads) squad.refusingOrders = refusalRemains;
    spendAction(candidate);
    const std::string message = "公平分配缓和了" + changed.name + "的不满：满意+20、稳定+8。";
    return commit(std::move(candidate), message, true);
}

} // namespace tribe
