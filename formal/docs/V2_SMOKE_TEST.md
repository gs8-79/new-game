# V2 程序烟雾测试记录

日期：2026-09-01
程序：`out/Release/tribe-dawn.exe`
环境：Windows，Visual Studio 2026 / MSVC 14.51，非交互管道模式（自动使用静态结局画面）。

## 路线一：双语命令与存档往返

输入：

```text
campaignseed quick 42
1
save 1
3
load 1
8
back
v2load auto
1
forcequit
```

确认结果：

- 固定种子42进入快速8季，初始季节为9/16。
- `save 1` 成功；采集后食物由42变为50；`load 1` 后恢复为42且行动点恢复为3。
- 第9季结算后自动存档，季节变为10/16。
- 返回主菜单后使用 `v2load auto` 恢复到同一季节、资源、小队疲劳和关系状态。
- 程序正常退出，退出码为0。

## 路线二：季节上限、结局演出和重放

输入：

```text
campaignseed quick 7
8
8
8
8
8
8
8
8
objectives
choose migration
replay
chronicle
back
q
```

确认结果：

- 第16季结算进入结局选择阶段。
- `choose migration` 生成“迁徙新生”结局、静态ASCII画面、后日谈、统计和重要编年史。
- `replay` 可再次播放结局；`chronicle` 可继续查看记录。
- 返回时V2自动档写入成功，程序退出码为0。

## 证据边界

## 路线三：苍林采集收益有界

固定种子73进入课程战役和苍林任务，依次执行`move forest`、`move deep`、`move hunting`，再连续输入4次`gather food`。前三次成功，第四次明确提示“采集时段已经结束”；失败命令不修改状态，之后仍可正常回营。

## 路线四：地图接触门槛

新课程战役尚未发现潮盐港时输入`trade tide food wood`，程序明确拒绝并提示尚未发现接触地点，资源和行动点不变。

## 最终复测

- 修复后Visual Studio 2026 Debug、Release均通过CTest 2/2，旧Demo `33/33`、正式/V1/V2 `73/73`。
- 上述四条Release程序路线均在仓库内独立烟测目录重新执行，退出码为0。

这些记录证明本机Release程序级主流程、采集上限与地图门槛可运行；防守不能自动征服由自动测试覆盖。它们不等于Mac实机、交换小组试玩、课堂验收或远端发布已经完成。
