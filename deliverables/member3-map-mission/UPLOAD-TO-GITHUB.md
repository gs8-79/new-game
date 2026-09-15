# 上传到 GitHub 项目的操作指南（成员3）

> 面向场景：你作为协作者，把自己的这部分改动提交到团队 GitHub 仓库（或提交 Pull Request）。
> 配套脚本：`tools/stage-for-github.sh`（安全暂存）、`tools/run-delivery-checks.sh`（合并前自检）。
> 协作依据：项目根目录 `TEAM_ASSIGNMENT.md` §8（协作边界）。

---

## 1. 上传前：先把文件准备好

| 顺序 | 命令 | 说明 |
| --- | --- | --- |
| 1 | `bash deliverables/member3-map-mission/tools/run-delivery-checks.sh` | 必须看到 `tests passed` 且无 `[FAIL]`、以及 `失败数：0`（团队 `main` 上为 46/46，本地快照 47/47） |
| 2 | `bash deliverables/member3-map-mission/tools/stage-for-github.sh` | 只暂存我负责的文件，并自动检查有没有误加 `out/`、`saves/` 等构建产物 |
| 3 | `git status --short` | 人工再确认一遍暂存清单（应只有第 2 节列出的那些路径） |
| 4 | `git commit -m "..."` | 用第 3 节给的提交信息 |
| 5 | `git push` / 开 PR | 用第 4 节的 PR 标题与正文（可直接粘贴） |

> 如果当前目录还不是 Git 仓库（`git rev-parse` 报“不是 Git 仓库”），说明你手上是原始工程包：
> 先 `git clone <团队仓库地址>`，再把**第 5 节列出的文件**按相同相对路径复制进去，然后从第 1 步开始。

## 2. 提交清单（我的改动共 2 类）

### 2.1 生产代码（6 个文件）

| 路径 | 改动性质 | 归属 |
| --- | --- | --- |
| `formal/src/expansion_game.cpp` | 修改：路线提示 + 5 处失败路径一致性修复 | 成员3（我） |
| `formal/include/tribe/expansion_game.hpp` | 修改：新增常量与只读接口 | 成员3（我） |
| `formal/src/world_map_catalog.cpp` | 修改：16 条地点档案 + BFS 最短路 | 成员3（我） |
| `formal/src/world_map_catalog.hpp` | 修改：新增档案结构与查询接口 | 成员3（我） |
| `formal/src/console_ui.cpp` | **修改（纯新增约 25 行）：地图页结算点 ★ 与路线提示两行** | **成员4（需确认）** |
| `CMakeLists.txt` | **修改：测试源 1 行 + include 1 处 + 可选工具目标 + 格式化清单 3 行** | **成员1/成员5（需确认）** |

### 2.2 交付物目录（整目录新增，14 个文件）

`deliverables/member3-map-mission/` —— 目录名与任何人都不重名，可以整目录提交，不会与队友冲突。

## 3. 建议的提交信息

```
feat(map): 补齐地图任务路线提示、地点档案与失败路径一致性

- world_map_catalog: 新增 LocationProfile（16 地点风险/收益/危险）与 BFS 最短路
  roadDistance/roadPath/nextStepToward，供任务层与界面复用
- expansion_game: 新增 missionRouteHint/routeHintText，look 与地图页给出
  “最近结算点、最近未探索地点、指定资源产地”的步数、方位链与可执行命令
- 修复 5 处失败路径一致性问题：结算后仍可用草药/开战、defend/retreat 被误报
  为未知命令、撤退落点未发现会导致撤退被整体取消、采集时段上限字面量分散
- 新增交付测试 13 例 189 断言（采集/移动/前哨/结算/失败/行动点/错误地点/状态校验/端到端）
- 新增可选工具 tribe-map-playthrough（默认关闭）生成试玩实录与 Mermaid 图
- 未改动任何公共结构体、枚举、存档字段与命令名称；v6 存档完全兼容

验证：默认构建全部测试通过（团队 main 46/46）；交付路线 45 条命令 0 失败
```

## 4. 可直接粘贴的 PR 正文

```markdown
## 这个 PR 做了什么

完成 **成员3：地图任务与小队系统** 的交付（`TEAM_ASSIGNMENT.md` §5），并落实 §5.2 的四项优化。
全部改动都经过真实编译、真实测试与真实试玩验证。

### 1. 功能与优化

**① 地图路线提示（减少迷路）**
地图目录新增 BFS 最短路（`roadDistance` / `roadPath` / `nextStepToward`），任务层新增 `missionRouteHint()`：
`look/查看` 与地图页现在直接告诉玩家「最近结算点几步、什么方向、该输入哪条 move」，
不产地资源时提示「最近产地 + 步数 + 命令」。实测输出：

```
路线提示：最近结算点 1.燧火营地 1步（东），输入 move 1
探索提示：最近未探索地点 4.芦苇沼泽 2步（西→北），输入 move 4
```

**② 每个地点的风险 / 奖励 / 描述**
新增 `world_map::LocationProfile`：16 个地点各有风险等级（0 安全 … 3 高）、可期收益与现场危险说明，
在 `look` 与地图页展示。风险等级**只做信息展示、不改任何数值**，因此不影响既有平衡与测试。

**③ 核查“采集后未结算不能进入部落仓库”**
三层证据确认规则成立：地图层 `settle` 只改阶段并保留载货；引擎层只在 `Settled` 时一次性入账；
`abort` 直接丢弃载货。新增 3 条验证：结算后仓库恰好 +载货、放弃任务仓库零增加、任务中存档再读回也不入账。

**④ 失败 / 行动点 / 错误地点测试**
新增 13 个用例（189 条断言），每条被拒绝的命令都用 19 字段快照证明「状态一个字节都没改」。

### 2. 修复的问题（失败路径一致性）

| 编号 | 问题 | 处理 |
| --- | --- | --- |
| M3-1 | `useHerb` 无 phase 门禁：结算后仍能用药并推进任务回合 | 补 phase 门禁 |
| M3-2 | `attack/defend/retreat` 同样缺 phase 门禁 | 三个函数统一补上 |
| M3-3 | 无遭遇时 `defend`/`retreat` 返回“未识别”，界面误报“无法识别该命令” | 改为“识别后拒绝”，提示“当前没有遭遇战” |
| M3-4 | 撤退落点若未登记为已发现，候选状态校验失败会**整体取消撤退**、把小队困在遭遇里 | 撤退时顺手标记落点已发现（防御性兜底） |
| M3-5 | 采集时段上限 `4` 在 3 处各写一遍 | 提取为 `kMaximumHarvestActions` 常量 |

### 3. 验证方式

```bash
bash deliverables/member3-map-mission/tools/run-delivery-checks.sh
# 期望输出：46/46 tests passed（团队 main；本地快照 47/47）；命令总数：45  失败数：0
```

- 默认构建（不含我的可选工具目标）：`cmake -S . -B out/x && cmake --build out/x` → 与原行为完全一致
- 交付测试 13 例已注册进 `ctest`，与既有回归用例一起运行
- 完整试玩实录：`deliverables/member3-map-mission/artifacts/playthrough-transcript.txt`
- 地图与路线 Mermaid 图（由代码生成）：`deliverables/member3-map-mission/artifacts/map-diagrams.md`

### 4. 兼容性与协作边界

- **未改动**任何公共结构体字段、枚举、存档字段与命令名称（`TEAM_ASSIGNMENT.md` §8-4 自查通过）；
  `kSaveVersion` 仍为 6，**v6 存档完全兼容**
- 新增的公开声明只有常量与只读查询接口，已在 `docs/06-协作边界与评审清单.md` §1.4 报备架构负责人
- 需要对应负责人确认的两处：`formal/src/console_ui.cpp`（成员4，纯新增约 25 行）、
  `CMakeLists.txt`（成员1/成员5，测试源 1 行 + 可选目标）。两处都可独立回退，回退后功能仍由 `look` 提示覆盖。

### 5. 交付物

文档（全项目分析、十六地点地图与试玩路线、地图任务说明表、优化与改动说明、测试说明与验收矩阵、
协作边界与评审清单）、13 例交付测试、试玩工具（可选目标）、一键验收脚本与全部实录，均在
`deliverables/member3-map-mission/`，目录名不与他人重名。
```

## 5. 从“原始工程包”搬到“团队仓库”时要复制哪些文件

```
CMakeLists.txt                                        （共享文件，只保留我那 4 处改动）
formal/include/tribe/expansion_game.hpp
formal/src/expansion_game.cpp
formal/src/world_map_catalog.hpp
formal/src/world_map_catalog.cpp
formal/src/console_ui.cpp                             （需与成员4协调）
deliverables/member3-map-mission/                      （整目录）
```

> 如果团队仓库里这些文件已经被队友改过，建议**逐个文件复制粘贴改动块**，而不是整文件覆盖——
> 尤其是 `CMakeLists.txt` 与 `console_ui.cpp`。

## 6. 千万不要提交的东西

| 路径 | 原因 |
| --- | --- |
| `out/` | 构建产物（二进制、CMake 缓存、测试日志），几百 MB 且与机器绑定 |
| `saves/` | 本地存档（`main.cpp` 把存档写在运行目录下） |
| `.DS_Store` | macOS 目录元数据（本工程根目录当前就有一个） |
| `*.o` / `*.a` / `tribe-dawn` 等可执行文件 | 同上，构建即可再生成 |

`tools/stage-for-github.sh` 已内置这些排除检查，误加会直接报错退出。

仓库根目录目前**没有 `.gitignore`**，建议由架构负责人在一个独立小 PR 里补上（避免与他人冲突），内容可直接用
`deliverables/member3-map-mission/.gitignore.suggested`。

## 7. 常见问题

| 情况 | 处理 |
| --- | --- |
| `git status` 里出现 `out/` 下的大量文件 | 执行 `git reset` 取消暂存，再用 `tools/stage-for-github.sh` 重新暂存 |
| 队友同时改了 `CMakeLists.txt`，push 被拒 | `git pull --rebase` 后按块解决冲突（我的改动集中在 4 个小块） |
| 队友改了 `console_ui.cpp` 导致冲突 | 优先保留队友版本，把我的 3 处新增按新结构重新插入；实在冲突就直接放弃这部分（`look` 提示仍然生效） |
| PR 里想展示地图 | 直接把 `artifacts/map-diagrams.md` 或 `docs/02` 里的 Mermaid 代码块贴进 PR 描述，GitHub 会渲染成图 |
| 想要一个自包含的课程提交包（含测试文件副本） | `bash deliverables/member3-map-mission/tools/package-deliverables.sh`，在 `dist/` 生成 tar.gz + zip |
