#include "game_command_catalog.hpp"

#include "command_parser.hpp"

namespace tribe::game_command_catalog {
namespace {

using command_parser::equalsAny;

} // namespace

CommandId classify(const command_parser::Command& command) {
    const auto verb = command.verb;
    if (equalsAny(verb, {"status", "状态", "1"})) return CommandId::Status;
    if (equalsAny(verb, {"look", "查看"})) return CommandId::Look;
    if (equalsAny(verb, {"map", "地图", "2"})) return CommandId::Map;
    if (equalsAny(verb, {"diplomacy", "外交", "6"})) return CommandId::Diplomacy;
    if (equalsAny(verb, {"factions", "派系", "稳定"})) return CommandId::Factions;
    if (equalsAny(verb, {"squad", "小队配置"})) return CommandId::Squad;
    if (equalsAny(verb, {"squads", "小队", "7"})) return CommandId::Squads;
    if (equalsAny(verb, {"objectives", "目标"})) return CommandId::Objectives;
    if (equalsAny(verb, {"chronicle", "编年史"})) return CommandId::Chronicle;
    if (equalsAny(verb, {"help", "帮助", "9"})) return CommandId::Help;
    if (equalsAny(verb, {"sandbox", "继续沙盒"})) return CommandId::Sandbox;
    if (equalsAny(verb, {"choose", "选择"})) return CommandId::Choose;
    if (equalsAny(verb, {"assign", "分配"})) return CommandId::Assign;
    if (equalsAny(verb, {"garrison", "驻军"})) return CommandId::Garrison;
    if (equalsAny(verb, {"disbandarmy", "解散军队"})) return CommandId::DisbandArmy;
    if (equalsAny(verb, {"event", "事件"})) return CommandId::Event;
    if (equalsAny(verb, {"build", "建造"})) return CommandId::Build;
    if (equalsAny(verb, {"buildoutpost", "outpost", "建造前哨"})) return CommandId::BuildOutpost;
    if (equalsAny(verb, {"research", "研究"})) return CommandId::Research;
    if (equalsAny(verb, {"mission", "出任务", "5"})) return CommandId::Mission;
    if (equalsAny(verb, {"workforce", "劳力", "3"})) return CommandId::Workforce;
    if (equalsAny(verb, {"inventory", "仓库", "4"})) return CommandId::Inventory;
    if (verb == "people") return CommandId::People;
    if (equalsAny(verb, {"person", "人物"})) return CommandId::Person;
    if (equalsAny(verb, {"buildings", "建筑清单"})) return CommandId::Buildings;
    if (equalsAny(verb, {"technologies", "技术清单"})) return CommandId::Technologies;
    if (equalsAny(verb, {"craft", "制造"})) return CommandId::Craft;
    if (equalsAny(verb, {"repair", "维修"})) return CommandId::Repair;
    if (equalsAny(verb, {"scrap", "报废"})) return CommandId::Scrap;
    if (equalsAny(verb, {"equip", "装备"})) return CommandId::Equip;
    if (equalsAny(verb, {"unequip", "卸下"})) return CommandId::Unequip;
    if (equalsAny(verb, {"appoint", "任命"})) return CommandId::Appoint;
    if (equalsAny(verb, {"unappoint", "卸任"})) return CommandId::Unappoint;
    if (equalsAny(verb, {"treat", "医治"})) return CommandId::Treat;
    if (equalsAny(verb, {"war", "出征"})) return CommandId::War;
    if (verb == "战争") return CommandId::WarTargets;
    if (equalsAny(verb, {"power", "战力"})) return CommandId::Power;
    if (equalsAny(verb, {"squadrest", "小队休整"})) return CommandId::SquadRest;
    if (equalsAny(verb, {"talk", "交谈"})) return CommandId::Talk;
    if (equalsAny(verb, {"gift", "送礼"})) return CommandId::Gift;
    if (equalsAny(verb, {"trade", "贸易"})) return CommandId::Trade;
    if (equalsAny(verb, {"openroute", "开通商路"})) return CommandId::OpenRoute;
    if (equalsAny(verb, {"marry", "联姻"})) return CommandId::Marry;
    if (equalsAny(verb, {"tribute", "朝贡"})) return CommandId::Tribute;
    if (equalsAny(verb, {"demand", "索贡"})) return CommandId::Demand;
    if (equalsAny(verb, {"ally", "结盟"})) return CommandId::Ally;
    if (equalsAny(verb, {"declare", "宣战"})) return CommandId::Declare;
    if (equalsAny(verb, {"truce", "停战"})) return CommandId::Truce;
    if (equalsAny(verb, {"raid", "劫掠"})) return CommandId::Raid;
    if (equalsAny(verb, {"appease", "安抚"})) return CommandId::Appease;
    if (equalsAny(verb, {"formarmy", "组建军队"})) return CommandId::FormArmy;
    if (equalsAny(verb, {"endturn", "end", "结束回合", "8"})) return CommandId::EndTurn;
    if (equalsAny(verb, {"abort", "放弃任务"})) return CommandId::Abort;
    if (equalsAny(verb, {"order", "下令"})) return CommandId::Order;
    if (equalsAny(verb, {"attack", "攻击"})) return CommandId::Attack;
    if (equalsAny(verb, {"defend", "防御"})) return CommandId::Defend;
    if (equalsAny(verb, {"retreat", "撤退"})) return CommandId::Retreat;
    if (equalsAny(verb, {"move", "移动"})) return CommandId::Move;
    if (equalsAny(verb, {"gather", "采集"})) return CommandId::Gather;
    if (equalsAny(verb, {"use", "使用"})) return CommandId::Use;
    if (equalsAny(verb, {"settle", "结算"})) return CommandId::Settle;
    return CommandId::Unknown;
}

bool hasArity(const command_parser::Command& command, const std::size_t expected) {
    return command.args.size() == expected;
}

} // namespace tribe::game_command_catalog
