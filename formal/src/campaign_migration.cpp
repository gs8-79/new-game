#include "tribe/campaign_migration.hpp"

#include "tribe/game_engine.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace tribe {
namespace {

using Fields = std::unordered_map<std::string, std::string>;
constexpr std::uintmax_t kMaximumLegacySaveBytes = 1024U * 1024U;

const std::array<const char*, 30> kLegacyKeys{{
    "format", "version", "mode", "seed", "turn", "actions_left", "population", "food",
    "wood", "stone", "herbs", "warriors", "morale", "camp_durability", "temporary_defense",
    "rockfang_strength", "phase", "ending", "pending_raid", "rockfang_truce",
    "rockfang_fort_captured", "discovered", "scouted", "buildings", "technologies",
    "relations", "quests", "event_schedule", "current_event", "reserved"
}};

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
        || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
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

bool parseFields(std::istream& input, Fields& fields, std::string& error) {
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0U) {
            error = "旧存档第" + std::to_string(lineNumber) + "行格式错误。";
            return false;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1U);
        if (!fields.emplace(key, value).second) {
            error = "旧存档包含重复字段：" + key;
            return false;
        }
    }

    std::set<std::string> expected;
    for (const char* key : kLegacyKeys) expected.emplace(key);
    if (fields.size() != expected.size()) {
        error = "旧存档字段数量不正确。";
        return false;
    }
    for (const auto& item : fields) {
        if (expected.find(item.first) == expected.end()) {
            error = "旧存档包含未知字段：" + item.first;
            return false;
        }
    }
    for (const auto& key : expected) {
        if (fields.find(key) == fields.end()) {
            error = "旧存档缺少字段：" + key;
            return false;
        }
    }
    return true;
}

bool deserializeLegacy(std::istream& input, GameState& candidate, std::string& error) {
    Fields fields;
    if (!parseFields(input, fields, error)) return false;
    if (fields.at("format") != "tribe-dawn") {
        error = "不是《燧火纪》旧正式版存档。";
        return false;
    }
    if (fields.at("version") != "1") {
        error = "只支持迁移版本1的旧正式版存档。";
        return false;
    }
    if (fields.at("reserved") != "0") {
        error = "旧存档保留字段不合法。";
        return false;
    }

    GameState parsed;
    int mode = 0;
    int phase = 0;
    int ending = 0;
    const auto parseNumber = [&](const char* key, int& value) {
        if (parseIntStrict(fields.at(key), value)) return true;
        error = std::string("旧存档字段不是合法整数：") + key;
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
        || !parseNumber("current_event", parsed.currentEvent)) {
        return false;
    }

    if (mode < 0 || mode > 1 || phase < 0 || phase > 3 || ending < 0 || ending > 5) {
        error = "旧存档中的枚举编号超出范围。";
        return false;
    }
    parsed.mode = static_cast<GameMode>(mode);
    parsed.phase = static_cast<Phase>(phase);
    parsed.ending = static_cast<Ending>(ending);
    if (!parseBool(fields.at("pending_raid"), parsed.pendingRaid)
        || !parseBool(fields.at("rockfang_truce"), parsed.rockfangTruce)
        || !parseBool(fields.at("rockfang_fort_captured"), parsed.rockfangFortCaptured)) {
        error = "旧存档中的布尔字段不合法。";
        return false;
    }
    if (!parseBoolArray(fields.at("discovered"), parsed.discovered)
        || !parseBoolArray(fields.at("scouted"), parsed.scouted)
        || !parseBoolArray(fields.at("buildings"), parsed.buildings)
        || !parseBoolArray(fields.at("technologies"), parsed.technologies)
        || !parseIntArray(fields.at("relations"), parsed.relations)
        || !parseIntArray(fields.at("quests"), parsed.quests)
        || !parseIntArray(fields.at("event_schedule"), parsed.eventSchedule)) {
        error = "旧存档中的数组长度或内容不合法。";
        return false;
    }
    if (!GameEngine::validateState(parsed, error)) {
        error = "旧存档状态校验失败：" + error;
        return false;
    }

    candidate = std::move(parsed);
    error.clear();
    return true;
}

enum class LoadFileStatus { Loaded, Missing, Invalid, Unavailable };

LoadFileStatus loadLegacyFileReadOnly(
    const std::filesystem::path& path, GameState& candidate, std::string& error) {
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error = "无法检查文件" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (!exists) {
        error = "文件不存在：" + path.filename().string();
        return LoadFileStatus::Missing;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "无法检查旧存档大小：" + path.filename().string() + "：" + code.message();
        return LoadFileStatus::Unavailable;
    }
    if (size > kMaximumLegacySaveBytes) {
        error = "旧存档超过1 MiB安全上限：" + path.filename().string();
        return LoadFileStatus::Invalid;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法只读打开文件：" + path.filename().string();
        return LoadFileStatus::Unavailable;
    }
    return deserializeLegacy(input, candidate, error)
        ? LoadFileStatus::Loaded : LoadFileStatus::Invalid;
}

CampaignEnding convertEnding(const Ending ending) {
    switch (ending) {
    case Ending::None: return CampaignEnding::None;
    case Ending::Alliance: return CampaignEnding::Alliance;
    case Ending::Conquest: return CampaignEnding::Conquest;
    case Ending::Prosperity: return CampaignEnding::Prosperity;
    case Ending::Migration: return CampaignEnding::Migration;
    case Ending::Extinction: return CampaignEnding::Extinction;
    }
    return CampaignEnding::None;
}

std::size_t v2RelationIndex(const FactionId faction) {
    switch (faction) {
    case FactionId::RiverDeer: return indexOf(TribeIdV2::RiverDeer);
    case FactionId::WhiteFeather: return indexOf(TribeIdV2::WhiteFeather);
    case FactionId::Rockfang: return indexOf(TribeIdV2::Rockfang);
    case FactionId::Count: break;
    }
    return indexOf(TribeIdV2::Player);
}

} // namespace

bool CampaignMigration::loadLegacyReadOnly(
    const std::filesystem::path& primaryPath, GameState& candidate, std::string& error) {
    GameState parsed;
    std::string primaryError;
    const LoadFileStatus primaryStatus = loadLegacyFileReadOnly(primaryPath, parsed, primaryError);
    if (primaryStatus == LoadFileStatus::Loaded) {
        candidate = std::move(parsed);
        error.clear();
        return true;
    }
    if (primaryStatus == LoadFileStatus::Unavailable) {
        error = "旧档主文件暂时不可用：" + primaryError
            + "。为避免误读旧备份，本次没有回退；任何旧档文件均未修改。";
        return false;
    }

    std::filesystem::path backup = primaryPath;
    backup += ".bak";
    std::filesystem::path temporary = primaryPath;
    temporary += ".tmp";

    std::string backupError;
    if (loadLegacyFileReadOnly(backup, parsed, backupError) == LoadFileStatus::Loaded) {
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    std::string temporaryError;
    if (loadLegacyFileReadOnly(temporary, parsed, temporaryError) == LoadFileStatus::Loaded) {
        candidate = std::move(parsed);
        error.clear();
        return true;
    }

    error = "旧档迁移读取失败。主文件：" + primaryError
        + "；备份文件：" + backupError + "；临时文件：" + temporaryError
        + "。任何旧档文件均未修改。";
    return false;
}

bool CampaignMigration::convert(
    const GameState& legacy, CampaignState& candidate, std::string& error) {
    std::string legacyError;
    if (!GameEngine::validateState(legacy, legacyError)) {
        error = "旧存档状态校验失败：" + legacyError;
        return false;
    }

    const CampaignMode mode = legacy.mode == GameMode::Quick
        ? CampaignMode::Quick : CampaignMode::Course;
    CampaignState converted = CampaignGame({mode, legacy.seed, "燧火", "炎角", "生存"}).state();

    converted.seed = legacy.seed;
    converted.season = legacy.turn;
    converted.seasonLimit = 16;
    converted.actionsLeft = legacy.actionsLeft;
    converted.population = legacy.population;
    converted.food = legacy.food;
    converted.wood = legacy.wood;
    converted.stone = legacy.stone;
    converted.herbs = legacy.herbs;
    converted.warriors = legacy.warriors;
    converted.morale = legacy.morale;
    converted.campDurability = legacy.campDurability;
    converted.rockfangStrength = legacy.rockfangStrength;
    converted.rockfangFortCaptured = legacy.rockfangFortCaptured;
    std::copy(legacy.discovered.begin(), legacy.discovered.end(), converted.discovered.begin());
    converted.buildings = legacy.buildings;
    converted.technologies = legacy.technologies;

    for (std::size_t oldIndex = 0; oldIndex < kFactionCount; ++oldIndex) {
        const auto faction = static_cast<FactionId>(oldIndex);
        DiplomacyRelation& relation = converted.relations[v2RelationIndex(faction)];
        relation.relation = legacy.relations[oldIndex];
        relation.trust = std::clamp(
            legacy.quests[oldIndex] * 20 + std::max(0, legacy.relations[oldIndex]) / 5, 0, 100);
    }

    DiplomacyRelation& rockfang = converted.relations[indexOf(TribeIdV2::Rockfang)];
    rockfang.atWar = false;
    rockfang.truce = legacy.rockfangTruce;
    rockfang.alliance = false;

    converted.ending = CampaignEnding::None;
    switch (legacy.phase) {
    case Phase::Playing:
        converted.phase = CampaignPhase::Managing;
        break;
    case Phase::AwaitingRaid:
        converted.phase = CampaignPhase::War;
        converted.war.active = true;
        converted.war.enemy = TribeIdV2::Rockfang;
        converted.war.commander = converted.squads.front().captain;
        converted.war.warriors = legacy.warriors;
        converted.war.militia = 0;
        converted.war.playerPower = std::max(
            0, legacy.warriors * 3 + legacy.morale / 10 + legacy.temporaryDefense);
        converted.war.enemyPower = std::max(1, legacy.rockfangStrength);
        converted.war.front = 1;
        converted.war.order = WarOrder::Hold;
        converted.war.riskConfirmed = false;
        rockfang.atWar = true;
        rockfang.truce = false;
        rockfang.alliance = false;
        break;
    case Phase::FinalChoice:
        converted.phase = CampaignPhase::EndingChoice;
        converted.actionsLeft = 0;
        break;
    case Phase::Finished:
        converted.phase = CampaignPhase::Finished;
        converted.ending = convertEnding(legacy.ending);
        converted.actionsLeft = 0;
        if (converted.ending == CampaignEnding::Alliance) {
            converted.relations[indexOf(TribeIdV2::RiverDeer)].alliance = true;
            converted.relations[indexOf(TribeIdV2::WhiteFeather)].alliance = true;
        }
        break;
    }

    std::string migrationDetail =
        "从旧正式版版本1只读迁入；旧存档保持不变，V2沿用资源、建设、科技、地图和三部落关系。";
    if (legacy.temporaryDefense > 0 && legacy.phase != Phase::AwaitingRaid) {
        migrationDetail += "旧版临时防御不属于V2长期状态，未继续保留。";
    }
    converted.chronicle.push_back(
        {legacy.turn, 3, "旧正式版存档升级", std::move(migrationDetail)});

    if (!CampaignGame::validateState(converted, error)) {
        error = "V2迁移结果校验失败：" + error;
        return false;
    }
    candidate = std::move(converted);
    error.clear();
    return true;
}

bool CampaignMigration::loadAndConvertReadOnly(
    const std::filesystem::path& primaryPath, CampaignState& candidate, std::string& error) {
    GameState legacy;
    if (!loadLegacyReadOnly(primaryPath, legacy, error)) return false;
    CampaignState converted;
    if (!convert(legacy, converted, error)) return false;
    candidate = std::move(converted);
    error.clear();
    return true;
}

} // namespace tribe
