# 界面重设计验证

本轮验证基线是 `qianduan1`，保留主游戏、苍林任务、人物装备、外交派系、战争、结局和存档可靠性测试。删除的测试对应已移除系统；原有纯文本界面检查已由新界面测试接替。

新增测试覆盖：封面中文居中与窄屏折行；无ANSI输出和篝火颜色；三种模式、固定种子、无效输入、B返回、EOF；七档摘要的时间/字段/文件只读性；主档路径被文件夹占用；备份恢复、临时文件恢复；手动覆盖取消、自动档读取和保存失败后继续游戏。

执行方法：
```powershell
.\build-formal.ps1 -Configuration Debug -Clean
.\build-formal.ps1 -Configuration Release -Clean
```

## 本地验证结果（2026-09-05）

Windows 上已完成 Debug、Release 的全量清理配置、编译和测试；最后一次封面居中及代码格式调整后，又分别完成增量构建与全部测试。

| 配置 | 最新测试结果 | 测试程序耗时 | 本地日志 |
| --- | --- | --- | --- |
| Debug | 66/66 通过 | 1.09秒 | `out/Formal-Debug/Testing/Temporary/LastTest.log` |
| Release | 66/66 通过 | 0.96秒 | `out/Formal-Release/Testing/Temporary/LastTest.log` |

CTest 注册1个测试程序，程序内部执行66项用例，其中9项为新增界面/应用流程测试。

Release 已在真实 Windows PTY 终端走通：封面 → 帮助目录/经营分类 → 返回 → 正式游戏 → 手动保存1 → 苍林移动与采集草药 → 回营 → 返回封面 → 七档列表 → 自动档加载 → 正常退出。已检查中文、颜色、菜单、任务状态和存档摘要；进程退出码为0。烟测存档隔离在 `out/interface-smoke-20260905/saves/game`，未使用玩家存档目录。最终调整后另外检查了重定向纯文本封面，图案和四项选项正常且没有ANSI转义。

PowerShell 打包脚本解析和两个 macOS Shell 脚本语法检查通过；未生成或验收发布ZIP。Git差异空白检查通过，验证时分支为 `qianduan1`。

macOS代码保留条件编译支持，但macOS实机、交换小组试玩和课堂验收尚未验证。本地测试结果不等同于这些验收。
