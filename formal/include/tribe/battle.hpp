#pragma once

#include "tribe/formal_types.hpp"

#include <string>

namespace tribe {

struct BattleContext {
    bool isRaid = false;
    bool battlefieldScouted = false;
    bool hasFlintSpear = false;
    bool hasShieldWall = false;
    bool hasAmbushTraining = false;
    int warriors = 0;
    int morale = 0;
    int defense = 0;
    int enemyStrength = 0;
};

struct BattleResult {
    bool valid = false;
    bool retreated = false;
    bool victory = false;
    int playerPower = 0;
    int enemyPower = 0;
    int casualties = 0;
    int campDamage = 0;
    int enemyDamage = 0;
    int moraleDelta = 0;
    std::string message;
};

class BattleSystem {
public:
    BattleResult resolve(const BattleContext& context, Tactic tactic) const;
};

} // namespace tribe
