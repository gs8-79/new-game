// 成员3交付工具：用真实游戏引擎跑完《02-十六地点地图与试玩路线.md》里登记的完整试玩路线，
// 并把每一步的真实回执、地图页面与最终状态打印成可粘贴进交付文档的实录。
//
// 用法：先配置并构建（可选目标，默认关闭）：
//         cmake -S . -B out/<构建目录> -DTRIBE_BUILD_DELIVERY_TOOLS=ON
//         cmake --build out/<构建目录> --parallel
//       然后：out/<构建目录>/tribe-map-playthrough > 交付实录.txt
// 退出码：0 表示整条路线全部成功；1 表示有命令被拒绝（路线或规则被改坏）。
//
// 说明：
//   - 本工具不复制任何游戏规则，只按命令表驱动 GameEngine，因此实录与玩家真实操作完全一致；
//   - 路线表来自 tools/delivery_route.hpp，与 tests/map_mission_delivery_tests.cpp 共用同一份数据。

#include "tribe/console_ui.hpp"
#include "tribe/expansion_game.hpp"
#include "tribe/game_engine.hpp"

#include "delivery_route.hpp"
#include "world_map_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using tribe::GameEngine;
using tribe::GameMode;
using tribe::GamePhase;
using tribe::GameState;
using tribe::WorldLocationId;

int failures = 0;
int stepNumber = 0;

/// 用途：打印分节标题。输入：标题。输出：写入标准输出；无状态修改。
void printSection(const std::string& title) {
    std::cout << "\n" << std::string(78, '=') << "\n" << title << "\n" << std::string(78, '=') << "\n";
}

/// 用途：把当前游戏状态压缩成一行关键指标，便于在实录中对比每步前后的变化。
std::string stateLine(const GameState& state) {
    std::ostringstream line;
    line << "    [季节" << state.season << " 行动点" << state.actionsLeft << " 阶段" << static_cast<int>(state.phase)
         << " 食物" << state.food << " 木材" << state.wood << " 石料" << state.stone << " 草药" << state.herbs
         << " 兽皮" << state.hides;
    if (state.activeMission) {
        const tribe::ExpansionState& mission = *state.activeMission;
        line << " | 任务回合" << mission.turn << " 位置" << (mission.worldLocation + 1) << " 载货"
             << mission.cargoFood + mission.cargoWood + mission.cargoStone + mission.cargoHerbs + mission.cargoHides
             << "/" << mission.cargoCapacity << " 采集" << mission.harvestActions << "/4";
        if (mission.encounterLife > 0) line << " 遭遇生命" << mission.encounterLife;
        if (mission.encounterDefeated) line << " 巡逻已击退";
    }
    line << "]";
    return line.str();
}

/// 用途：执行一条命令并打印真实回执。输入：引擎、命令和步骤说明。
/// 输出：是否成功。失败：被拒绝时把失败计入总失败数，并打印原因。
bool runCommand(GameEngine& game, const std::string& command, const std::string& note) {
    ++stepNumber;
    const tribe::ActionResult result = game.execute(command);
    std::cout << "\n[" << stepNumber << "] > " << command << "\n";
    if (!note.empty()) std::cout << "    说明：" << note << "\n";
    if (!result.recognized) {
        ++failures;
        std::cout << "    ✗ 命令未被识别（路线表与命令目录不一致）\n";
        return false;
    }
    std::cout << "    " << (result.success ? "✓ " : "✗ ") << result.message << "\n";
    if (!result.success) ++failures;
    std::cout << stateLine(game.state()) << "\n";
    return result.success;
}

/// 用途：反复攻击直到岩牙巡逻被击退，并记录每次攻击后的敌军生命。
bool runAssault(GameEngine& game, const std::string& note) {
    std::cout << "\n[击退遭遇] 反复 attack，直到岩牙巡逻被击退（安全上限 " << delivery::kMaximumAssaultAttempts
              << " 次）\n";
    if (!note.empty()) std::cout << "    说明：" << note << "\n";
    for (int attempt = 1; attempt <= delivery::kMaximumAssaultAttempts; ++attempt) {
        ++stepNumber;
        const tribe::ActionResult result = game.execute("attack");
        std::cout << "\n[" << stepNumber << "] > attack（第" << attempt << "次）\n";
        std::cout << "    " << (result.success ? "✓ " : "✗ ") << result.message << "\n";
        if (!result.success) {
            ++failures;
            std::cout << stateLine(game.state()) << "\n";
            return false;
        }
        std::cout << stateLine(game.state()) << "\n";
        if (game.state().activeMission && game.state().activeMission->encounterDefeated) return true;
    }
    ++failures;
    std::cout << "    ✗ 攻击次数达到安全上限，巡逻仍未击退\n";
    return false;
}

/// 用途：逐步执行一份路线表。输入：引擎、路线和阶段标题。输出：无；失败计入总失败数。
void runRoute(GameEngine& game, const std::vector<delivery::RouteStep>& route, const std::string& title) {
    printSection(title);
    for (const delivery::RouteStep& step : route) {
        if (step.kind == delivery::RouteStepKind::Command)
            runCommand(game, step.command, step.note);
        else
            runAssault(game, step.note);
    }
}

/// 用途：渲染真实的终端地图页面（含 5.2 新增的结算点星标与路线提示），作为交付文档的界面证据。
void printMapPage(const GameEngine& game, const std::string& caption) {
    std::ostringstream page;
    tribe::ConsoleUI ui{page, false, false, 80U};
    ui.renderGame(game, caption);
    std::cout << "\n" << page.str();
}

/// 用途：打印最终结算摘要。输入：引擎与开始时的库存。输出：写入标准输出；无状态修改。
void printSummary(const GameEngine& game, const int woodAtStart, const int stoneAtStart) {
    const GameState& state = game.state();
    printSection("最终摘要");
    std::cout << "任务完成次数：" << state.missionCount << "  已发现地点："
              << std::count(state.discovered.begin(), state.discovered.end(), true) << "/16  前哨："
              << std::count(state.outposts.begin(), state.outposts.end(), true) - 1 << "（不含营地）\n";
    std::cout << "起点库存：木材" << woodAtStart << " 石料" << stoneAtStart << "\n";
    std::cout << "终点库存：食物" << state.food << " 木材" << state.wood << " 石料" << state.stone << " 草药"
              << state.herbs << " 兽皮" << state.hides << "\n";
    std::cout << "小队驻地：" << game.squadText();
    std::cout << "库存物品：" << game.inventoryText();
    std::cout << "\n命令总数：" << stepNumber << "  失败数：" << failures << "\n";
    if (failures == 0)
        std::cout << "结论：交付试玩路线在真实引擎上全部成功，采集、移动、遭遇、前哨建设与结算均已验证。\n";
}

/// 用途：把交付路线表输出为 Markdown 命令表，供交付文档直接粘贴，保证文档与代码永远一致。
/// 输入：无。输出：写入标准输出；无状态修改。
void printRouteMarkdown() {
    const auto dump = [](const std::string& title, const std::vector<delivery::RouteStep>& route) {
        std::cout << "### " << title << "\n\n| 序号 | 命令 | 该步验证的规则 |\n| --- | --- | --- |\n";
        int index = 0;
        for (const delivery::RouteStep& step : route) {
            ++index;
            const std::string command = step.kind == delivery::RouteStepKind::AttackUntilDefeated
                                            ? "`attack`（重复至击退，上限 " +
                                                  std::to_string(delivery::kMaximumAssaultAttempts) + " 次）"
                                            : "`" + step.command + "`";
            std::cout << "| " << index << " | " << command << " | " << step.note << " |\n";
        }
        std::cout << "\n";
    };
    std::cout << "# 交付路线命令表（由 tribe-map-playthrough --route-markdown 生成）\n\n";
    dump("第一段：木材采集任务（前置：`assign wood 2` 后输入 `mission wood`）", delivery::gatherMissionRoute());
    dump("第二段：前哨建设任务（前置：`assign wood 2`、`assign stone 2` 后输入 `mission outpost`）",
         delivery::outpostMissionRoute());
}

// ---------------------------------------------------------------- Mermaid 图示生成
//
// 为什么要让工具生成图，而不是手画：
//   手画的图会随地图或路线调整而过期，读者无法判断哪一份是最新的。
//   这里所有节点、道路、风险颜色都直接读 world_map_catalog，所有移动序列都读 delivery_route.hpp，
//   因此“图示 = 代码”；一旦路线被改坏（例如移动到不相邻的地点），生成会失败并返回非 0。

/// 用途：资源枚举转中文名。输入：资源枚举。输出：中文名；未知枚举返回“未知”。
const char* resourceText(const tribe::ResourceKind resource) {
    switch (resource) {
        case tribe::ResourceKind::Food:
            return "食物";
        case tribe::ResourceKind::Wood:
            return "木材";
        case tribe::ResourceKind::Stone:
            return "石料";
        case tribe::ResourceKind::Herbs:
            return "草药";
        case tribe::ResourceKind::Hides:
            return "兽皮";
    }
    return "未知";
}

/// 用途：生成某个地点的图示说明（风险、可采资源、结算点、死胡同）。
/// 输入：地点下标。输出：Mermaid 标签文本；无状态修改。
std::string mapNodeDetail(const std::size_t index) {
    const auto location = static_cast<WorldLocationId>(index);
    const tribe::world_map::LocationProfile& profile = tribe::world_map::profile(location);
    std::string detail = "风险" + std::to_string(profile.risk) + " " + std::string(profile.riskName);
    std::string resources;
    for (int raw = 0; raw <= static_cast<int>(tribe::ResourceKind::Hides); ++raw) {
        const auto kind = static_cast<tribe::ResourceKind>(raw);
        if (!tribe::world_map::supportsResource(location, kind)) continue;
        if (!resources.empty()) resources += "/";
        resources += resourceText(kind);
    }
    if (!resources.empty()) detail += "<br/>可采：" + resources;
    if (index == 0U) detail += "<br/>初始结算点 ★";
    if (tribe::world_map::locations()[index].neighbors.size() == 1U) detail += "<br/>【死胡同】";
    return detail;
}

/// 用途：打印 Mermaid 拓扑图（节点与道路全部来自世界地图目录）。
/// 输入：小节标题。输出：Markdown 代码块与一行统计；无状态修改。
/// 不变量：邻接表是双向的，因此只画“目标编号大于起点编号”的边，恰好得到 17 条去重后的道路。
void printMapMermaid(const std::string& title) {
    const auto& locations = tribe::world_map::locations();
    std::cout << "### " << title << "\n\n```mermaid\nflowchart LR\n";
    for (std::size_t index = 0U; index < locations.size(); ++index) {
        std::cout << "    M" << (index + 1U) << "[\"" << (index + 1U) << " " << locations[index].name << "<br/>"
                  << mapNodeDetail(index) << "\"]\n";
    }
    std::size_t roads = 0U;
    for (std::size_t index = 0U; index < locations.size(); ++index) {
        for (const WorldLocationId neighbor : locations[index].neighbors) {
            const std::size_t target = tribe::indexOf(neighbor);
            if (target <= index) continue;
            std::cout << "    M" << (index + 1U) << " --- M" << (target + 1U) << "\n";
            ++roads;
        }
    }
    for (std::size_t index = 0U; index < locations.size(); ++index) {
        std::cout << "    class M" << (index + 1U) << " risk"
                  << tribe::world_map::profile(static_cast<WorldLocationId>(index)).risk << "\n";
    }
    std::cout << "    classDef risk0 fill:#d7f0d7,stroke:#2f6b2f,color:#111111\n"
                 "    classDef risk1 fill:#eef7d9,stroke:#5d7a2f,color:#111111\n"
                 "    classDef risk2 fill:#fde3bd,stroke:#a35b00,color:#111111\n"
                 "    classDef risk3 fill:#f6c9c9,stroke:#a30000,stroke-width:2px,color:#111111\n"
                 "```\n\n"
              << "> 地点 " << locations.size() << " 个、双向道路 " << roads
              << " 条，全部读取自 `world_map::locations()`；颜色为风险等级。\n\n";
}

/// 用途：路线图上的一个停靠点。输入/输出：只读数据；无状态修改。
struct DiagramStop {
    WorldLocationId location = WorldLocationId::Camp;
    std::string arrival; // 到达本节点的步骤编号
    std::string actions; // 在本节点执行的现场动作（采集、建造、结算、战斗）
};

/// 用途：把路线命令表转换为停靠点序列，供 Mermaid 路线图使用。
/// 输入：路线步骤表。输出：停靠点序列。
/// 失败：出现不相邻的移动、或非要塞处的撤退时抛出异常，绝不画出与规则不符的路线。
std::vector<DiagramStop> buildRouteStops(const std::vector<delivery::RouteStep>& route) {
    std::vector<DiagramStop> stops;
    stops.push_back({WorldLocationId::Camp, "出发", ""});
    int index = 0;
    for (const delivery::RouteStep& step : route) {
        ++index;
        const std::string tag = "步" + std::to_string(index);
        if (step.kind == delivery::RouteStepKind::AttackUntilDefeated) {
            if (!stops.back().actions.empty()) stops.back().actions += "<br/>";
            stops.back().actions += tag + " attack ×n（直至击退）";
            continue;
        }
        WorldLocationId target = stops.back().location;
        bool moved = false;
        std::string tagSuffix;
        if (step.command.rfind("move ", 0U) == 0U) {
            const auto parsed = tribe::world_map::parse(step.command.substr(5U));
            if (!parsed || !tribe::world_map::adjacent(stops.back().location, *parsed))
                throw std::runtime_error("路线图生成失败：不是相邻道路的移动 " + step.command);
            target = *parsed;
            moved = true;
        } else if (step.command == "retreat") {
            if (stops.back().location != WorldLocationId::RockfangFort)
                throw std::runtime_error("路线图生成失败：只允许在岩牙要塞撤退");
            target = WorldLocationId::OldPass;
            moved = true;
            tagSuffix = " retreat 撤退";
        }
        if (moved) {
            stops.push_back({target, tag + tagSuffix, ""});
            continue;
        }
        if (!stops.back().actions.empty()) stops.back().actions += "<br/>";
        stops.back().actions += tag + " " + step.command;
        // 结算节点加星标：读者一眼就能看出“载货在这里才入库”。
        if (step.command == "settle") stops.back().actions += " ★ 结算入库";
    }
    return stops;
}

/// 用途：打印 Mermaid 路线图（停靠点带步骤编号与现场动作）。
/// 输入：小节标题与路线表。输出：Markdown 代码块；路线不合法时计入失败数并打印原因。
void printRouteMermaid(const std::string& title, const std::vector<delivery::RouteStep>& route) {
    std::cout << "### " << title << "\n\n";
    std::vector<DiagramStop> stops;
    try {
        stops = buildRouteStops(route);
    } catch (const std::exception& error) {
        ++failures;
        std::cout << "路线图生成失败：" << error.what() << "\n\n";
        return;
    }
    std::cout << "```mermaid\nflowchart LR\n";
    for (std::size_t index = 0U; index < stops.size(); ++index) {
        const std::size_t location = tribe::indexOf(stops[index].location);
        std::cout << "    P" << (index + 1U) << "[\"" << (location + 1U) << " "
                  << tribe::world_map::locations()[location].name;
        if (stops[index].arrival == "出发")
            std::cout << "<br/>出发";
        else
            std::cout << "<br/>(" << stops[index].arrival << ")";
        if (!stops[index].actions.empty()) std::cout << "<br/>" << stops[index].actions;
        std::cout << "\"]\n";
    }
    for (std::size_t index = 1U; index < stops.size(); ++index) {
        std::cout << "    P" << index << " -->|\"" << stops[index].arrival << "\"| P" << (index + 1U) << "\n";
    }
    std::cout << "    class P1,P" << stops.size() << " terminus\n"
                 "    classDef terminus fill:#ffd98a,stroke:#7a4a00,stroke-width:2px,color:#111111\n"
                 "```\n\n";
}

/// 用途：生成完整的 Mermaid 图示文档（拓扑图 + 两段路线图 + 图例）。
/// 输入：无。输出：Markdown；无状态修改。
void printMermaidDocument() {
    std::cout << "# 十六地点地图与完整试玩路线（Mermaid）\n\n"
                 "> 生成方式：`./out/<构建目录>/tribe-map-playthrough --mermaid > "
                 "artifacts/map-diagrams.md`\n"
                 "> 图 1 的地点、道路与风险颜色读取自 `world_map_catalog`；图 2、图 3 的移动序列读取自 "
                 "`delivery_route.hpp`。\n"
                 "> 因此**图示与代码同源**：地图或路线一旦被改坏（例如此处移动到不相邻地点），生成即失败并返回非 0。\n\n"
                 "## 图 1：十六地点道路总览\n\n";
    printMapMermaid("道路总览（16 地点 / 17 条双向道路）");
    std::cout << "## 图 2：第一段完整试玩路线（木材采集，29 步）\n\n";
    printRouteMermaid("营地 → 苍林 → 芦苇沼泽 → 白羽营地(往返) → 盐风海岸 → 贝壳滩 → 潮盐港 → 山前集市 → "
                      "河鹿渡口 → 红土原 → 燧石矿场 → 玄石谷 → 玄石工坊 → 断崖商道 → 古老山隘 → 岩牙要塞 → 返程 → 营地结算",
                      delivery::gatherMissionRoute());
    std::cout << "## 图 3：第二段完整试玩路线（前哨建设，7 步）\n\n";
    printRouteMermaid("营地 → 红土原 → 河鹿渡口 → 山前集市 → 断崖商道 → 古老山隘（建前哨并结算）",
                      delivery::outpostMissionRoute());
    std::cout << "## 图例\n\n"
                 "| 记号 | 含义 |\n"
                 "| --- | --- |\n"
                 "| 节点底色 | 风险等级：绿 = 0 安全、浅绿 = 1 低、橙 = 2 中、红 = 3 高 |\n"
                 "| `可采：…` | 该地点允许采集的资源（`world_map::supportsResource` 的唯一分布） |\n"
                 "| `初始结算点 ★` | 开局唯一可 `settle` 的地点；任意地点建成前哨后也成为结算点 |\n"
                 "| `【死胡同】` | 只有一个邻接地点，进入后必须原路返回（白羽营地、岩牙要塞） |\n"
                 "| 拓扑图连线 `---` | 双向可通行的道路 |\n"
                 "| 路线图箭头上的 `步N` | 命令表中的第 N 步，与 `docs/02` 第 5 节、`artifacts/route-tables.md` "
                 "的序号一致（`attack ×n` 表示重复直到击退） |\n"
                 "| 路线图金色节点 | 起点与终点（两段都以结算收尾） |\n";
}

} // namespace

int main(const int argc, const char* const argv[]) {
    if (argc > 1 && std::string{argv[1]} == "--route-markdown") {
        printRouteMarkdown();
        return 0;
    }
    if (argc > 1 && std::string{argv[1]} == "--mermaid") {
        printMermaidDocument();
        return failures == 0 ? 0 : 1;
    }

    std::cout << "《燧火纪：部落黎明》· 成员3交付实录\n";
    std::cout << "内容：十六地点地图任务与小队系统（移动 / 采集 / 遭遇 / 前哨建设 / 结算）\n";
    std::cout << "模式：Standard  固定种子：20250915（保证实录可复现）\n";

    GameEngine game{{GameMode::Standard, 20250915U}};
    const int woodAtStart = game.state().wood;
    const int stoneAtStart = game.state().stone;

    printSection("第零段：经营阶段准备（分配劳力后才能出任务）");
    runCommand(game, "assign wood 2", "木材队2人：采集任务的劳力与载货容量都由它决定");
    runCommand(game, "assign stone 2", "石料队2人：前哨建设任务要求木材队与石料队各至少2人");
    runCommand(game, "workforce", "只读查看统一人口池占用与已激活效果");

    printSection("地图页面（任务出发前：道路总览 + 结算点星标 + 路线提示）");
    runCommand(game, "mission wood", "从燧火营地出发，执行木材采集任务");
    printMapPage(game, "交付路线：十六地点地图总览");

    runRoute(game, delivery::gatherMissionRoute(),
             "第一段：木材采集任务（营地→苍林→…→岩牙要塞→…→营地，覆盖十六地点后结算）");
    printMapPage(game, "营地结算后的地图页面（已发现 16/16，星标为结算点）");

    printSection("第二段出发：前哨建设任务（路线表以出发后的动作为准）");
    runCommand(game, "mission outpost", "从仓库带走木材6、石料4 随队出发；建设任务期间不能再采集");
    runRoute(game, delivery::outpostMissionRoute(), "第二段：前哨建设任务（古老山隘建前哨并在前哨结算）");
    printMapPage(game, "前哨结算后的地图页面（古老山隘 ★ 成为第二个结算点）");

    std::cout << "\n长期地图视图（worldText：地点、职能与结算点标记）\n";
    std::cout << game.worldText();

    printSummary(game, woodAtStart, stoneAtStart);
    return failures == 0 ? 0 : 1;
}
