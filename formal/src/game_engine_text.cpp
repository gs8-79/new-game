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
const std::array<WorldLocationInfo, kWorldLocationCount>& GameEngine::worldLocations() {
    static const std::array<WorldLocationInfo, kWorldLocationCount> locations{{
        {WorldLocationId::Camp,
         "燧火营地",
         "部落管理、建设与结算",
         LocationRole::Camp,
         {WorldLocationId::Forest, WorldLocationId::RedPlain}},
        {WorldLocationId::Forest,
         "苍林",
         "食物、木材、草药与兽皮",
         LocationRole::Resource,
         {WorldLocationId::Camp, WorldLocationId::Marsh}},
        {WorldLocationId::RedPlain,
         "红土原",
         "食物与兽皮采集，通往渡口和矿场",
         LocationRole::Resource,
         {WorldLocationId::Camp, WorldLocationId::RiverFord, WorldLocationId::Quarry}},
        {WorldLocationId::Marsh,
         "芦苇沼泽",
         "木材与草药采集，连接白羽与海岸",
         LocationRole::Resource,
         {WorldLocationId::Forest, WorldLocationId::WhiteFeatherCamp, WorldLocationId::SaltwindCoast}},
        {WorldLocationId::RiverFord,
         "河鹿渡口",
         "河鹿部落交谈、贸易与商路",
         LocationRole::Diplomacy,
         {WorldLocationId::RedPlain, WorldLocationId::MountainMarket}},
        {WorldLocationId::WhiteFeatherCamp,
         "白羽营地",
         "白羽部落交谈、贸易与联盟",
         LocationRole::Diplomacy,
         {WorldLocationId::Marsh}},
        {WorldLocationId::Quarry,
         "燧石矿场",
         "石料采集，通往玄石谷",
         LocationRole::Resource,
         {WorldLocationId::RedPlain, WorldLocationId::BlackstoneValley}},
        {WorldLocationId::OldPass,
         "古老山隘",
         "连接岩牙要塞的山路",
         LocationRole::Route,
         {WorldLocationId::CliffTradeRoad, WorldLocationId::RockfangFort}},
        {WorldLocationId::RockfangFort,
         "岩牙要塞",
         "岩牙巡逻与征服目标",
         LocationRole::War,
         {WorldLocationId::OldPass}},
        {WorldLocationId::SaltwindCoast,
         "盐风海岸",
         "远程食物采集，通往潮盐港",
         LocationRole::Resource,
         {WorldLocationId::Marsh, WorldLocationId::ShellBeach}},
        {WorldLocationId::TidesaltHarbor,
         "潮盐港",
         "潮盐部落交谈、贸易与商路",
         LocationRole::Diplomacy,
         {WorldLocationId::ShellBeach, WorldLocationId::MountainMarket}},
        {WorldLocationId::ShellBeach,
         "贝壳滩",
         "连接海岸与潮盐港的潮汐通道",
         LocationRole::Route,
         {WorldLocationId::SaltwindCoast, WorldLocationId::TidesaltHarbor}},
        {WorldLocationId::BlackstoneValley,
         "玄石谷",
         "石料采集，通往玄石工坊",
         LocationRole::Resource,
         {WorldLocationId::Quarry, WorldLocationId::BlackstoneWorkshop}},
        {WorldLocationId::BlackstoneWorkshop,
         "玄石工坊",
         "玄石部落交谈、贸易与联盟",
         LocationRole::Diplomacy,
         {WorldLocationId::BlackstoneValley, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::MountainMarket,
         "山前集市",
         "三路交汇，开通商路的交通节点",
         LocationRole::Route,
         {WorldLocationId::RiverFord, WorldLocationId::TidesaltHarbor, WorldLocationId::CliffTradeRoad}},
        {WorldLocationId::CliffTradeRoad,
         "断崖商道",
         "连接集市、玄石与山隘的商路",
         LocationRole::Route,
         {WorldLocationId::BlackstoneWorkshop, WorldLocationId::MountainMarket, WorldLocationId::OldPass}},
    }};
    return locations;
}

std::string GameEngine::statusText() const {
    std::ostringstream output;
    output << "部落战役  模式：" << modeName(state_.mode) << "  季节：" << state_.season << "/" << state_.seasonLimit
           << "  阶段：" << phaseName(state_.phase) << "  行动点：" << state_.actionsLeft << "\n"
           << "部落：" << state_.tribeName << "  首领：" << state_.leaderName;
    if (!state_.actingLeaderName.empty()) output << "（代理/继任：" << state_.actingLeaderName << "）";
    output << "  稳定：" << state_.stability << "  士气：" << state_.morale << "\n"
           << "人口：" << state_.population << "  食物：" << state_.food << "  木材：" << state_.wood << "  石料："
           << state_.stone << "  草药：" << state_.herbs << "  兽皮：" << state_.hides << "  战士：" << state_.warriors
           << "\n"
           << "营地耐久：" << state_.campDurability << "  贸易次数：" << state_.tradeCount << "\n"
           << "统一人口池：已占用" << population_rules::committedPopulation(state_) << '/'
           << population_rules::populationCapacity(state_)
           << "（劳力、前哨、驻军、军队、出任务小队；首领与基础留守2人不分配）";
    if (state_.workforceReassignmentRequired) output << " [劳力待重分配]";
    output << "\n"
           << "建筑：" << countTrue(state_.buildings) << "/6  技术：" << countTrue(state_.technologies)
           << "/9  已发现地点：" << countTrue(state_.discovered) << "/16  战争胜负：" << state_.warsWon << "/"
           << state_.warsLost;
    return output.str();
}

std::string GameEngine::workforceText() const {
    const WorkforceState& w = state_.workforce;
    std::ostringstream out;
    out << "劳力分工（资源只能由地图任务带回；所有岗位均占用统一人口池）\n"
        << "食物队" << w.foodCrew << " 木材队" << w.woodCrew << " 石料队" << w.stoneCrew << " 草药队" << w.herbCrew
        << "\n支持岗位（0未配置/1已配置）：工匠" << (w.crafters > 0 ? 1 : 0) << " 医者" << (w.healers > 0 ? 1 : 0)
        << " 侦察" << (w.scouts > 0 ? 1 : 0) << " 使者" << (w.envoys > 0 ? 1 : 0) << " 营地守卫"
        << (w.campGuards > 0 ? 1 : 0) << "\n前哨守卫：";
    bool hasOutpostGuard = false;
    for (std::size_t index = 1; index < kWorldLocationCount; ++index) {
        if (!state_.outposts[index]) continue;
        out << worldLocations()[index].name << w.outpostGuards[index] << ' ';
        hasOutpostGuard = true;
    }
    if (!hasOutpostGuard) out << "无";
    out << "\n人口占用：" << population_rules::committedPopulation(state_) << '/'
        << population_rules::populationCapacity(state_) << "；当前/下季行动容量：" << state_.actionsLeft << '/'
        << availableTeams(state_) << "（基础3，每支2至6人的资源队+1，最高7）"
        << "\n资源队可分配2至6人；支持岗位与前哨守卫只分配0或1人。"
        << "\n已激活效果：";
    bool hasSupportEffect = false;
    if (w.crafters > 0) {
        out << (state_.buildings[indexOf(BuildingId::Workshop)] ? "工坊可制造、维修" : "工匠等待武备工坊");
        hasSupportEffect = true;
    }
    if (w.healers > 0) {
        out << (hasSupportEffect ? "；" : "")
            << (state_.buildings[indexOf(BuildingId::HealerHut)] ? "医者维护疾病防护并可治疗" : "医者等待医者小屋");
        hasSupportEffect = true;
    }
    if (w.scouts > 0) {
        out << (hasSupportEffect ? "；" : "") << "侦察降低袭扰与野兽风险";
        hasSupportEffect = true;
    }
    if (w.envoys > 0) {
        out << (hasSupportEffect ? "；" : "") << "使者维持外交";
        if (state_.buildings[indexOf(BuildingId::CouncilFire)]) out << "、主持议事火坛";
        hasSupportEffect = true;
    }
    if (w.campGuards > 0) {
        out << (hasSupportEffect ? "；" : "") << "营地守卫抵御袭扰";
        hasSupportEffect = true;
    }
    if (!hasSupportEffect) out << "尚未激活支持岗位";
    out << "\n负责人：工坊"
        << (state_.workshopSupervisor.empty()
                ? "未任命"
                : state_.workshopSupervisor + "（工艺等级" + std::to_string(craftSupervisorRank(state_)) + "）" +
                      (w.crafters == 0 ? "[未配置劳力，停工]" : ""))
        << "；医者"
        << (state_.healerSupervisor.empty()
                ? "未任命"
                : state_.healerSupervisor + "（医疗等级" + std::to_string(medicineSupervisorRank(state_)) + "）" +
                      (w.healers == 0 ? "[未配置劳力，停工]" : ""));
    if (state_.workforceReassignmentRequired)
        out << "\n[劳力待重分配] 仅可降低劳力或驻军，或解散军队，直到人口占用恢复合法。";
    out << "\n用法：assign <岗位> <人数>；assign outpost <地点> <0|1>。";
    return out.str();
}

std::string GameEngine::inventoryText() const {
    std::ostringstream out;
    out << "共享装备仓库（" << state_.stockpile.size() << "件）：\n";
    for (const Item& item : state_.stockpile) {
        out << item.id << "  " << item.name << "  状态"
            << (item.condition == ItemCondition::Intact    ? "完好"
                : item.condition == ItemCondition::Damaged ? "损坏"
                                                           : "报废")
            << "  品质" << itemQualityName(item.quality) << "  实际属性";
        bool hasBonus = false;
        const int divisor = item.condition == ItemCondition::Damaged ? 2 : 1;
        const int quality = itemQualityTier(item.quality);
        for (std::size_t index = 0; index < kAttributeCount; ++index) {
            const int base = item.bonuses.values[index];
            const int effective = (base > 0 ? base + quality : base) / divisor;
            if (effective == 0) continue;
            out << (hasBonus ? "," : "") << "属性" << (index + 1U) << (effective > 0 ? "+" : "") << effective;
            hasBonus = true;
        }
        if (!hasBonus) out << "无";
        out << '\n';
    }
    if (state_.stockpile.empty()) out << "（空）\n";
    return out.str();
}

std::string GameEngine::peopleText() const {
    std::ostringstream out;
    out << "人物档案：工坊负责人"
        << (state_.workshopSupervisor.empty()
                ? "未任命"
                : state_.workshopSupervisor + "（工艺等级" + std::to_string(craftSupervisorRank(state_)) +
                      (state_.workforce.crafters == 0 ? "，停工）" : "）"))
        << "；医者负责人"
        << (state_.healerSupervisor.empty()
                ? "未任命"
                : state_.healerSupervisor + "（医疗等级" + std::to_string(medicineSupervisorRank(state_)) +
                      (state_.workforce.healers == 0 ? "，停工）" : "）"))
        << "\n";
    for (const Character& character : state_.roster)
        out << character.name << " 等级" << character.level << " 生命" << character.life << " 疲劳" << character.fatigue
            << " 忠诚" << character.loyalty << " 职业" << occupationName(character.occupation)
            << (character.name == state_.workshopSupervisor ? " [工坊负责人]" : "")
            << (character.name == state_.healerSupervisor ? " [医者负责人]" : "") << '\n';
    return out.str();
}

std::string GameEngine::personText(const std::string_view name) const {
    const Character* person = findRosterCharacter(state_.roster, name);
    if (person == nullptr) return "没有这个人物。";
    std::ostringstream out;
    out << person->name << " 职业" << occupationName(person->occupation) << " 等级" << person->level << " 经验"
        << person->experience << " 生命" << person->life << " 疲劳" << person->fatigue << " 忠诚" << person->loyalty
        << "\n八项属性：";
    for (int value : person->attributes.values) out << ' ' << value;
    out << "\n装备：";
    bool any = false;
    for (std::size_t i = 0; i < person->equipment.size(); ++i) {
        const auto& equipped = person->equipment[i];
        if (!equipped.has_value()) continue;
        const Item& item = equipped.value();
        out << equipmentSlotName(static_cast<EquipmentSlot>(i)) << ':' << item.name << ' ';
        any = true;
    }
    if (!any) out << "无";
    return out.str();
}

std::string GameEngine::buildingsText() const {
    return "建筑清单（木材/石料/行动/维护/收益）\n粮仓 8/2/1/无/降低粮食风险\n木墙 10/2/1/营地守卫/提高防御\n武备工坊 "
           "8/6/1/工匠/制造、维修、三阶技术；工匠负责人决定装备品质\n医者小屋 "
           "6/2/1/医者/医者维护疾病防护、草药医治；医者负责人强化治疗与休整\n瞭望塔 "
           "8/4/1/侦察/预警，袭扰与野兽伤害-1\n议事火坛 6/4/1/使者/派系满意流失-2";
}

std::string GameEngine::technologiesText() const {
    return "技术清单（食物/木材/前置/效果）\n食物保存 3/2/无/食物任务增益\n草药知识 "
           "3/2/无/草药任务增益、披风与护符\n引水耕作 "
           "3/2/无/食物任务增益\n燧石长矛、盾墙阵形、伏击训练需武备工坊及前序技术。";
}

std::string GameEngine::warTargetsText() const {
    std::ostringstream out;
    out << "战争目标（据点/占领/驻军/动乱）：\n";
    for (std::size_t i = 1; i < kTribeCount; ++i) {
        const OccupationState& site = state_.occupations[i];
        out << tribeName(static_cast<TribeId>(i)) << " / "
            << worldLocations()[indexOf(contactLocation(static_cast<TribeId>(i)))].name << " / "
            << (site.occupied ? "已占领" : "未占领") << " / " << site.garrison << " / " << site.unrest
            << "（需2至4驻军）\n";
    }
    return out.str();
}

std::string GameEngine::powerText() const {
    const WarState& war = state_.war;
    std::ostringstream out;
    out << "军队战力：正式战士" << war.warriors << " 民兵" << war.militia << " 长矛" << war.spearMilitia << " 盾兵"
        << war.shieldBearers << " 重装长矛" << war.heavySpears << "\n锁定装备" << war.lockedEquipment.size()
        << "件，工坊负责人" << (state_.workshopSupervisor.empty() ? "未任命" : state_.workshopSupervisor)
        << "，品质战力+" << war.craftsmanshipPower
        << "（品质总和最多+4）；攻击/防御基础=正式战士×2+民兵+士气÷10，长矛+2、盾+1、重装组合再+2；当前战力"
        << war.playerPower;
    return out.str();
}

std::string GameEngine::worldText() const {
    std::ostringstream output;
    output << "十六地点世界地图（小队沿相邻道路探索）：\n";
    for (std::size_t index = 0; index < kWorldLocationCount; ++index) {
        const auto& location = worldLocations()[index];
        output << (index + 1) << ". " << (state_.discovered[index] ? location.name : "????")
               << (state_.discovered[index] ? " [" + locationRoleName(location.role) + "] — " + location.feature : "")
               << (state_.outposts[index] ? " [结算点]" : "") << '\n';
    }
    return output.str();
}

std::string GameEngine::diplomacyText() const {
    std::ostringstream output;
    output << "六部落外交（关系/信任/恐惧/贸易依赖）：\n";
    for (std::size_t index = 1; index < kTribeCount; ++index) {
        const auto& profile = state_.tribes[index];
        const auto& relation = state_.relations[index];
        const TribeId tribe = static_cast<TribeId>(index);
        output << profile.name << "  " << relation.relation << '/' << relation.trust << '/' << relation.fear << '/'
               << relation.tradeDependence;
        if (relation.atWar) output << " [战争]";
        if (relation.truce) output << " [停战]";
        if (relation.alliance) output << " [联盟]";
        if (relation.marriage) output << " [联姻]";
        if (relation.playerPaysTribute) output << " [我方朝贡]";
        if (relation.otherPaysTribute) output << " [对方进贡]";
        if (relation.tradeRoute) output << " [固定商路]";
        if (!locationDiscovered(state_, contactLocation(tribe))) {
            output << " [尚未充分接触]";
        } else {
            output << "  首领" << profile.leader << " 性格：" << profile.personality;
            const FactionState& faction = dominantFaction(profile);
            output << "  主导派系：" << faction.name;
            if (knowsFactionDemand(relation))
                output << " 诉求：" << faction.demand;
            else
                output << " [诉求待查]";
            if (knowsFullFactionNetwork(relation) && profile.factions.size() > 1U) {
                output << "  其他派系：";
                bool first = true;
                for (const FactionState& other : profile.factions) {
                    if (&other == &faction) continue;
                    if (!first) output << "、";
                    output << other.name << "（" << other.demand << "）";
                    first = false;
                }
            }
        }
        output << '\n';
    }
    return output.str();
}

std::string GameEngine::factionText() const {
    std::ostringstream output;
    output << "内部稳定：" << state_.stability << "\n";
    for (std::size_t index = 0; index < kPlayerFactionCount; ++index) {
        const auto& faction = state_.playerFactions[index];
        output << (index + 1) << ". " << faction.name << " 影响" << faction.influence << " 满意" << faction.satisfaction
               << " 危机：" << crisisName(faction.crisis) << " 诉求：" << faction.demand << " 候选："
               << faction.candidate << '\n';
    }
    return output.str();
}

std::string GameEngine::squadText() const {
    std::ostringstream output;
    output << "具名人物：" << state_.roster.size() << " 最高等级：" << state_.highestLevel << "\n";
    for (const PermanentSquad& squad : state_.squads) {
        output << squad.name << " 队长" << squad.captain << " 人数" << squad.members.size() << " 疲劳" << squad.fatigue
               << " 精锐经验" << squad.eliteExperience << " 驻地" << worldLocations()[indexOf(squad.station)].name
               << (squad.refusingOrders ? " [抗命]" : "") << '\n';
    }
    return output.str();
}

std::string GameEngine::objectiveText() const {
    const auto endings = availableEndings();
    std::ostringstream output;
    output << "当前已满足道路：";
    for (const GameEnding ending : endings) output << endingName(ending) << ' ';
    output << "\n联盟：河鹿/白羽关系70、至少2个联盟、部落联盟技术。"
           << "\n征服：占领任意两个外部据点、战士5、士气55。"
           << "\n繁荣：人口20、食物40、建筑4、技术4。"
           << "\n迁徙：只要部落仍存活即可选择。";
    return output.str();
}

std::string GameEngine::chronicleText() const {
    std::ostringstream output;
    const std::size_t start = state_.chronicle.size() > 12U ? state_.chronicle.size() - 12U : 0U;
    for (std::size_t index = start; index < state_.chronicle.size(); ++index) {
        const auto& entry = state_.chronicle[index];
        output << "第" << entry.season << "季 [" << entry.importance << "] " << entry.title << "：" << entry.detail
               << '\n';
    }
    return output.str();
}

std::string GameEngine::helpText() const {
    return "查询：1/status状态 2/map地图 3/workforce劳力 4/inventory仓库 6/diplomacy外交 factions派系 squads小队 "
           "objectives目标 chronicle编年史\n"
           "经营：build/建造 <建筑>，research/研究 <技术>；资源和地点只能通过地图任务取得\n"
           "任务：5 或 mission；任务内使用move/移动、gather/采集、build outpost/建造前哨、settle/结算\n"
           "劳力：资源队可分配2至6人；工匠、医者、侦察、使者、守卫和前哨守卫只分配0或1人；所有岗位、驻军、军队和出任务"
           "小队共用人口-2\n"
           "小队：squadrest 小队休整；appoint/unappoint <workshop|healer> 任免负责人；负责人不可加入小队\n"
           "外交：talk gift trade <部落> <给出资源> <换取资源> openroute marry tribute demand ally declare truce raid\n"
           "内政：appease <1至3>；战争：formarmy <战士> <民兵>，disbandarmy 解散军队，war "
           "<部落>，战中attack/defend/order/retreat\n"
           "季节：8/endturn；结局：choose <alliance|conquest|prosperity|migration>；长期结局后sandbox。";
}

EndingSummary GameEngine::endingSummary() const {
    EndingSummary summary;
    summary.ending = state_.ending;
    summary.title = endingName(state_.ending);
    switch (state_.ending) {
        case GameEnding::Alliance:
            summary.epilogue = "诸部落的旗帜围绕共同火坛，争执仍在，但道路第一次由议事而非刀锋决定。";
            break;
        case GameEnding::Conquest:
            summary.epilogue = "红金战旗升上岩牙要塞，胜利带来疆土，也要求后人承担统治的代价。";
            break;
        case GameEnding::Prosperity:
            summary.epilogue = "粮仓、武备工坊与炊烟连成新的聚落，燧火从求生之火变成文明之火。";
            break;
        case GameEnding::Migration:
            summary.epilogue = "队伍越过山隘，把旧火种带往晨光中的新土地。";
            break;
        case GameEnding::Extinction:
            summary.epilogue = "营墙倒塌，火坛变暗；留下的故事提醒后来者饥饿、战争与分裂的代价。";
            break;
        case GameEnding::None:
            summary.epilogue = "战役尚未结束。";
            break;
    }
    summary.statistics = {
        "生存季节：" + std::to_string(state_.season),
        "人口/食物/稳定：" + std::to_string(state_.population) + "/" + std::to_string(state_.food) + "/" +
            std::to_string(state_.stability),
        "建筑/技术：" + std::to_string(countTrue(state_.buildings)) + "/" +
            std::to_string(countTrue(state_.technologies)),
        "小队任务/阵亡：" + std::to_string(state_.missionCount) + "/" + std::to_string(state_.missionDeaths),
        "战争胜负：" + std::to_string(state_.warsWon) + "/" + std::to_string(state_.warsLost),
        "贸易次数：" + std::to_string(state_.tradeCount),
        "最终首领：" + state_.leaderName + "，历任记录" + std::to_string(state_.leadershipHistory.size()) + "条",
    };
    for (const GameEnding ending : availableEndings()) {
        if (ending != state_.ending) summary.otherRoads.push_back(endingName(ending));
    }
    std::vector<ChronicleEntry> sorted = state_.chronicle;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ChronicleEntry& left, const ChronicleEntry& right) {
        return left.importance > right.importance;
    });
    if (sorted.size() > 10U) sorted.resize(10U);
    summary.importantChronicle = std::move(sorted);
    return summary;
}

std::string GameEngine::modeName(const GameMode mode) {
    switch (mode) {
        case GameMode::Quick:
            return "快速游戏（8季）";
        case GameMode::Standard:
            return "正式游戏（16季）";
        case GameMode::Long:
            return "长期游戏（32季）";
    }
    return "未知模式";
}

std::string GameEngine::phaseName(const GamePhase phase) {
    switch (phase) {
        case GamePhase::Managing:
            return "部落管理";
        case GamePhase::Mission:
            return "可操控小队任务";
        case GamePhase::War:
            return "可操控部落战争";
        case GamePhase::EndingChoice:
            return "时代结算选择";
        case GamePhase::Finished:
            return "独立结局结算";
        case GamePhase::Sandbox:
            return "结局后沙盒";
    }
    return "未知阶段";
}

std::string GameEngine::endingName(const GameEnding ending) {
    switch (ending) {
        case GameEnding::None:
            return "尚未结算";
        case GameEnding::Alliance:
            return "联盟共主";
        case GameEnding::Conquest:
            return "山河征服者";
        case GameEnding::Prosperity:
            return "燧火繁荣";
        case GameEnding::Migration:
            return "迁徙新生";
        case GameEnding::Extinction:
            return "部落覆灭";
    }
    return "未知结局";
}

std::string GameEngine::tribeName(const TribeId tribe) {
    switch (tribe) {
        case TribeId::Player:
            return "玩家部落";
        case TribeId::RiverDeer:
            return "河鹿";
        case TribeId::WhiteFeather:
            return "白羽";
        case TribeId::Rockfang:
            return "岩牙";
        case TribeId::Tidesalt:
            return "潮盐";
        case TribeId::Blackstone:
            return "玄石";
        case TribeId::Count:
            break;
    }
    return "未知部落";
}

std::string GameEngine::resourceName(const ResourceKind resource) {
    switch (resource) {
        case ResourceKind::Food:
            return "食物";
        case ResourceKind::Wood:
            return "木材";
        case ResourceKind::Stone:
            return "石料";
        case ResourceKind::Herbs:
            return "草药";
        case ResourceKind::Hides:
            return "兽皮";
    }
    return "未知资源";
}

} // namespace tribe
