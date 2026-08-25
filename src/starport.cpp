#include "scenarios.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace mud {
namespace {

constexpr int kMaxHp = 20;
constexpr int kMaxOxygen = 12;
constexpr int kPlayerAttack = 6;
constexpr int kDroneAttack = 4;
constexpr int kDroneMaxHp = 12;

struct Room {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::pair<std::string, std::string>> exits;
};

struct StarportState {
    std::string roomId = "maintenance";
    int hp = kMaxHp;
    int oxygen = kMaxOxygen;
    int droneHp = kDroneMaxHp;
    bool warehouseSearched = false;
    bool reactorRepaired = false;
    std::vector<std::string> inventory{"wrench"};
    Outcome outcome = Outcome::Running;
};

const std::vector<Room>& rooms() {
    static const std::vector<Room> value{
        {"maintenance", "维修舱", "应急灯忽明忽暗。你握着一把维修扳手，北侧舱门通向中央走廊。",
         {{"north", "corridor"}}},
        {"corridor", "中央走廊", "警报声在金属走廊中回荡。西侧是仓库，东侧是能源舱。",
         {{"south", "maintenance"}, {"west", "warehouse"}, {"east", "reactor"}}},
        {"warehouse", "仓库", "散落的货箱挡住去路，一台失控的安保无人机守着备用零件。",
         {{"east", "corridor"}}},
        {"reactor", "能源舱", "主反应堆已经停机，控制台提示保险丝熔断。",
         {{"west", "corridor"}}},
    };
    return value;
}

const Room* findRoom(const std::string& id) {
    const auto& allRooms = rooms();
    const auto found = std::find_if(allRooms.begin(), allRooms.end(), [&](const Room& room) {
        return room.id == id;
    });
    return found == allRooms.end() ? nullptr : &*found;
}

std::string directionName(const std::string& direction) {
    if (direction == "north") return "北";
    if (direction == "south") return "南";
    if (direction == "east") return "东";
    if (direction == "west") return "西";
    return direction;
}

std::string normalizeDirection(const std::string& direction) {
    if (direction == "north" || direction == "n" || direction == "北") return "north";
    if (direction == "south" || direction == "s" || direction == "南") return "south";
    if (direction == "east" || direction == "e" || direction == "东") return "east";
    if (direction == "west" || direction == "w" || direction == "西") return "west";
    return {};
}

std::string outcomeText(const Outcome outcome) {
    switch (outcome) {
    case Outcome::Running: return "running";
    case Outcome::Won: return "won";
    case Outcome::Lost: return "lost";
    }
    return {};
}

bool parseOutcome(const std::string& text, Outcome& outcome) {
    if (text == "running") {
        outcome = Outcome::Running;
        return true;
    }
    if (text == "won") {
        outcome = Outcome::Won;
        return true;
    }
    if (text == "lost") {
        outcome = Outcome::Lost;
        return true;
    }
    return false;
}

class StarportScenario final : public Scenario {
public:
    std::string id() const override { return "starport"; }
    std::string title() const override { return "星港危机：最后的维修员"; }
    std::string intro() const override {
        return
            "曙光七号空间站断电，氧气只够支撑少量行动。\n"
            "击毁仓库中的安保无人机，找到保险丝，并在氧气耗尽前修复反应堆。";
    }
    std::string help() const override {
        return
            "星港命令（英文 / 中文）：\n"
            "  look / 查看                 查看当前舱室\n"
            "  status / 状态               查看生命与氧气\n"
            "  inventory / 背包            查看携带物品\n"
            "  go <direction> / 前往 <方向> 移动，方向可用北南东西\n"
            "  attack drone / 攻击 无人机   攻击安保无人机\n"
            "  search / 搜索               搜索仓库\n"
            "  use medkit / 使用 医疗包     恢复 4 点生命\n"
            "  repair reactor / 修复 反应堆 修复主反应堆\n";
    }
    void reset() override { state_ = StarportState{}; }

    CommandResult execute(const ParsedCommand& command) override {
        if (commandIs(command, {"look", "查看", "观察"})) {
            return noAction(command.args.empty(), describeRoom(), "用法：look / 查看");
        }
        if (commandIs(command, {"status", "状态"})) {
            std::ostringstream message;
            message << "生命：" << state_.hp << '/' << kMaxHp
                    << "  氧气：" << state_.oxygen << '/' << kMaxOxygen
                    << "  攻击：" << kPlayerAttack;
            return noAction(command.args.empty(), message.str(), "用法：status / 状态");
        }
        if (commandIs(command, {"inventory", "inv", "背包"})) {
            return noAction(command.args.empty(), describeInventory(), "用法：inventory / 背包");
        }
        if (commandIs(command, {"go", "move", "前往", "移动"})) return move(command);
        if (commandIs(command, {"attack", "攻击"})) return attack(command);
        if (commandIs(command, {"search", "搜索"})) return search(command);
        if (commandIs(command, {"use", "使用"})) return use(command);
        if (commandIs(command, {"repair", "修复"})) return repair(command);
        return {};
    }

    Outcome outcome() const override { return state_.outcome; }

    SaveFields saveFields() const override {
        std::ostringstream inventory;
        for (std::size_t index = 0; index < state_.inventory.size(); ++index) {
            if (index != 0U) inventory << ',';
            inventory << state_.inventory[index];
        }
        return {
            {"room", state_.roomId},
            {"hp", std::to_string(state_.hp)},
            {"oxygen", std::to_string(state_.oxygen)},
            {"drone_hp", std::to_string(state_.droneHp)},
            {"warehouse_searched", state_.warehouseSearched ? "1" : "0"},
            {"reactor_repaired", state_.reactorRepaired ? "1" : "0"},
            {"inventory", inventory.str()},
            {"outcome", outcomeText(state_.outcome)},
        };
    }

    bool loadFields(const SaveFields& fields, std::string& error) override {
        error.clear();
        if (!hasExactKeys(fields,
                          {"room", "hp", "oxygen", "drone_hp", "warehouse_searched",
                           "reactor_repaired", "inventory", "outcome"},
                          error)) {
            return false;
        }

        StarportState candidate;
        candidate.roomId = fields.at("room");
        if (findRoom(candidate.roomId) == nullptr) {
            error = "存档中的舱室不存在。";
            return false;
        }
        if (!parseIntStrict(fields.at("hp"), candidate.hp) || candidate.hp < 0 || candidate.hp > kMaxHp) {
            error = "存档中的生命值无效。";
            return false;
        }
        if (!parseIntStrict(fields.at("oxygen"), candidate.oxygen) ||
            candidate.oxygen < 0 || candidate.oxygen > kMaxOxygen) {
            error = "存档中的氧气值无效。";
            return false;
        }
        if (!parseIntStrict(fields.at("drone_hp"), candidate.droneHp) ||
            (candidate.droneHp != 0 && candidate.droneHp != 6 && candidate.droneHp != kDroneMaxHp)) {
            error = "存档中的无人机生命值无效。";
            return false;
        }
        if (!parseBoolStrict(fields.at("warehouse_searched"), candidate.warehouseSearched) ||
            !parseBoolStrict(fields.at("reactor_repaired"), candidate.reactorRepaired)) {
            error = "存档中的任务标记无效。";
            return false;
        }
        if (!parseOutcome(fields.at("outcome"), candidate.outcome)) {
            error = "存档中的结局状态无效。";
            return false;
        }
        if (!parseInventory(fields.at("inventory"), candidate.inventory, error)) return false;
        if (!validateState(candidate, error)) return false;

        state_ = std::move(candidate);
        return true;
    }

private:
    static CommandResult noAction(
        const bool valid, std::string successMessage, std::string errorMessage) {
        return {true, false, false, valid ? std::move(successMessage) : std::move(errorMessage)};
    }

    bool hasItem(const std::string& id) const {
        return std::find(state_.inventory.begin(), state_.inventory.end(), id) != state_.inventory.end();
    }

    void removeItem(const std::string& id) {
        const auto found = std::find(state_.inventory.begin(), state_.inventory.end(), id);
        if (found != state_.inventory.end()) state_.inventory.erase(found);
    }

    CommandResult finishAction(std::string message) {
        state_.oxygen = std::max(0, state_.oxygen - 1);
        if (state_.reactorRepaired) state_.outcome = Outcome::Won;
        else if (state_.hp <= 0 || state_.oxygen <= 0) state_.outcome = Outcome::Lost;

        message += "\n剩余氧气：" + std::to_string(state_.oxygen) + "。";
        if (state_.outcome == Outcome::Won) {
            message += "\n反应堆重新点火，空间站恢复供电与生命支持！";
        } else if (state_.outcome == Outcome::Lost) {
            message += state_.hp <= 0 ? "\n你伤势过重，维修任务失败。" : "\n氧气耗尽，维修任务失败。";
        }
        return {true, true, true, std::move(message)};
    }

    std::string describeRoom() const {
        const Room* room = findRoom(state_.roomId);
        if (room == nullptr) return "当前位置数据损坏。";
        std::ostringstream message;
        message << '[' << room->name << "]\n" << room->description << "\n出口：";
        for (std::size_t index = 0; index < room->exits.size(); ++index) {
            if (index != 0U) message << "、";
            message << directionName(room->exits[index].first);
        }
        if (state_.roomId == "warehouse") {
            if (state_.droneHp > 0) message << "\n安保无人机生命：" << state_.droneHp << '/' << kDroneMaxHp << "。";
            else if (!state_.warehouseSearched) message << "\n无人机已停止运转，货箱现在可以搜索。";
        }
        if (state_.roomId == "reactor" && !state_.reactorRepaired) {
            message << "\n需要维修扳手和备用保险丝才能修复反应堆。";
        }
        return message.str();
    }

    std::string describeInventory() const {
        std::ostringstream message;
        message << "背包：";
        for (std::size_t index = 0; index < state_.inventory.size(); ++index) {
            if (index != 0U) message << "、";
            const std::string& item = state_.inventory[index];
            if (item == "wrench") message << "维修扳手";
            else if (item == "fuse") message << "备用保险丝";
            else if (item == "medkit") message << "医疗包";
        }
        return message.str();
    }

    CommandResult move(const ParsedCommand& command) {
        if (command.args.size() != 1U) {
            return {true, false, false, "用法：go <north|south|east|west> / 前往 <北|南|东|西>"};
        }
        const std::string direction = normalizeDirection(command.args[0]);
        if (direction.empty()) return {true, false, false, "未知方向，请使用 north/south/east/west 或北/南/东/西。"};

        const Room* room = findRoom(state_.roomId);
        const auto exit = std::find_if(room->exits.begin(), room->exits.end(), [&](const auto& pair) {
            return pair.first == direction;
        });
        if (exit == room->exits.end()) return {true, false, false, "这个方向没有可通行的舱门。"};
        state_.roomId = exit->second;
        return finishAction("你进入了" + findRoom(state_.roomId)->name + "。\n" + describeRoom());
    }

    CommandResult attack(const ParsedCommand& command) {
        if (command.args.size() != 1U ||
            (command.args[0] != "drone" && command.args[0] != "无人机" && command.args[0] != "安保无人机")) {
            return {true, false, false, "用法：attack drone / 攻击 无人机"};
        }
        if (state_.roomId != "warehouse") return {true, false, false, "这里没有安保无人机。"};
        if (state_.droneHp <= 0) return {true, false, false, "安保无人机已经停止运转。"};

        state_.droneHp = std::max(0, state_.droneHp - kPlayerAttack);
        std::ostringstream message;
        message << "你用维修扳手击中无人机，造成 " << kPlayerAttack << " 点伤害。";
        if (state_.droneHp > 0) {
            state_.hp = std::max(0, state_.hp - kDroneAttack);
            message << "\n无人机反击，造成 " << kDroneAttack << " 点伤害。当前生命："
                    << state_.hp << '/' << kMaxHp << "。";
        } else {
            message << "\n安保无人机失去动力，仓库恢复安全。";
        }
        return finishAction(message.str());
    }

    CommandResult search(const ParsedCommand& command) {
        if (!command.args.empty()) return {true, false, false, "用法：search / 搜索"};
        if (state_.roomId != "warehouse") return {true, false, false, "这里没有值得搜索的维修物资。"};
        if (state_.droneHp > 0) return {true, false, false, "无人机封锁了货箱，必须先使它停止运转。"};
        if (state_.warehouseSearched) return {true, false, false, "仓库已经搜索完毕。"};

        state_.warehouseSearched = true;
        state_.inventory.push_back("fuse");
        state_.inventory.push_back("medkit");
        return finishAction("你找到了一枚备用保险丝和一个医疗包。");
    }

    CommandResult use(const ParsedCommand& command) {
        if (command.args.size() != 1U ||
            (command.args[0] != "medkit" && command.args[0] != "医疗包")) {
            return {true, false, false, "用法：use medkit / 使用 医疗包"};
        }
        if (!hasItem("medkit")) return {true, false, false, "背包中没有医疗包。"};
        if (state_.hp >= kMaxHp) return {true, false, false, "生命值已满，无需使用医疗包。"};

        removeItem("medkit");
        const int oldHp = state_.hp;
        state_.hp = std::min(kMaxHp, state_.hp + 4);
        return finishAction("你使用医疗包，恢复 " + std::to_string(state_.hp - oldHp) + " 点生命。");
    }

    CommandResult repair(const ParsedCommand& command) {
        if (command.args.size() != 1U ||
            (command.args[0] != "reactor" && command.args[0] != "反应堆")) {
            return {true, false, false, "用法：repair reactor / 修复 反应堆"};
        }
        if (state_.roomId != "reactor") return {true, false, false, "必须在能源舱内才能修复反应堆。"};
        if (!hasItem("wrench") || !hasItem("fuse")) {
            return {true, false, false, "维修失败：需要维修扳手和备用保险丝。"};
        }

        removeItem("fuse");
        state_.reactorRepaired = true;
        return finishAction("你更换保险丝并重新连接主能源线路。");
    }

    static bool parseInventory(const std::string& text, std::vector<std::string>& inventory, std::string& error) {
        inventory.clear();
        if (text.empty() || text.front() == ',' || text.back() == ',') {
            error = "存档中的背包格式无效。";
            return false;
        }
        std::set<std::string> seen;
        std::istringstream stream(text);
        std::string item;
        while (std::getline(stream, item, ',')) {
            if (item.empty() || (item != "wrench" && item != "fuse" && item != "medkit")) {
                error = "存档中的背包物品无效。";
                return false;
            }
            if (!seen.emplace(item).second) {
                error = "存档中的背包物品重复。";
                return false;
            }
        }
        for (const auto* id : {"wrench", "fuse", "medkit"}) {
            if (seen.find(id) != seen.end()) inventory.emplace_back(id);
        }
        return true;
    }

    static bool validateState(const StarportState& state, std::string& error) {
        const auto contains = [&](const std::string& id) {
            return std::find(state.inventory.begin(), state.inventory.end(), id) != state.inventory.end();
        };
        if (!contains("wrench")) {
            error = "存档缺少不可丢弃的维修扳手。";
            return false;
        }
        if (!state.warehouseSearched && (contains("fuse") || contains("medkit"))) {
            error = "存档中的仓库搜索状态与背包冲突。";
            return false;
        }
        if (state.warehouseSearched && state.droneHp != 0) {
            error = "存档中的仓库搜索状态与无人机状态冲突。";
            return false;
        }
        if (state.warehouseSearched && !state.reactorRepaired && !contains("fuse")) {
            error = "存档中的保险丝状态无效。";
            return false;
        }
        if (state.reactorRepaired &&
            (!state.warehouseSearched || state.droneHp != 0 || state.roomId != "reactor" || contains("fuse"))) {
            error = "存档中的反应堆状态不一致。";
            return false;
        }
        if (state.outcome == Outcome::Running &&
            (state.hp <= 0 || state.oxygen <= 0 || state.reactorRepaired)) {
            error = "运行中的存档包含结束条件。";
            return false;
        }
        if (state.outcome == Outcome::Won && (!state.reactorRepaired || state.hp <= 0)) {
            error = "胜利存档的任务状态不一致。";
            return false;
        }
        if (state.outcome == Outcome::Lost &&
            (state.reactorRepaired || (state.hp > 0 && state.oxygen > 0))) {
            error = "失败存档的任务状态不一致。";
            return false;
        }
        return true;
    }

    StarportState state_;
};

} // namespace

std::unique_ptr<Scenario> makeStarportScenario() {
    return std::make_unique<StarportScenario>();
}

} // namespace mud
