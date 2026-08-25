#include "tribe/formal_types.hpp"

#include <algorithm>
#include <tuple>

namespace tribe {

bool operator==(const GameState& left, const GameState& right) {
    return std::tie(left.mode, left.seed, left.turn, left.actionsLeft, left.population,
               left.food, left.wood, left.stone, left.herbs, left.warriors, left.morale,
               left.campDurability, left.temporaryDefense, left.rockfangStrength, left.phase,
               left.ending, left.pendingRaid, left.rockfangTruce, left.rockfangFortCaptured,
               left.discovered, left.scouted, left.buildings, left.technologies, left.relations,
               left.quests, left.eventSchedule, left.currentEvent)
        == std::tie(right.mode, right.seed, right.turn, right.actionsLeft, right.population,
               right.food, right.wood, right.stone, right.herbs, right.warriors, right.morale,
               right.campDurability, right.temporaryDefense, right.rockfangStrength, right.phase,
               right.ending, right.pendingRaid, right.rockfangTruce, right.rockfangFortCaptured,
               right.discovered, right.scouted, right.buildings, right.technologies, right.relations,
               right.quests, right.eventSchedule, right.currentEvent);
}

bool operator!=(const GameState& left, const GameState& right) { return !(left == right); }

Season seasonForTurn(const int turn) {
    const int normalized = std::max(1, turn) - 1;
    return static_cast<Season>(normalized % 4);
}

int yearForTurn(const int turn) { return (std::max(1, turn) - 1) / 4 + 1; }

int teamsForPopulation(const int population) {
    if (population < 5) return 1;
    if (population < 10) return 2;
    return 3;
}

std::string modeName(const GameMode mode) {
    return mode == GameMode::Standard ? "正式模式" : "快速展示模式";
}

std::string seasonName(const Season season) {
    switch (season) {
    case Season::Spring: return "春";
    case Season::Summer: return "夏";
    case Season::Autumn: return "秋";
    case Season::Winter: return "冬";
    }
    return "未知";
}

std::string phaseName(const Phase phase) {
    switch (phase) {
    case Phase::Playing: return "行动中";
    case Phase::AwaitingRaid: return "等待迎战";
    case Phase::FinalChoice: return "结局议事";
    case Phase::Finished: return "已经结束";
    }
    return "未知";
}

std::string endingName(const Ending ending) {
    switch (ending) {
    case Ending::Alliance: return "联盟共主";
    case Ending::Conquest: return "山河征服者";
    case Ending::Prosperity: return "燧火繁荣";
    case Ending::Migration: return "迁徙新生";
    case Ending::Extinction: return "部落覆灭";
    case Ending::None: break;
    }
    return "尚未结束";
}

} // namespace tribe
