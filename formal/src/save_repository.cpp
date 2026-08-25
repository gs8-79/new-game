#include "tribe/save_repository.hpp"

#include "tribe/game_engine.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace tribe {
namespace {

using Fields = std::unordered_map<std::string, std::string>;

const std::array<const char*, 30> kKeys{{
    "format", "version", "mode", "seed", "turn", "actions_left", "population", "food",
    "wood", "stone", "herbs", "warriors", "morale", "camp_durability", "temporary_defense",
    "rockfang_strength", "phase", "ending", "pending_raid", "rockfang_truce",
    "rockfang_fort_captured", "discovered", "scouted", "buildings", "technologies",
    "relations", "quests", "event_schedule", "current_event", "reserved"
}};

template <typename T, std::size_t Size>
std::string joinNumbers(const std::array<T, Size>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) output << ',';
        output << static_cast<int>(values[index]);
    }
    return output.str();
}

bool parseIntStrict(const std::string_view text, int& value) {
    if (text.empty()) return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    value = parsed;
    return true;
}

bool parseUnsignedStrict(const std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    unsigned long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parseBool(const std::string_view text, bool& value) {
    if (text == "0") {
        value = false;
        return true;
    }
    if (text == "1") {
        value = true;
        return true;
    }
    return false;
}

template <std::size_t Size>
bool parseIntArray(const std::string& text, std::array<int, Size>& values) {
    std::istringstream input(text);
    std::string token;
    std::size_t index = 0;
    while (std::getline(input, token, ',')) {
        if (index >= Size || !parseIntStrict(token, values[index])) return false;
        ++index;
    }
    return index == Size;
}

template <std::size_t Size>
bool parseBoolArray(const std::string& text, std::array<bool, Size>& values) {
    std::istringstream input(text);
    std::string token;
    std::size_t index = 0;
    while (std::getline(input, token, ',')) {
        if (index >= Size || !parseBool(token, values[index])) return false;
        ++index;
    }
    return index == Size;
}

std::string serialize(const GameState& state) {
    std::ostringstream output;
    output << "format=tribe-dawn\n"
           << "version=1\n"
           << "mode=" << static_cast<int>(state.mode) << '\n'
           << "seed=" << state.seed << '\n'
           << "turn=" << state.turn << '\n'
           << "actions_left=" << state.actionsLeft << '\n'
           << "population=" << state.population << '\n'
           << "food=" << state.food << '\n'
           << "wood=" << state.wood << '\n'
           << "stone=" << state.stone << '\n'
           << "herbs=" << state.herbs << '\n'
           << "warriors=" << state.warriors << '\n'
           << "morale=" << state.morale << '\n'
           << "camp_durability=" << state.campDurability << '\n'
           << "temporary_defense=" << state.temporaryDefense << '\n'
           << "rockfang_strength=" << state.rockfangStrength << '\n'
           << "phase=" << static_cast<int>(state.phase) << '\n'
           << "ending=" << static_cast<int>(state.ending) << '\n'
           << "pending_raid=" << (state.pendingRaid ? 1 : 0) << '\n'
           << "rockfang_truce=" << (state.rockfangTruce ? 1 : 0) << '\n'
           << "rockfang_fort_captured=" << (state.rockfangFortCaptured ? 1 : 0) << '\n'
           << "discovered=" << joinNumbers(state.discovered) << '\n'
           << "scouted=" << joinNumbers(state.scouted) << '\n'
           << "buildings=" << joinNumbers(state.buildings) << '\n'
           << "technologies=" << joinNumbers(state.technologies) << '\n'
           << "relations=" << joinNumbers(state.relations) << '\n'
           << "quests=" << joinNumbers(state.quests) << '\n'
           << "event_schedule=" << joinNumbers(state.eventSchedule) << '\n'
           << "current_event=" << state.currentEvent << '\n'
           << "reserved=0\n";
    return output.str();
}

bool parseFields(std::istream& input, Fields& fields, std::string& error) {
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0U) {
            error = "存档第" + std::to_string(lineNumber) + "行格式错误。";
            return false;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1U);
        if (!fields.emplace(key, value).second) {
            error = "存档包含重复字段：" + key;
            return false;
        }
    }
    std::set<std::string> expected;
    for (const char* key : kKeys) expected.emplace(key);
    if (fields.size() != expected.size()) {
        error = "存档字段数量不正确。";
        return false;
    }
    for (const auto& item : fields) {
        if (expected.find(item.first) == expected.end()) {
            error = "存档包含未知字段：" + item.first;
            return false;
        }
    }
    for (const auto& key : expected) {
        if (fields.find(key) == fields.end()) {
            error = "存档缺少字段：" + key;
            return false;
        }
    }
    return true;
}

bool deserialize(std::istream& input, GameState& candidate, std::string& error) {
    Fields fields;
    if (!parseFields(input, fields, error)) return false;
    if (fields.at("format") != "tribe-dawn") {
        error = "不是《燧火纪》正式版存档。";
        return false;
    }
    if (fields.at("version") != "1") {
        error = "不支持的存档版本。";
        return false;
    }
    if (fields.at("reserved") != "0") {
        error = "存档保留字段不合法。";
        return false;
    }

    GameState parsed;
    int mode = 0;
    int phase = 0;
    int ending = 0;
    const auto parseNumber = [&](const char* key, int& value) {
        if (parseIntStrict(fields.at(key), value)) return true;
        error = std::string("存档字段不是合法整数：") + key;
        return false;
    };
    if (!parseNumber("mode", mode) || !parseUnsignedStrict(fields.at("seed"), parsed.seed)
        || !parseNumber("turn", parsed.turn) || !parseNumber("actions_left", parsed.actionsLeft)
        || !parseNumber("population", parsed.population) || !parseNumber("food", parsed.food)
        || !parseNumber("wood", parsed.wood) || !parseNumber("stone", parsed.stone)
        || !parseNumber("herbs", parsed.herbs) || !parseNumber("warriors", parsed.warriors)
        || !parseNumber("morale", parsed.morale) || !parseNumber("camp_durability", parsed.campDurability)
        || !parseNumber("temporary_defense", parsed.temporaryDefense)
        || !parseNumber("rockfang_strength", parsed.rockfangStrength)
        || !parseNumber("phase", phase) || !parseNumber("ending", ending)
        || !parseNumber("current_event", parsed.currentEvent)) return false;

    if (mode < 0 || mode > 1 || phase < 0 || phase > 3 || ending < 0 || ending > 5) {
        error = "存档中的枚举编号超出范围。";
        return false;
    }
    parsed.mode = static_cast<GameMode>(mode);
    parsed.phase = static_cast<Phase>(phase);
    parsed.ending = static_cast<Ending>(ending);
    if (!parseBool(fields.at("pending_raid"), parsed.pendingRaid)
        || !parseBool(fields.at("rockfang_truce"), parsed.rockfangTruce)
        || !parseBool(fields.at("rockfang_fort_captured"), parsed.rockfangFortCaptured)) {
        error = "存档中的布尔字段不合法。";
        return false;
    }
    if (!parseBoolArray(fields.at("discovered"), parsed.discovered)
        || !parseBoolArray(fields.at("scouted"), parsed.scouted)
        || !parseBoolArray(fields.at("buildings"), parsed.buildings)
        || !parseBoolArray(fields.at("technologies"), parsed.technologies)
        || !parseIntArray(fields.at("relations"), parsed.relations)
        || !parseIntArray(fields.at("quests"), parsed.quests)
        || !parseIntArray(fields.at("event_schedule"), parsed.eventSchedule)) {
        error = "存档中的数组长度或内容不合法。";
        return false;
    }
    if (!GameEngine::validateState(parsed, error)) return false;
    candidate = std::move(parsed);
    error.clear();
    return true;
}

bool deserializeText(const std::string& text, GameState& candidate, std::string& error) {
    std::istringstream input(text);
    return deserialize(input, candidate, error);
}

} // namespace

SaveRepository::SaveRepository(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path SaveRepository::pathFor(const SaveSlot slot) const {
    switch (slot) {
    case SaveSlot::Slot1: return root_ / "slot1.sav";
    case SaveSlot::Slot2: return root_ / "slot2.sav";
    case SaveSlot::Slot3: return root_ / "slot3.sav";
    case SaveSlot::Autosave: return root_ / "autosave.sav";
    }
    return root_ / "invalid.sav";
}

bool SaveRepository::save(const GameState& state, const SaveSlot slot, std::string& error) const {
    if (!GameEngine::validateState(state, error)) return false;
    const std::string text = serialize(state);
    GameState roundTrip;
    if (!deserializeText(text, roundTrip, error) || roundTrip != state) {
        if (error.empty()) error = "存档写入前的往返校验失败。";
        return false;
    }

    const std::filesystem::path path = pathFor(slot);
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
        error = "无法创建存档目录：" + code.message();
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::filesystem::path backup = path;
    backup += ".bak";
    std::filesystem::remove(temporary, code);
    code.clear();

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "无法打开临时存档文件。";
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output) {
            error = "写入临时存档失败。";
            return false;
        }
    }

    const bool hadOriginal = std::filesystem::exists(path, code) && !code;
    if (hadOriginal) {
        std::filesystem::remove(backup, code);
        code.clear();
        std::filesystem::rename(path, backup, code);
        if (code) {
            error = "无法备份旧存档：" + code.message();
            std::filesystem::remove(temporary, code);
            return false;
        }
    }
    code.clear();
    std::filesystem::rename(temporary, path, code);
    if (code) {
        const std::string renameError = code.message();
        if (hadOriginal) {
            std::error_code restoreCode;
            std::filesystem::rename(backup, path, restoreCode);
        }
        std::filesystem::remove(temporary, code);
        error = "无法替换正式存档：" + renameError;
        return false;
    }
    if (hadOriginal) std::filesystem::remove(backup, code);
    error.clear();
    return true;
}

bool SaveRepository::load(const SaveSlot slot, GameState& candidate, std::string& error) const {
    const std::filesystem::path path = pathFor(slot);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "找不到" + slotName(slot) + "。";
        return false;
    }
    GameState parsed;
    if (!deserialize(input, parsed, error)) return false;
    candidate = std::move(parsed);
    error.clear();
    return true;
}

std::optional<SaveSlot> SaveRepository::parseSlot(const std::string_view text) {
    if (text == "1" || text == "slot1" || text == "存档1") return SaveSlot::Slot1;
    if (text == "2" || text == "slot2" || text == "存档2") return SaveSlot::Slot2;
    if (text == "3" || text == "slot3" || text == "存档3") return SaveSlot::Slot3;
    if (text == "auto" || text == "autosave" || text == "自动" || text == "自动档") return SaveSlot::Autosave;
    return std::nullopt;
}

std::string SaveRepository::slotName(const SaveSlot slot) {
    switch (slot) {
    case SaveSlot::Slot1: return "手动存档1";
    case SaveSlot::Slot2: return "手动存档2";
    case SaveSlot::Slot3: return "手动存档3";
    case SaveSlot::Autosave: return "自动存档";
    }
    return "未知存档";
}

} // namespace tribe
