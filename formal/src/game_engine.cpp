#include "tribe/game_engine.hpp"

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

GameEngine::GameEngine(GameConfig config) {
    state_.mode = config.mode;
    state_.seed = config.seed;
    state_.tribeName = config.tribeName.empty() ? "燧火" : std::move(config.tribeName);
    state_.leaderName = config.leaderName.empty() ? "炎角" : std::move(config.leaderName);
    state_.leaderFocus = config.leaderFocus.empty() ? "生存" : std::move(config.leaderFocus);
    if (state_.mode == GameMode::Quick) {
        state_.season = 1;
        state_.seasonLimit = 8;
        state_.food = 42;
        state_.wood = 24;
        state_.stone = 12;
        state_.warriors = 5;
    } else if (state_.mode == GameMode::Long) {
        state_.seasonLimit = 32;
        state_.food = 20;
    } else {
        state_.food = 20;
    }
    state_.discovered[indexOf(WorldLocationId::Camp)] = true;
    state_.discovered[indexOf(WorldLocationId::Forest)] = true;
    state_.discovered[indexOf(WorldLocationId::RedPlain)] = true;
    state_.outposts[indexOf(WorldLocationId::Camp)] = true;

    state_.tribes[indexOf(TribeId::Player)] = {TribeId::Player,
                                               state_.tribeName,
                                               state_.leaderName,
                                               "",
                                               "青枝",
                                               state_.leaderFocus,
                                               {{"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
                                                {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
                                                {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::RiverDeer)] = {TribeId::RiverDeer,
                                                  "河鹿",
                                                  "牧河",
                                                  "",
                                                  "禾角",
                                                  "务实农业",
                                                  {{"农耕者", 50, 65, "稳定粮食", "禾角", FactionCrisis::Calm},
                                                   {"渡口商人", 30, 55, "扩大贸易", "舟苇", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::WhiteFeather)] = {TribeId::WhiteFeather,
                                                     "白羽",
                                                     "羽医",
                                                     "",
                                                     "轻翎",
                                                     "谨慎救助",
                                                     {{"医者", 45, 65, "救助伤者", "轻翎", FactionCrisis::Calm},
                                                      {"远望者", 35, 60, "共享情报", "苍羽", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Rockfang)] = {TribeId::Rockfang,
                                                 "岩牙",
                                                 "赤獠",
                                                 "",
                                                 "黑牙",
                                                 "强硬好战",
                                                 {{"战团", 55, 60, "取得战利品", "黑牙", FactionCrisis::Calm},
                                                  {"矿奴监工", 25, 45, "控制矿路", "裂石", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Tidesalt)] = {TribeId::Tidesalt,
                                                 "潮盐",
                                                 "澜母",
                                                 "",
                                                 "潮舟",
                                                 "精明航运",
                                                 {{"船主", 45, 60, "保护航路", "潮舟", FactionCrisis::Calm},
                                                  {"盐工", 35, 55, "提高盐价", "白沫", FactionCrisis::Calm}}};
    state_.tribes[indexOf(TribeId::Blackstone)] = {TribeId::Blackstone,
                                                   "玄石",
                                                   "玄砧",
                                                   "",
                                                   "黑炉",
                                                   "冷静工艺",
                                                   {{"工匠", 45, 60, "换取粮食", "黑炉", FactionCrisis::Calm},
                                                    {"雇佣战士", 35, 50, "获得装备", "玄盾", FactionCrisis::Calm}}};

    state_.relations[indexOf(TribeId::RiverDeer)] = {20, 15, 0, 0};
    state_.relations[indexOf(TribeId::WhiteFeather)] = {5, 5, 0, 0};
    state_.relations[indexOf(TribeId::Rockfang)] = {-40, 0, 35, 0};
    state_.relations[indexOf(TribeId::Tidesalt)] = {0, 0, 0, 0};
    state_.relations[indexOf(TribeId::Blackstone)] = {0, 0, 5, 0};
    state_.playerFactions = {{
        {"猎手派", 35, 65, "保证狩猎分配", "逐鹿", FactionCrisis::Calm},
        {"战士派", 35, 60, "维护战士荣誉", "石刃", FactionCrisis::Calm},
        {"长老派", 30, 65, "遵守议事传统", "白榆", FactionCrisis::Calm},
    }};

    state_.roster = {
        makeCampaignCharacter("青枝", Occupation::Envoy),  makeCampaignCharacter("石刃", Occupation::Warrior),
        makeCampaignCharacter("苍眼", Occupation::Scout),  makeCampaignCharacter("白榆", Occupation::Healer),
        makeCampaignCharacter("逐鹿", Occupation::Hunter), makeCampaignCharacter("岩槌", Occupation::Crafter),
        makeCampaignCharacter("芦风", Occupation::Hunter), makeCampaignCharacter("河矛", Occupation::Warrior),
    };
    const OperationResult equipped = equipItem(state_.roster.front(), EquipmentSlot::MainHand, makeCampaignLeaderBow());
    if (!equipped) throw std::logic_error("长期人物初始装备失败：" + equipped.message);
    state_.squads.push_back({"晨火队", "青枝", {"青枝", "石刃", "苍眼", "芦风"}, 0, 0, false, false});
    state_.leadershipHistory.push_back(state_.leaderName + "（初代首领）");
    addChronicle(state_, 3, "燧火新议", state_.leaderName + "召集族人，决定走向更广阔的世界。");

    std::string error;
    if (!validateState(state_, error)) throw std::logic_error("游戏初始状态无效：" + error);
}

GameEngine::GameEngine(GameState state) : state_(std::move(state)) {
    std::string error;
    if (!validateState(state_, error)) throw std::invalid_argument("游戏状态无效：" + error);
}

bool GameEngine::canSpendAction(ActionResult& result) const {
    if (state_.phase != GamePhase::Managing && state_.phase != GamePhase::Sandbox) {
        result = rejected("当前阶段不能执行部落行动。");
        return false;
    }
    if (state_.actionsLeft <= 0) {
        result = rejected("本季小队行动点已经用完，请结束回合。");
        return false;
    }
    return true;
}

ActionResult GameEngine::commit(GameState candidate, std::string message, const bool consumesAction,
                                const bool seasonAdvanced, const bool endingReached) {
    // 1. 根据候选状态重新计算派生的人口待重分配标志，避免调用方遗漏这一派生字段。
    population_rules::refreshWorkforceReassignment(candidate);
    // 2. 在移动任何数据前统一验证文本、人物、任务、装备和人口不变量；失败直接丢弃 candidate。
    std::string error;
    if (!validateState(candidate, error)) return rejected("行动后的状态未通过校验，已原子取消：" + error);
    // 3. 校验成功后才执行唯一一次状态替换，因此失败路径绝不会污染已提交的 state_。
    state_ = std::move(candidate);
    return {true, true, true, consumesAction, seasonAdvanced, endingReached, std::move(message)};
}

ActionResult GameEngine::rejected(std::string message) const {
    return {true, false, false, false, false, false, std::move(message)};
}

void GameEngine::addChronicle(GameState& candidate, const int importance, std::string title, std::string detail) const {
    candidate.chronicle.push_back(
        {candidate.season, std::clamp(importance, 1, 5), std::move(title), std::move(detail)});
    if (candidate.chronicle.size() > kMaximumChronicleEntries) candidate.chronicle.erase(candidate.chronicle.begin());
}

int GameEngine::availableTeams(const GameState& state) const {
    const WorkforceState& work = state.workforce;
    const int configuredCrews = (work.foodCrew >= 2 ? 1 : 0) + (work.woodCrew >= 2 ? 1 : 0) +
                                (work.stoneCrew >= 2 ? 1 : 0) + (work.herbCrew >= 2 ? 1 : 0);
    return std::min(7, 3 + configuredCrews);
}

WorldLocationId GameEngine::contactLocation(const TribeId tribe) const {
    switch (tribe) {
        case TribeId::RiverDeer:
            return WorldLocationId::RiverFord;
        case TribeId::WhiteFeather:
            return WorldLocationId::WhiteFeatherCamp;
        case TribeId::Rockfang:
            return WorldLocationId::OldPass;
        case TribeId::Tidesalt:
            return WorldLocationId::TidesaltHarbor;
        case TribeId::Blackstone:
            return WorldLocationId::BlackstoneWorkshop;
        case TribeId::Player:
        case TribeId::Count:
            return WorldLocationId::MountainMarket;
    }
    return WorldLocationId::MountainMarket;
}

bool GameEngine::locationDiscovered(const GameState& state, const WorldLocationId location) const {
    return state.discovered[indexOf(location)];
}

} // namespace tribe
