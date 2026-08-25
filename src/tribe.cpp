#include "scenarios.hpp"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace mud {
namespace {

enum class TribeEnding { None = 0, Alliance = 1, Conquest = 2, Survival = 3, Extinction = 4 };

struct TribeState {
    int season = 1;
    int actionsLeft = 2;
    int population = 12;
    int food = 18;
    int wood = 6;
    int warriors = 2;
    int morale = 60;
    int defense = 0;
    int riverRelation = 20;
    int rockfangStrength = 10;
    bool wallBuilt = false;
    TribeEnding ending = TribeEnding::None;
};

constexpr int kActionsPerSeason = 2;
constexpr int kFinalSeason = 4;
constexpr int kWallCost = 10;
constexpr int kWallDefense = 6;

int clampPercent(const int value) { return std::clamp(value, 0, 100); }

bool hasOneArgument(const ParsedCommand& command) { return command.args.size() == 1U; }

bool argumentIs(const ParsedCommand& command, const std::initializer_list<std::string_view> aliases) {
    if (!hasOneArgument(command)) {
        return false;
    }
    const std::string& argument = command.args.front();
    return std::any_of(aliases.begin(), aliases.end(), [&](const std::string_view alias) {
        return argument == alias;
    });
}

std::string endingName(const TribeEnding ending) {
    switch (ending) {
    case TribeEnding::Alliance: return "联盟之主";
    case TribeEnding::Conquest: return "征服者";
    case TribeEnding::Survival: return "艰难幸存";
    case TribeEnding::Extinction: return "部落覆灭";
    case TribeEnding::None: break;
    }
    return "尚未结束";
}

class TribeScenario final : public Scenario {
public:
    std::string id() const override { return "tribe"; }
    std::string title() const override { return "燧火纪：部落黎明"; }
    std::string intro() const override {
        return
            "你领导燧火部落度过四个季节。每季有 2 点行动：积累食物和木材，\n"
            "或选择训练战士、修筑木墙、联合河鹿部落、征服岩牙部落。\n"
            "第三季岩牙会发动袭击；第四季结束时，根据你的选择产生结局。";
    }
    std::string help() const override {
        return
            "部落命令：\n"
            "  status / 状态                       查看资源与局势\n"
            "  map / 地图                          查看三方势力\n"
            "  gather food / 采集 食物             食物 +8，消耗 1 行动\n"
            "  gather wood / 采集 木材             木材 +6，消耗 1 行动\n"
            "  train / 训练                        食物 -3、战士 +1，消耗 1 行动\n"
            "  build wall / 建造 木墙              木材 -10、防御 +6，消耗 1 行动\n"
            "  diplomacy riverdeer / 外交 河鹿部落  食物 -4、关系 +25，消耗 1 行动\n"
            "  attack rockfang / 进攻 岩牙部落      与岩牙决战，消耗 1 行动\n"
            "  endturn / 结束回合 / 下一季          结算食物、事件并进入下一季\n";
    }

    void reset() override { state_ = TribeState{}; }

    CommandResult execute(const ParsedCommand& command) override {
        if (state_.ending != TribeEnding::None) {
            return {true, false, false, "本局已经结束，结局：" + endingName(state_.ending) + "。"};
        }
        if (commandIs(command, {"status", "状态"})) {
            return command.args.empty() ? CommandResult{true, false, false, statusText()}
                                        : usage("用法：status / 状态");
        }
        if (commandIs(command, {"map", "地图"})) {
            if (!command.args.empty()) return usage("用法：map / 地图");
            return {true, false, false,
                "[河谷：河鹿部落 关系 " + std::to_string(state_.riverRelation) + "]\n"
                "              |\n"
                "[燧火营地] —— [石岭：岩牙战力 " + std::to_string(state_.rockfangStrength) + "]"};
        }
        if (commandIs(command, {"gather", "采集"})) {
            if (!hasOneArgument(command)) return usage("用法：gather food|wood / 采集 食物|木材");
            if (argumentIs(command, {"food", "食物"})) return gatherFood();
            if (argumentIs(command, {"wood", "木材"})) return gatherWood();
            return usage("只能采集 food/食物 或 wood/木材。");
        }
        if (commandIs(command, {"train", "训练"})) {
            return command.args.empty() ? train() : usage("用法：train / 训练");
        }
        if (commandIs(command, {"build", "建造"})) {
            return argumentIs(command, {"wall", "木墙"}) ? buildWall()
                                                            : usage("用法：build wall / 建造 木墙");
        }
        if (commandIs(command, {"diplomacy", "外交"})) {
            return argumentIs(command, {"riverdeer", "河鹿", "河鹿部落"}) ? diplomacy()
                                                                           : usage("用法：diplomacy riverdeer / 外交 河鹿部落");
        }
        if (commandIs(command, {"attack", "进攻", "攻击"})) {
            return argumentIs(command, {"rockfang", "岩牙", "岩牙部落"}) ? attack()
                                                                         : usage("用法：attack rockfang / 进攻 岩牙部落");
        }
        if (commandIs(command, {"endturn", "结束回合", "下一季"})) {
            return command.args.empty() ? endTurn() : usage("用法：endturn / 结束回合 / 下一季");
        }
        return {};
    }

    Outcome outcome() const override {
        if (state_.ending == TribeEnding::None) return Outcome::Running;
        return state_.ending == TribeEnding::Extinction ? Outcome::Lost : Outcome::Won;
    }

    SaveFields saveFields() const override {
        return {
            {"season", std::to_string(state_.season)},
            {"actions_left", std::to_string(state_.actionsLeft)},
            {"population", std::to_string(state_.population)},
            {"food", std::to_string(state_.food)},
            {"wood", std::to_string(state_.wood)},
            {"warriors", std::to_string(state_.warriors)},
            {"morale", std::to_string(state_.morale)},
            {"defense", std::to_string(state_.defense)},
            {"river_relation", std::to_string(state_.riverRelation)},
            {"rockfang_strength", std::to_string(state_.rockfangStrength)},
            {"wall_built", state_.wallBuilt ? "1" : "0"},
            {"ending", std::to_string(static_cast<int>(state_.ending))},
        };
    }

    bool loadFields(const SaveFields& fields, std::string& error) override {
        if (!hasExactKeys(fields, {
                "season", "actions_left", "population", "food", "wood", "warriors", "morale",
                "defense", "river_relation", "rockfang_strength", "wall_built", "ending"}, error)) {
            return false;
        }

        TribeState candidate;
        int endingValue = 0;
        const auto parseNumber = [&](const char* key, int& target) {
            if (parseIntStrict(fields.at(key), target)) return true;
            error = std::string("存档字段不是合法整数：") + key;
            return false;
        };
        if (!parseNumber("season", candidate.season)
            || !parseNumber("actions_left", candidate.actionsLeft)
            || !parseNumber("population", candidate.population)
            || !parseNumber("food", candidate.food)
            || !parseNumber("wood", candidate.wood)
            || !parseNumber("warriors", candidate.warriors)
            || !parseNumber("morale", candidate.morale)
            || !parseNumber("defense", candidate.defense)
            || !parseNumber("river_relation", candidate.riverRelation)
            || !parseNumber("rockfang_strength", candidate.rockfangStrength)
            || !parseNumber("ending", endingValue)) {
            return false;
        }
        if (!parseBoolStrict(fields.at("wall_built"), candidate.wallBuilt)) {
            error = "存档字段不是合法布尔值：wall_built";
            return false;
        }
        if (endingValue < static_cast<int>(TribeEnding::None)
            || endingValue > static_cast<int>(TribeEnding::Extinction)) {
            error = "存档结局编号超出范围。";
            return false;
        }
        candidate.ending = static_cast<TribeEnding>(endingValue);
        if (!validate(candidate, error)) return false;

        state_ = candidate;
        error.clear();
        return true;
    }

private:
    static bool validate(const TribeState& state, std::string& error) {
        if (state.season < 1 || state.season > kFinalSeason
            || state.actionsLeft < 0 || state.actionsLeft > kActionsPerSeason
            || state.population < 0 || state.population > 100
            || state.food < 0 || state.food > 1000
            || state.wood < 0 || state.wood > 1000
            || state.warriors < 0 || state.warriors > state.population
            || state.morale < 0 || state.morale > 100
            || state.riverRelation < 0 || state.riverRelation > 100
            || state.rockfangStrength < 0 || state.rockfangStrength > 10) {
            error = "存档数值超出允许范围。";
            return false;
        }
        if ((state.wallBuilt && state.defense != kWallDefense)
            || (!state.wallBuilt && state.defense != 0)) {
            error = "木墙状态与防御值不一致。";
            return false;
        }
        if (state.ending == TribeEnding::None && state.population == 0) {
            error = "仍在进行的游戏不能拥有零人口。";
            return false;
        }
        if (state.ending != TribeEnding::None && state.season != kFinalSeason) {
            error = "已经结束的游戏必须位于第四季。";
            return false;
        }
        if (state.ending == TribeEnding::Extinction && state.population != 0) {
            error = "覆灭结局必须拥有零人口。";
            return false;
        }
        if (state.ending != TribeEnding::None && state.ending != TribeEnding::Extinction
            && state.population == 0) {
            error = "成功结局必须仍有人口存活。";
            return false;
        }
        if (state.ending == TribeEnding::Alliance && state.riverRelation < 70) {
            error = "联盟结局的关系值不足。";
            return false;
        }
        if (state.ending == TribeEnding::Conquest
            && (state.riverRelation >= 70 || state.rockfangStrength != 0)) {
            error = "征服结局与外交或岩牙状态不一致。";
            return false;
        }
        if (state.ending == TribeEnding::Survival
            && (state.riverRelation >= 70 || state.rockfangStrength == 0)) {
            error = "幸存结局与外交或岩牙状态不一致。";
            return false;
        }
        return true;
    }

    CommandResult usage(std::string message) const { return {true, false, false, std::move(message)}; }

    CommandResult requireAction() const {
        return state_.actionsLeft <= 0
            ? CommandResult{true, false, false, "本季行动点已用完，请结束回合。"}
            : CommandResult{true, true, true, {}};
    }

    CommandResult finishAction(std::string message) {
        --state_.actionsLeft;
        if (state_.population <= 0) {
            state_.population = 0;
            state_.ending = TribeEnding::Extinction;
            message += "\n最后的族人倒下了，结局：部落覆灭。";
        }
        return {true, true, true, std::move(message)};
    }

    CommandResult gatherFood() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        state_.food += 8;
        return finishAction("猎手与采集者带回 8 单位食物。");
    }
    CommandResult gatherWood() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        state_.wood += 6;
        return finishAction("伐木队带回 6 单位木材。");
    }
    CommandResult train() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        if (state_.food < 3) return {true, false, false, "训练需要 3 单位食物，状态未改变。"};
        if (state_.warriors >= state_.population) return {true, false, false, "所有族人都已是战士，状态未改变。"};
        state_.food -= 3;
        ++state_.warriors;
        return finishAction("一名族人完成训练，成为战士。");
    }
    CommandResult buildWall() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        if (state_.wallBuilt) return {true, false, false, "木墙已经建成，不能重复建造。"};
        if (state_.wood < kWallCost) return {true, false, false, "建造木墙需要 10 单位木材，状态未改变。"};
        state_.wood -= kWallCost;
        state_.defense = kWallDefense;
        state_.wallBuilt = true;
        return finishAction("环绕营地的木墙建成，防御提高 6 点。");
    }
    CommandResult diplomacy() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        if (state_.food < 4) return {true, false, false, "拜访河鹿部落需要 4 单位食物作为礼物，状态未改变。"};
        state_.food -= 4;
        state_.riverRelation = clampPercent(state_.riverRelation + 25);
        return finishAction("河鹿部落接受礼物，双方关系提高 25 点。");
    }
    CommandResult attack() {
        const auto allowed = requireAction();
        if (!allowed.stateChanged) return allowed;
        if (state_.rockfangStrength == 0) return {true, false, false, "岩牙部落已经被击败，无需再次进攻。"};
        if (state_.warriors == 0) return {true, false, false, "没有战士可以出征，状态未改变。"};
        const int attackStrength = state_.warriors * 2 + state_.morale / 20;
        if (attackStrength >= state_.rockfangStrength) {
            state_.rockfangStrength = 0;
            state_.morale = clampPercent(state_.morale + 15);
            return finishAction("战士攻下石岭，岩牙部落已被击败，士气提高 15 点。");
        }
        --state_.warriors;
        --state_.population;
        state_.morale = clampPercent(state_.morale - 10);
        return finishAction("进攻失败，一名战士阵亡，士气下降 10 点。");
    }

    CommandResult endTurn() {
        std::ostringstream message;
        const int foodCost = (state_.population + 2) / 3;
        message << "第 " << state_.season << " 季结算：需要 " << foodCost << " 单位食物。";
        if (state_.food >= foodCost) {
            state_.food -= foodCost;
            message << " 食物供应充足。";
        } else {
            state_.food = 0;
            state_.population = std::max(0, state_.population - 2);
            state_.warriors = std::min(state_.warriors, state_.population);
            state_.morale = clampPercent(state_.morale - 15);
            message << " 食物不足，人口减少 2，士气下降 15。";
        }
        if (state_.season == 3 && state_.rockfangStrength > 0 && state_.population > 0) {
            const int defenseStrength = state_.warriors * 2 + state_.defense + state_.morale / 20;
            message << "\n岩牙部落来袭！你的防守力为 " << defenseStrength << "。";
            if (defenseStrength >= state_.rockfangStrength) {
                state_.morale = clampPercent(state_.morale + 5);
                message << " 袭击被击退，士气提高 5 点。";
            } else {
                state_.population = std::max(0, state_.population - 2);
                state_.warriors = std::min(state_.warriors, state_.population);
                state_.morale = clampPercent(state_.morale - 15);
                message << " 防线失守，人口减少 2，士气下降 15。";
            }
        }
        if (state_.population <= 0) {
            state_.population = 0;
            state_.ending = TribeEnding::Extinction;
            message << "\n结局：部落覆灭。";
        } else if (state_.season == kFinalSeason) {
            if (state_.riverRelation >= 70) state_.ending = TribeEnding::Alliance;
            else if (state_.rockfangStrength == 0) state_.ending = TribeEnding::Conquest;
            else state_.ending = TribeEnding::Survival;
            message << "\n结局：" << endingName(state_.ending) << "。";
        } else {
            ++state_.season;
            state_.actionsLeft = kActionsPerSeason;
            message << "\n进入第 " << state_.season << " 季，恢复 2 点行动。";
        }
        return {true, true, false, message.str()};
    }

    std::string statusText() const {
        std::ostringstream output;
        output << "第 " << state_.season << "/4 季  行动点：" << state_.actionsLeft << "/2\n"
               << "人口：" << state_.population << "  食物：" << state_.food << "  木材：" << state_.wood << '\n'
               << "战士：" << state_.warriors << "  士气：" << state_.morale << "  防御：" << state_.defense << '\n'
               << "河鹿关系：" << state_.riverRelation << "  岩牙战力：" << state_.rockfangStrength << '\n'
               << "木墙：" << (state_.wallBuilt ? "已建成" : "未建造");
        return output.str();
    }

    TribeState state_;
};

} // namespace

std::unique_ptr<Scenario> makeTribeScenario() { return std::make_unique<TribeScenario>(); }

} // namespace mud
