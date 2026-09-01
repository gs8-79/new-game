# 大型扩展 V1 程序烟雾测试记录

测试日期：2026-09-01

平台：Windows x64，Visual Studio 2026 / MSVC 14.51

程序：`out/Release/tribe-dawn.exe`

代码状态：V1提交前工作区，自动测试已通过；提交SHA在发布后补充到版本总结。

## 1. 和平贸易路线

输入：

```text
e
move forest
move deep
gather herbs
move clearing
talk
trade
return
back
q
```

关键输出：

```text
用2份口粮换得1份药包和1件贸易货物。
小队回营完成结算：经验、疲劳、凝聚力和物资均已处理。
燧火未熄，感谢游玩。
```

结果：程序退出码0。

## 2. 固定种子战斗、拾取与回营路线

输入：

```text
expandedseed 113 4
move forest
move deep
move clearing
raid
order focus
attack（重复，直到突破第三战线）
loot
return
back
q
```

关键输出：

```text
三段战线全部突破，外族放下武器，可以免费搜取一次战利品后回营。
免费搜取战利品成功；不消耗回合，也不增加疲劳。
小队回营完成结算：经验、疲劳、凝聚力和物资均已处理。
燧火未熄，感谢游玩。
```

结果：程序退出码0。

## 3. 证据边界

本记录证明当前 Windows Release 程序的两条纵切路线可运行。它不等于 macOS 实机验证、V1 ZIP 验证、交换小组试玩或课堂验收。
