#include "tribe/battle.hpp"

#include "tribe/content.hpp"

#include <memory>
#include <sstream>

namespace tribe {
namespace {

class BattleTactic {
public:
    virtual ~BattleTactic() = default;
    virtual int modifier(const BattleContext& context) const = 0;
    virtual int failureCasualtyAdjustment() const { return 0; }
    virtual int enemyDamageOnWin() const = 0;
};

class AssaultTactic final : public BattleTactic {
public:
    int modifier(const BattleContext&) const override { return 3; }
    int failureCasualtyAdjustment() const override { return 1; }
    int enemyDamageOnWin() const override { return 5; }
};

class AmbushTactic final : public BattleTactic {
public:
    int modifier(const BattleContext& context) const override {
        if (!context.battlefieldScouted) return -2;
        return 5 + (context.hasAmbushTraining ? 3 : 0);
    }
    int enemyDamageOnWin() const override { return 6; }
};

class DefendTactic final : public BattleTactic {
public:
    int modifier(const BattleContext& context) const override {
        return context.defense + (context.hasShieldWall ? 2 : 0);
    }
    int enemyDamageOnWin() const override { return 3; }
};

std::unique_ptr<BattleTactic> makeTactic(const Tactic tactic) {
    switch (tactic) {
    case Tactic::Assault: return std::make_unique<AssaultTactic>();
    case Tactic::Ambush: return std::make_unique<AmbushTactic>();
    case Tactic::Defend: return std::make_unique<DefendTactic>();
    case Tactic::Retreat: break;
    }
    return nullptr;
}

} // namespace

BattleResult BattleSystem::resolve(const BattleContext& context, const Tactic tactic) const {
    BattleResult result;
    if (tactic == Tactic::Retreat) {
        result.retreated = true;
        result.moraleDelta = -5;
        result.message = "燧火战士保持队形撤退，没有人员伤亡，但食物减少3、士气下降5。";
        return result;
    }

    const auto strategy = makeTactic(tactic);
    const int weaponBonus = context.hasFlintSpear ? 2 : 0;
    result.playerPower = context.warriors * 2 + context.morale / 20 + weaponBonus
        + strategy->modifier(context);
    result.enemyPower = context.enemyStrength + (context.isRaid ? 2 : 0);
    const int margin = result.playerPower - result.enemyPower;
    result.victory = margin >= 0;

    if (margin >= 4) {
        result.casualties = 0;
        result.enemyDamage = strategy->enemyDamageOnWin();
        result.moraleDelta = 6;
    } else if (margin >= 0) {
        result.casualties = 1;
        result.enemyDamage = strategy->enemyDamageOnWin();
        result.moraleDelta = 3;
    } else if (margin >= -3) {
        result.casualties = 1 + strategy->failureCasualtyAdjustment();
        result.enemyDamage = 2;
        result.moraleDelta = -8;
    } else {
        result.casualties = 2 + strategy->failureCasualtyAdjustment();
        result.enemyDamage = 1;
        result.campDamage = context.isRaid ? 4 : 0;
        result.moraleDelta = -12;
    }

    if (result.casualties > context.warriors) result.casualties = context.warriors;
    std::ostringstream message;
    message << "战术：" << tacticName(tactic) << "。燧火战力 " << result.playerPower
            << "，敌方战力 " << result.enemyPower << "。"
            << (result.victory ? "战斗胜利。" : "战斗失利。")
            << " 战士伤亡 " << result.casualties << "，敌方战力下降 " << result.enemyDamage << "。";
    if (result.campDamage > 0) message << " 营地耐久下降 " << result.campDamage << "。";
    result.message = message.str();
    return result;
}

} // namespace tribe
