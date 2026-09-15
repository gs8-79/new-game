# 成员3交付物：地图任务与小队系统

> 项目：《燧火纪：部落黎明》正式版（`formal/`，C++17 + CMake）
> 角色：成员3 · 地图任务负责人（依据根目录 `TEAM_ASSIGNMENT.md` §5）
> 负责范围：十六地点道路地图、小队移动与行动点、资源采集、前哨建设与结算、岩牙要塞遭遇
> 主要维护文件：`formal/src/expansion_game.cpp`、`formal/src/world_map_catalog.cpp`、`formal/include/tribe/expansion_game.hpp`、`formal/include/tribe/expansion_types.hpp`
> 最近一次验收：**构建通过、全部测试通过（团队 `main` 46/46、本地快照 47/47）、试玩路线 45 条命令 0 失败**（见 `artifacts/`）
>
> **要上传到 GitHub / 开 PR？直接看 [`UPLOAD-TO-GITHUB.md`](UPLOAD-TO-GITHUB.md)（含提交清单、提交信息、可直接粘贴的 PR 正文）。**
> **要评审我的改动？看 [`docs/06-协作边界与评审清单.md`](docs/06-协作边界与评审清单.md)（文件归属、需要谁确认、回滚方式）。**

---

## 1. 交付物清单

| 文件 | 内容 | 对应任务书 |
| --- | --- | --- |
| `UPLOAD-TO-GITHUB.md` | GitHub 上传/PR 指南：提交清单、排除项、提交信息、可直接粘贴的 PR 正文、常见问题 | 协作上传 |
| `docs/01-项目分析.md` | 全项目分析：工程形态、四层架构、命令系统、状态与 v6 存档、测试体系、成员3范围定位与 15 条带行号风险 | 5.1 前置分析 |
| `docs/02-十六地点地图与试玩路线.md` | 十六地点权威邻接表 + **Mermaid 地图**、地图结构（主环 + 东线捷径 + 两条死胡同）、风险/收益档案、**完整试玩路线命令表 + Mermaid 路线图**、实测结果、常见迷路点 | 5.3 地图 + 试玩路线 |
| `docs/03-地图任务说明表.md` | 地点 / 可采资源 / 消耗 / 奖励 / 结算点 全表，含出发条件、失败矩阵与任务状态字段速查 | 5.3 地图任务说明表 |
| `docs/04-优化与改动说明.md` | 5.2 四项优化的实现、取舍、影响面与合并建议；核查中发现的 5 个问题（M3-1…M3-5）与修复 | 5.2 优化 |
| `docs/05-测试说明与验收矩阵.md` | 13 个新用例逐条说明、需求→测试覆盖矩阵、断言风格、已知未覆盖范围 | 5.3 测试 + 5.2 测试 |
| `docs/06-协作边界与评审清单.md` | 文件归属（我的 / 别人的）、§8-4 自查证据、Reviewer Checklist、冲突风险、三级回滚、与队友交付物的对接点 | 协作 |
| `tests/map_mission_delivery_tests.cpp` | 交付测试：13 个用例 / 189 条断言（采集、移动、前哨、结算、失败、行动点、错误地点、状态校验、端到端） | 5.3 验收测试 |
| `tools/delivery_route.hpp` | 完整试玩路线的**唯一数据来源**（文档、测试、工具共用），含每步说明与击退遭遇语义 | 5.3 试玩路线 |
| `tools/playthrough_main.cpp` | 试玩工具 `tribe-map-playthrough`：按路线表驱动真实引擎，输出实录；`--route-markdown` 生成命令表，`--mermaid` 生成地图与路线图 | 5.3 试玩路线 |
| `tools/run-delivery-checks.sh` | 一键验收：构建 → 全量测试 → 生成路线表、Mermaid 图与试玩实录 | 5.3 验收 |
| `tools/stage-for-github.sh` | 安全暂存：只 `git add` 我负责的文件，自动拦截 `out/`、`saves/`、`.DS_Store`、二进制 | 协作上传 |
| `tools/package-deliverables.sh` | 生成自包含提交包（`dist/*.tar.gz` + `.zip`，含代码快照与 MANIFEST），供课程平台提交 | 交付打包 |
| `.gitignore.suggested` | 建议的仓库根 `.gitignore`（供架构负责人在独立小 PR 采用；本文件不参与构建） | 协作上传 |
| `artifacts/playthrough-transcript.txt` | 真实引擎执行实录：45 条命令、逐条回执、地图页面、最终摘要 | 5.3 试玩路线证据 |
| `artifacts/test-run-Release.log` | 完整测试日志（在团队 `main` 上运行，46/46 通过） | 5.3 验收证据 |
| `artifacts/route-tables.md` | 由工具生成的路线命令表（文档第 5 节的数据来源） | 5.3 试玩路线 |
| `artifacts/map-diagrams.md` | 由工具生成的 Mermaid 图：十六地点道路总览 + 两段试玩路线图 + 图例（图示与代码同源，路线不合法即生成失败） | 5.3 地图与路线 |

## 2. 一键验收

```bash
# 在仓库根目录执行：配置 + 构建 + 全量测试 + 生成路线表/Mermaid 图/试玩实录（全部通过则退出码为 0）
bash deliverables/member3-map-mission/tools/run-delivery-checks.sh
```

> 脚本会自动带上 `-DTRIBE_BUILD_DELIVERY_TOOLS=ON`（试玩工具是**可选目标，默认关闭**，
> 所以其他成员与 CI 的默认构建行为完全不变）。构建目录默认 `out/macos-formal-Release`，
> 可用 `TRIBE_BUILD_DIR=... ` 覆盖，或让它自动沿用已存在的 `out/Formal-<Config>`。

输出末尾应看到：

```
47/47 tests passed        # 本地快照；在团队 main 上为 46/46（既有用例少 1 项）
命令总数：45  失败数：0
结论：交付试玩路线在真实引擎上全部成功，采集、移动、遭遇、前哨建设与结算均已验证。
成员3交付验收完成：构建通过、测试全绿、试玩路线全部成功。
```

## 3. 一分钟看懂交付内容

**地图**：16 个地点、17 条双向道路，结构 = 一个 9 地点主环 + 一条东线捷径 + 两条死胡同（白羽营地、岩牙要塞）。唯一最远点岩牙要塞距营地 6 步。地图数据只有一份（`world_map_catalog.cpp`），本次新增了 BFS 最短路查询与 16 条地点档案。

> Mermaid 图（十六地点道路总览 + 两段试玩路线图）见 `docs/02-十六地点地图与试玩路线.md` 第 1.1 / 5.5 节，或独立文件 `artifacts/map-diagrams.md`；图由 `tribe-map-playthrough --mermaid` 从代码直接生成。

**试玩路线**：两段任务、2 点行动点、45 条命令，覆盖全部十六地点，包含采集 12 单位木材、一次岩牙遭遇（先撤退演示、再击退拿徽记）、在古老山隘建前哨，并以“营地结算 + 前哨结算”两种合法结算点收尾。实录见 `artifacts/playthrough-transcript.txt`。

**规则要点**（详见 `docs/03-地图任务说明表.md`）：

- 采集只在指定资源且地点支持时成功；食物任务可顺带采兽皮；每次任务最多 4 次采集；载货上限 = 16 + 队伍人数 × 4。
- 移动只能沿相邻道路；遭遇期间道路封锁；撤退固定回古老山隘。
- 前哨需要现场木材 6、石料 4；建成后该地点成为结算点。
- **载货只在 `settle` 成功时进入部落仓库**；放弃任务（`abort`）丢弃载货；未结算存档再读回也不会入账。

**本次生产代码改动**：地图目录新增 `LocationProfile` / `roadDistance` / `roadPath` / `nextStepToward`；任务层新增 `missionRouteHint` 与 `look` 路线提示；地图页新增结算点 `★` 星标与提示行；修复 5 个失败路径一致性问题（结算后仍能使用草药/开战、`defend`/`retreat` 被误报为“未知命令”、未发现落点导致撤退被取消、采集时段上限字面量分散）。**没有新增任何持久化字段，v6 存档完全兼容。**

## 4. 验收对照（任务书 5.3）

| 要求 | 交付证据 | 状态 |
| --- | --- | --- |
| 一张十六地点地图 | `docs/02` 第 1 节权威邻接表 + 第 2 节结构分解；`docs/03` 第 2 节地点表 | ✅ |
| 一条完整试玩路线 | `docs/02` 第 5 节（29 + 7 步命令表）+ `artifacts/playthrough-transcript.txt`（真实执行 45 命令 0 失败） | ✅ |
| 地图任务说明表：地点、可采资源、消耗、奖励、结算点 | `docs/03` 第 2、3、5、6 节（另含出发条件、失败矩阵、状态字段） | ✅ |
| 至少一组采集、移动、前哨建设和结算测试 | `tests/map_mission_delivery_tests.cpp` 中 8 个核心用例 + 端到端用例；全量测试通过 | ✅ |
| 5.2 地图路线提示 | `docs/04` 第 2 节；测试用例 3；实录中地图页的“路线提示/探索提示” | ✅ |
| 5.2 地点风险 / 奖励 / 描述 | `docs/02` 第 4 节 + `docs/04` 第 3 节；测试用例 2 | ✅ |
| 5.2 结算前不入库规则核查 | `docs/04` 第 4 节（三层证据 + 3 条新验证）；测试用例 8、9 | ✅ |
| 5.2 失败 / 行动点 / 错误地点测试 | `docs/03` 第 7 节失败矩阵逐行有测试；测试用例 5、6、7、10、11、12 | ✅ |

## 5. 协作上传与评审（GitHub）

| 我要做什么 | 用哪个文件/脚本 |
| --- | --- |
| 把我的改动提交到团队仓库并开 PR | `UPLOAD-TO-GITHUB.md`（提交清单、排除项、提交信息、**可直接粘贴的 PR 正文**） |
| 只暂存我负责的文件、避免误提交构建产物 | `bash tools/stage-for-github.sh`（自动拦截 `out/`、`saves/`、`.DS_Store`、二进制） |
| 合并前自检 / 让队友一键复现 | `bash tools/run-delivery-checks.sh` |
| 交给课程平台的自包含压缩包 | `bash tools/package-deliverables.sh`（生成 `dist/*.tar.gz` + `.zip`，含代码快照与 MANIFEST） |
| 让队友快速评审我的改动 | `docs/06-协作边界与评审清单.md`（文件归属、Reviewer Checklist、回滚方式） |
| 给仓库补 `.gitignore`（可选，由架构负责人决定） | `.gitignore.suggested` |

**协作边界自查（依据 `TEAM_ASSIGNMENT.md` §8-4）**：未改动任何公共结构体字段、枚举、存档字段与命令名称；
`kSaveVersion` 仍为 6，**v6 存档完全兼容**。新增的公开声明只有常量与只读查询接口。
需要对应负责人确认的只有两处：`formal/src/console_ui.cpp`（成员4，纯新增约 25 行）与
`CMakeLists.txt`（成员1/成员5，测试源 1 行 + 可选工具目标），两处都可独立回退。

## 6. 给评审与合并者的提示

- **改动面**：生产代码只动了 5 个文件（地图目录 2 个、任务层 2 个、`console_ui.cpp` 小改）；`expansion_types.*`、`save_codec.cpp`、`game_engine_*.cpp` 的数据结构未改。
- **默认构建零影响**：试玩工具是可选目标（`TRIBE_BUILD_DELIVERY_TOOLS`，默认 OFF），队友执行 `cmake -S . -B out/x && cmake --build out/x` 的产物与行为与改动前完全一致（已验证）；交付测试则注册进 `ctest`，默认就会跑。
- **测试位置**：交付测试放在我自己的交付目录 `deliverables/member3-map-mission/tests/`，而不是成员5 负责的 `formal/tests/`，以避免目录归属冲突；`CMakeLists.txt` 只加 1 行源文件 + 1 个 include 路径。
- **存档兼容**：`ExpansionState` 字段顺序与数量未变，v6 存档可继续读写；`docs/01` 风险 1 解释了为什么这是硬约束。
- **回归命令**：`bash deliverables/member3-map-mission/tools/run-delivery-checks.sh`（等于 `build-formal-macos.sh` 的加强版）。
