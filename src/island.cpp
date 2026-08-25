#include "scenarios.hpp"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace mud {
namespace {

enum class Location { Beach, Jungle, Spring, Cliff };

struct IslandState {
    int day = 5;
    int actionPoints = 4;
    int health = 100;
    int hunger = 80;
    int thirst = 70;
    Location location = Location::Beach;
    int wood = 0;
    int vine = 0;
    int water = 0;
    int cloth = 1;
    int coconut = 1;
    int rope = 0;
    bool signalBuilt = false;
    Outcome outcome = Outcome::Running;
};

std::string locationId(const Location location) {
    switch (location) {
    case Location::Beach: return "beach";
    case Location::Jungle: return "jungle";
    case Location::Spring: return "spring";
    case Location::Cliff: return "cliff";
    }
    return "";
}

std::string locationName(const Location location) {
    switch (location) {
    case Location::Beach: return "海滩";
    case Location::Jungle: return "丛林";
    case Location::Spring: return "淡水泉";
    case Location::Cliff: return "悬崖顶";
    }
    return "未知地点";
}

bool parseLocation(const std::string& text, Location& location) {
    if (text == "beach" || text == "海滩") {
        location = Location::Beach;
    } else if (text == "jungle" || text == "丛林" || text == "森林") {
        location = Location::Jungle;
    } else if (text == "spring" || text == "淡水泉" || text == "泉水") {
        location = Location::Spring;
    } else if (text == "cliff" || text == "悬崖" || text == "悬崖顶") {
        location = Location::Cliff;
    } else {
        return false;
    }
    return true;
}

bool adjacent(const Location from, const Location to) {
    if (from == Location::Beach) {
        return to == Location::Jungle;
    }
    if (from == Location::Jungle) {
        return to == Location::Beach || to == Location::Spring || to == Location::Cliff;
    }
    if (from == Location::Spring || from == Location::Cliff) {
        return to == Location::Jungle;
    }
    return false;
}

std::string outcomeId(const Outcome outcome) {
    switch (outcome) {
    case Outcome::Running: return "running";
    case Outcome::Won: return "won";
    case Outcome::Lost: return "lost";
    }
    return "";
}

bool parseOutcome(const std::string& text, Outcome& outcome) {
    if (text == "running") {
        outcome = Outcome::Running;
    } else if (text == "won") {
        outcome = Outcome::Won;
    } else if (text == "lost") {
        outcome = Outcome::Lost;
    } else {
        return false;
    }
    return true;
}

bool noArguments(const ParsedCommand& command) {
    return command.args.empty();
}

bool oneArgument(const ParsedCommand& command) {
    return command.args.size() == 1U;
}

CommandResult information(std::string message) {
    return {true, false, false, std::move(message)};
}

CommandResult rejection(std::string message) {
    return {true, false, false, std::move(message)};
}

CommandResult action(std::string message) {
    return {true, true, true, std::move(message)};
}

class IslandScenario final : public Scenario {
public:
    IslandScenario() { reset(); }

    std::string id() const override { return "island"; }
    std::string title() const override { return "荒岛求生7日：最后三天"; }
    std::string intro() const override {
        return
            "救援船将在第七天经过。你从第5天早晨的海滩开始，必须采集藤蔓和木材，\n"
            "制作绳索登上悬崖，并在第七天点亮信号火。食物和淡水同样不能忽视。";
    }
    std::string help() const override {
        return
            "场景命令：\n"
            "  look / 查看                 查看当前地点\n"
            "  map / 地图                  查看地点连接\n"
            "  status / 状态               查看生存状态\n"
            "  inventory / 背包            查看物资\n"
            "  go <place> / 前往 <地点>    移动（beach/jungle/spring/cliff）\n"
            "  gather <resource> / 采集 <资源> 采集 wood/vine/water\n"
            "  collect water / 收集 水     在淡水泉取水\n"
            "  craft rope / 制作 绳索      消耗2份藤蔓制作绳索\n"
            "  use <item> / 使用 <物品>     使用 coconut/water\n"
            "  build signal / 建造 信号火  第7天在悬崖消耗3木材和1布料\n"
            "  end / 结束                  提前结束当天";
    }

    void reset() override { state_ = IslandState{}; }

    CommandResult execute(const ParsedCommand& command) override {
        if (state_.outcome != Outcome::Running) {
            return rejection("本次求生已经结束，请返回菜单重新开始。");
        }

        CommandResult result;
        if (commandIs(command, {"look", "查看", "观察"})) {
            result = executeLook(command);
        } else if (commandIs(command, {"map", "地图"})) {
            result = executeMap(command);
        } else if (commandIs(command, {"status", "状态"})) {
            result = executeStatus(command);
        } else if (commandIs(command, {"inventory", "背包", "物品"})) {
            result = executeInventory(command);
        } else if (commandIs(command, {"go", "前往", "移动"})) {
            result = executeGo(command);
        } else if (commandIs(command, {"gather", "collect", "采集", "收集"})) {
            result = executeGather(command);
        } else if (commandIs(command, {"craft", "制作"})) {
            result = executeCraft(command);
        } else if (commandIs(command, {"use", "使用"})) {
            result = executeUse(command);
        } else if (commandIs(command, {"build", "建造", "点燃"})) {
            result = executeBuild(command);
        } else if (commandIs(command, {"end", "结束", "休息"})) {
            return executeEnd(command);
        } else {
            return {};
        }

        if (result.stateChanged && result.consumesAction) {
            --state_.actionPoints;
            if (state_.actionPoints == 0 && state_.outcome == Outcome::Running) {
                result.message += "\n" + settleNight();
            }
        }
        return result;
    }

    Outcome outcome() const override { return state_.outcome; }

    SaveFields saveFields() const override {
        return {
            {"day", std::to_string(state_.day)},
            {"actionPoints", std::to_string(state_.actionPoints)},
            {"health", std::to_string(state_.health)},
            {"hunger", std::to_string(state_.hunger)},
            {"thirst", std::to_string(state_.thirst)},
            {"location", locationId(state_.location)},
            {"wood", std::to_string(state_.wood)},
            {"vine", std::to_string(state_.vine)},
            {"water", std::to_string(state_.water)},
            {"cloth", std::to_string(state_.cloth)},
            {"coconut", std::to_string(state_.coconut)},
            {"rope", std::to_string(state_.rope)},
            {"signalBuilt", state_.signalBuilt ? "1" : "0"},
            {"outcome", outcomeId(state_.outcome)},
        };
    }

    bool loadFields(const SaveFields& fields, std::string& error) override {
        if (!hasExactKeys(
                fields,
                {"day", "actionPoints", "health", "hunger", "thirst", "location", "wood", "vine",
                 "water", "cloth", "coconut", "rope", "signalBuilt", "outcome"},
                error)) {
            return false;
        }

        IslandState candidate;
        if (!parseIntegerField(fields, "day", candidate.day, error) ||
            !parseIntegerField(fields, "actionPoints", candidate.actionPoints, error) ||
            !parseIntegerField(fields, "health", candidate.health, error) ||
            !parseIntegerField(fields, "hunger", candidate.hunger, error) ||
            !parseIntegerField(fields, "thirst", candidate.thirst, error) ||
            !parseIntegerField(fields, "wood", candidate.wood, error) ||
            !parseIntegerField(fields, "vine", candidate.vine, error) ||
            !parseIntegerField(fields, "water", candidate.water, error) ||
            !parseIntegerField(fields, "cloth", candidate.cloth, error) ||
            !parseIntegerField(fields, "coconut", candidate.coconut, error) ||
            !parseIntegerField(fields, "rope", candidate.rope, error)) {
            return false;
        }
        if (!parseLocation(fields.at("location"), candidate.location)) {
            error = "存档地点无效。";
            return false;
        }
        if (!parseBoolStrict(fields.at("signalBuilt"), candidate.signalBuilt)) {
            error = "存档字段 signalBuilt 必须为0或1。";
            return false;
        }
        if (!parseOutcome(fields.at("outcome"), candidate.outcome)) {
            error = "存档结局状态无效。";
            return false;
        }
        if (!validateCandidate(candidate, error)) {
            return false;
        }

        state_ = candidate;
        return true;
    }

private:
    static bool parseIntegerField(
        const SaveFields& fields,
        const std::string& key,
        int& value,
        std::string& error) {
        if (!parseIntStrict(fields.at(key), value)) {
            error = "存档字段 " + key + " 不是合法整数。";
            return false;
        }
        return true;
    }

    static bool validateCandidate(const IslandState& candidate, std::string& error) {
        const auto between = [](const int value, const int low, const int high) {
            return value >= low && value <= high;
        };
        if (!between(candidate.day, 5, 7) || !between(candidate.actionPoints, 0, 4) ||
            !between(candidate.health, 0, 100) || !between(candidate.hunger, 0, 100) ||
            !between(candidate.thirst, 0, 100)) {
            error = "存档中的日期、行动点或生存属性越界。";
            return false;
        }
        if (!between(candidate.wood, 0, 99) || !between(candidate.vine, 0, 99) ||
            !between(candidate.water, 0, 99) || !between(candidate.cloth, 0, 1) ||
            !between(candidate.coconut, 0, 1) || !between(candidate.rope, 0, 1)) {
            error = "存档中的物资数量越界。";
            return false;
        }
        if (candidate.location == Location::Cliff && candidate.rope == 0) {
            error = "没有绳索时不能位于悬崖顶。";
            return false;
        }
        if (candidate.outcome == Outcome::Running &&
            (candidate.health == 0 || candidate.actionPoints == 0 || candidate.signalBuilt)) {
            error = "运行中的存档状态不一致。";
            return false;
        }
        if (candidate.outcome == Outcome::Won &&
            (candidate.day != 7 || candidate.location != Location::Cliff || !candidate.signalBuilt ||
             candidate.health == 0 || candidate.cloth != 0)) {
            error = "成功结局的存档状态不一致。";
            return false;
        }
        if (candidate.outcome == Outcome::Lost && (candidate.day != 7 || candidate.signalBuilt)) {
            error = "失败结局不能包含已建成的信号火。";
            return false;
        }
        if (candidate.signalBuilt && candidate.outcome != Outcome::Won) {
            error = "信号火状态与结局不一致。";
            return false;
        }
        return true;
    }

    CommandResult executeLook(const ParsedCommand& command) const {
        if (!noArguments(command)) {
            return rejection("用法：look / 查看");
        }
        std::string description;
        switch (state_.location) {
        case Location::Beach:
            description = "破损的木船搁浅在海滩。向丛林前进才能寻找制作信号火的材料。";
            break;
        case Location::Jungle:
            description = "潮湿的丛林里有木材和藤蔓。这里通往海滩、淡水泉和悬崖。";
            break;
        case Location::Spring:
            description = "岩缝中流出清澈淡水，可以在这里收集 water。";
            break;
        case Location::Cliff:
            description = "海面一览无余。第七天可在此用3份木材和1份布料建造信号火。";
            break;
        }
        return information("[" + locationName(state_.location) + "] " + description);
    }

    CommandResult executeMap(const ParsedCommand& command) const {
        if (!noArguments(command)) {
            return rejection("用法：map / 地图");
        }
        return information(
            "地图：海滩 beach <-> 丛林 jungle <-> 淡水泉 spring\n"
            "                         |\n"
            "                     悬崖 cliff（需要绳索）\n"
            "当前位置：" + locationName(state_.location));
    }

    CommandResult executeStatus(const ParsedCommand& command) const {
        if (!noArguments(command)) {
            return rejection("用法：status / 状态");
        }
        std::ostringstream output;
        output << "第" << state_.day << "天  行动点 " << state_.actionPoints << "/4  地点："
               << locationName(state_.location) << "\n生命 " << state_.health << "/100  饥饿 "
               << state_.hunger << "/100  口渴 " << state_.thirst << "/100";
        return information(output.str());
    }

    CommandResult executeInventory(const ParsedCommand& command) const {
        if (!noArguments(command)) {
            return rejection("用法：inventory / 背包");
        }
        std::ostringstream output;
        output << "木材 wood=" << state_.wood << "，藤蔓 vine=" << state_.vine
               << "，淡水 water=" << state_.water << "，布料 cloth=" << state_.cloth
               << "，椰子 coconut=" << state_.coconut << "，绳索 rope=" << state_.rope;
        return information(output.str());
    }

    CommandResult executeGo(const ParsedCommand& command) {
        if (!oneArgument(command)) {
            return rejection("用法：go <beach|jungle|spring|cliff> / 前往 <地点>");
        }
        Location destination;
        if (!parseLocation(command.args[0], destination)) {
            return rejection("未知地点。可用地点：beach、jungle、spring、cliff。");
        }
        if (destination == state_.location) {
            return rejection("你已经在" + locationName(destination) + "。此操作不消耗行动点。");
        }
        if (!adjacent(state_.location, destination)) {
            return rejection("该地点不相邻，请先回到丛林中转。此操作不消耗行动点。");
        }
        if (destination == Location::Cliff && state_.rope == 0) {
            return rejection("悬崖过于陡峭，必须先用2份藤蔓制作绳索。此操作不消耗行动点。");
        }
        state_.location = destination;
        return action("你到达了" + locationName(destination) + "。");
    }

    CommandResult executeGather(const ParsedCommand& command) {
        if (!oneArgument(command)) {
            return rejection("用法：gather <wood|vine|water> / 采集 <木材|藤蔓|水>");
        }
        const std::string& resource = command.args[0];
        if (resource == "wood" || resource == "木材") {
            if (state_.location != Location::Jungle) {
                return rejection("只有丛林能够采集木材。此操作不消耗行动点。");
            }
            if (state_.wood == 99) {
                return rejection("木材已经达到试玩版上限。此操作不消耗行动点。");
            }
            ++state_.wood;
            return action("你采集了1份木材。");
        }
        if (resource == "vine" || resource == "藤蔓") {
            if (state_.location != Location::Jungle) {
                return rejection("只有丛林能够采集藤蔓。此操作不消耗行动点。");
            }
            if (state_.vine == 99) {
                return rejection("藤蔓已经达到试玩版上限。此操作不消耗行动点。");
            }
            ++state_.vine;
            return action("你采集了1份藤蔓。");
        }
        if (resource == "water" || resource == "水" || resource == "淡水") {
            if (state_.location != Location::Spring) {
                return rejection("只有淡水泉能够取水。此操作不消耗行动点。");
            }
            if (state_.water == 99) {
                return rejection("淡水已经达到试玩版上限。此操作不消耗行动点。");
            }
            ++state_.water;
            return action("你收集了1份淡水。");
        }
        return rejection("未知资源。可采集 wood、vine、water。此操作不消耗行动点。");
    }

    CommandResult executeCraft(const ParsedCommand& command) {
        if (!oneArgument(command) ||
            (command.args[0] != "rope" && command.args[0] != "绳索")) {
            return rejection("用法：craft rope / 制作 绳索");
        }
        if (state_.rope != 0) {
            return rejection("你已经有绳索了。此操作不消耗行动点。");
        }
        if (state_.vine < 2) {
            return rejection("制作绳索需要2份藤蔓。此操作不消耗行动点。");
        }
        state_.vine -= 2;
        state_.rope = 1;
        return action("你把藤蔓编成了结实的绳索，现在可以攀上悬崖。");
    }

    CommandResult executeUse(const ParsedCommand& command) {
        if (!oneArgument(command)) {
            return rejection("用法：use <coconut|water> / 使用 <椰子|水>");
        }
        const std::string& item = command.args[0];
        if (item == "coconut" || item == "椰子") {
            if (state_.coconut == 0) {
                return rejection("背包里没有椰子。");
            }
            --state_.coconut;
            state_.hunger = std::min(100, state_.hunger + 25);
            return {true, true, false, "你吃下椰子，饥饿值恢复25点。"};
        }
        if (item == "water" || item == "水" || item == "淡水") {
            if (state_.water == 0) {
                return rejection("背包里没有淡水。");
            }
            --state_.water;
            state_.thirst = std::min(100, state_.thirst + 50);
            return {true, true, false, "你喝下淡水，口渴值恢复50点。"};
        }
        return rejection("无法使用该物品。可使用 coconut 或 water。");
    }

    CommandResult executeBuild(const ParsedCommand& command) {
        if (!oneArgument(command) ||
            (command.args[0] != "signal" && command.args[0] != "signal-fire" &&
             command.args[0] != "信号火")) {
            return rejection("用法：build signal / 建造 信号火");
        }
        if (state_.location != Location::Cliff) {
            return rejection("信号火必须建在悬崖顶。此操作不消耗行动点。");
        }
        if (state_.day != 7) {
            return rejection("救援船尚未靠近。为了避免燃料烧尽，只能在第7天点亮信号火。");
        }
        if (state_.wood < 3 || state_.cloth < 1) {
            return rejection("建造信号火需要3份木材和1份布料。此操作不消耗行动点。");
        }
        state_.wood -= 3;
        --state_.cloth;
        state_.signalBuilt = true;
        state_.outcome = Outcome::Won;
        return action("信号火在悬崖上熊熊燃烧！救援船发现了你，荒岛求生成功。");
    }

    CommandResult executeEnd(const ParsedCommand& command) {
        if (!noArguments(command)) {
            return rejection("用法：end / 结束");
        }
        return {true, true, false, "你决定结束今天的行动。\n" + settleNight()};
    }

    std::string settleNight() {
        state_.hunger = std::max(0, state_.hunger - 25);
        state_.thirst = std::max(0, state_.thirst - 30);

        int damage = 0;
        if (state_.hunger == 0) {
            damage += 15;
        }
        if (state_.thirst == 0) {
            damage += 25;
        }
        state_.health = std::max(0, state_.health - damage);

        std::ostringstream message;
        message << "夜间结算：饥饿=" << state_.hunger << "，口渴=" << state_.thirst;
        if (damage > 0) {
            message << "，因缺乏食水损失" << damage << "点生命";
        }
        message << "。";

        if (state_.health == 0) {
            state_.outcome = Outcome::Lost;
            message << " 你没能坚持到救援。";
            return message.str();
        }
        if (state_.day == 7) {
            state_.outcome = Outcome::Lost;
            message << " 第七天结束，救援船没有看到信号，已经驶远。";
            return message.str();
        }

        ++state_.day;
        state_.actionPoints = 4;
        message << " 第" << state_.day << "天开始，行动点恢复为4。";
        return message.str();
    }

    IslandState state_;
};

} // namespace

std::unique_ptr<Scenario> makeIslandScenario() {
    return std::make_unique<IslandScenario>();
}

} // namespace mud
