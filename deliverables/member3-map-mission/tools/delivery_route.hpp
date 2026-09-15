#pragma once

// 成员3交付物：完整试玩路线的唯一数据来源。
//
// 为什么单独放在一个头文件里：
//   1) 交付文档《02-十六地点地图与试玩路线.md》里的命令表、验收测试和试玩工具必须描述同一条路线；
//   2) 如果三处各写一份，任何一次地图调整都会让文档与测试互相矛盾；
//   3) 本文件只有数据（命令文本 + 说明），不含任何游戏规则实现，因此可以直接被测试与工具复用。
//
// 不变量：
//   - 每个 "move <编号>" 步骤的目标编号必须在世界地图目录中与上一处地点相邻；
//   - 采集路线必须覆盖全部十六地点，并在燧火营地结束；
//   - 前哨路线必须建立前哨并在该前哨结算。
// 以上不变量由 tests/map_mission_delivery_tests.cpp 静态校验，任何一步写错都会让测试失败。

#include <string>
#include <vector>

namespace delivery {

/// 用途：标记一个试玩步骤的语义，让测试与工具正确处理“击退遭遇”这类不定次数步骤。
enum class RouteStepKind {
    /// 输入命令一次。
    Command = 0,
    /// 反复输入 attack，直到岩牙巡逻被击退（最多 kMaximumAssaultAttempts 次）。
    AttackUntilDefeated
};

/// 用途：一次试玩中执行的一步。输入/输出：只读数据；无状态修改。
struct RouteStep {
    std::string command;
    RouteStepKind kind = RouteStepKind::Command;
    std::string note;
};

/// 用途：击退岩牙巡逻的安全次数上限，防止脚本在规则被改坏时无限循环。
constexpr int kMaximumAssaultAttempts = 10;

/// 用途：返回采集任务的完整命令序列。输出：从燧火营地出发、覆盖十六地点并回营结算的步骤表。
/// 说明：前置条件是先在经营阶段执行 assign wood 2（木材队至少2人）并输入 mission wood 出发。
inline std::vector<RouteStep> gatherMissionRoute() {
    return {
        {"move 2", RouteStepKind::Command, "燧火营地→苍林：先在手边采集，避免全程空跑"},
        {"gather wood", RouteStepKind::Command, "采集木材（采集时段1/4）"},
        {"gather wood", RouteStepKind::Command, "采集木材（采集时段2/4），此后留出时段以备返程"},
        {"move 4", RouteStepKind::Command, "苍林→芦苇沼泽（风险2级：木材与草药）"},
        {"move 6", RouteStepKind::Command, "芦苇沼泽→白羽营地（风险0级：草药与外交）"},
        {"move 4", RouteStepKind::Command, "白羽营地→芦苇沼泽：支线必须原路返回，这是最容易迷路的一段"},
        {"move 10", RouteStepKind::Command, "芦苇沼泽→盐风海岸"},
        {"move 12", RouteStepKind::Command, "盐风海岸→贝壳滩（纯通道，涨潮封路）"},
        {"move 11", RouteStepKind::Command, "贝壳滩→潮盐港"},
        {"move 15", RouteStepKind::Command, "潮盐港→山前集市（三路交汇的交通节点）"},
        {"move 5", RouteStepKind::Command, "山前集市→河鹿渡口"},
        {"move 3", RouteStepKind::Command, "河鹿渡口→红土原"},
        {"move 7", RouteStepKind::Command, "红土原→燧石矿场"},
        {"move 13", RouteStepKind::Command, "燧石矿场→玄石谷"},
        {"move 14", RouteStepKind::Command, "玄石谷→玄石工坊"},
        {"move 16", RouteStepKind::Command, "玄石工坊→断崖商道"},
        {"move 8", RouteStepKind::Command, "断崖商道→古老山隘（岩牙部落接触点）"},
        {"move 9", RouteStepKind::Command, "古老山隘→岩牙要塞（风险3级：敌对巡逻）"},
        {"attack", RouteStepKind::Command, "第一次攻击巡逻：遭遇未结束期间道路被封锁，不能移动或采集"},
        {"retreat", RouteStepKind::Command, "撤退固定退回古老山隘，演示撤退落点稳定、不会把小队丢到随机地点"},
        {"move 9", RouteStepKind::Command, "再次进入要塞，准备彻底击退巡逻"},
        {"", RouteStepKind::AttackUntilDefeated, "反复 attack 击退岩牙巡逻，获得岩牙巡逻徽记（饰品，意志+1）"},
        {"move 8", RouteStepKind::Command, "要塞→古老山隘：击退后道路恢复通行"},
        {"move 16", RouteStepKind::Command, "古老山隘→断崖商道（开始返程）"},
        {"move 15", RouteStepKind::Command, "断崖商道→山前集市"},
        {"move 5", RouteStepKind::Command, "山前集市→河鹿渡口"},
        {"move 3", RouteStepKind::Command, "河鹿渡口→红土原"},
        {"move 1", RouteStepKind::Command, "红土原→燧火营地：十六地点至此全部发现"},
        {"settle", RouteStepKind::Command, "在营地结算：载货与任务背包在此时才写入部落仓库"},
    };
}

/// 用途：返回前哨建设任务的完整命令序列。输出：从营地前往古老山隘、建前哨并在前哨结算的步骤表。
/// 说明：前置条件是 assign wood 2、assign stone 2，且仓库木材≥6、石料≥4（出发时从仓库扣除随队携带）。
inline std::vector<RouteStep> outpostMissionRoute() {
    return {
        {"move 3", RouteStepKind::Command, "燧火营地→红土原：随队携带木材6、石料4，不能再采集"},
        {"move 5", RouteStepKind::Command, "红土原→河鹿渡口"},
        {"move 15", RouteStepKind::Command, "河鹿渡口→山前集市"},
        {"move 16", RouteStepKind::Command, "山前集市→断崖商道"},
        {"move 8", RouteStepKind::Command, "断崖商道→古老山隘：选这里当前哨，作为进攻岩牙要塞的前进基地"},
        {"build outpost", RouteStepKind::Command, "建造前哨：现场消耗木材6、石料4，此处变成结算点"},
        {"settle", RouteStepKind::Command, "在前哨结算：验证“营地以外也能入库”的第二条合法结算路径"},
    };
}

} // namespace delivery
