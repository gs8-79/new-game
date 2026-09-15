# 测试报告：存档子系统与测试体系专项分析

> 本报告所有数据均来自本机真实构建与测试运行输出，未使用任何推测或填充数据。
> 无法从运行结果或源码中确认的内容，一律标注「未获取」或「待确认」。

---

## 1. 测试环境

| 项目 | 值 |
|---|---|
| 操作系统 | Microsoft Windows 11 家庭版 中文版（内部版本 10.0.26200） |
| 编译器 | MSVC（cl.exe）19.51.36257 for x64，随 Visual Studio Community 2026 安装（安装目录 `D:\vs`） |
| CMake | 4.3.1-msvc1（Visual Studio 自带，非系统 PATH） |
| 构建生成器 | Ninja（Visual Studio 自带 ninja.exe） |
| 构建类型 | Debug（单配置生成器，通过 `-DCMAKE_BUILD_TYPE=Debug` 指定） |
| C++ 标准 | C++17（`CMAKE_CXX_STANDARD 17`，`CXX_EXTENSIONS OFF`） |
| 编译选项 | MSVC：`/W4 /permissive- /utf-8`；运行时库 `MultiThreadedDebug`（静态调试运行时） |
| 仓库远程地址 | https://github.com/gs8-79/new-game.git |
| 仓库 commit 哈希 | `e4a83475a4e804e0e0d5362d1dcec0358a58be2a`（短哈希 `e4a8347`，提交信息「重构：整合可读性模块化与可靠性校验」） |
| 当前分支 | `main`（构建与测试时工作区干净） |
| 测试执行日期时间 | 2026-09-15（北京时间，UTC+8）；构建完成 09:27:46，ctest 09:28:07，可执行文件直跑 09:28:10，格式检查 09:28:27 |
| 构建目录 | `build/`（out-of-source，未污染源码目录） |

### 1.1 环境加载方式

`cmake.exe`、`ninja.exe`、`cl.exe` 均不在系统 PATH 中。本次测试通过 Visual Studio 的
`D:\vs\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64` 加载 MSVC 开发环境（与项目自带
`build-formal.ps1` 的加载方式一致），再把 VS 自带的 CMake / Ninja 目录加入本次会话 PATH。

### 1.2 关于 clang-format 的说明

`CMakeLists.txt` 中 `find_program(CLANG_FORMAT_EXECUTABLE)` 的搜索提示路径写死为
`C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin`，而本机 VS 实际安装在
`D:\vs`，因此**首次配置时输出「clang-format was not found; format and format-check targets are unavailable」**，
`format-check` 目标未生成。为完成格式检查任务，第二次配置时手动追加
`-DCLANG_FORMAT_EXECUTABLE=D:\vs\VC\Tools\Llvm\x64\bin\clang-format.exe`，目标随即生成。
该操作仅通过 CMake 命令行参数指定工具路径，**未修改任何 CMakeLists.txt 或源码文件**。

---

## 2. 构建结果

### 2.1 配置与构建命令（原样记录）

加载 MSVC 环境后，在仓库根目录依次执行：

```powershell
# 配置（第二次配置，显式指定 clang-format 以使 format-check 目标可用）
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_MAKE_PROGRAM=D:\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe `
  -DCLANG_FORMAT_EXECUTABLE=D:\vs\VC\Tools\Llvm\x64\bin\clang-format.exe

# 构建
cmake --build build
```

### 2.2 构建结论

| 项目 | 值 |
|---|---|
| 配置是否成功 | 成功（退出码 0；首次配置 4.0 秒，增量重配 0.1 秒） |
| 构建是否成功 | **成功，退出码 0** |
| 编译/链接步骤总数 | 34 步（Ninja 并行编译，含静态库 `tribe_formal_lib.lib`、可执行文件 `tribe-dawn.exe`、`tribe-formal-tests.exe`） |
| 构建耗时 | 7.2 秒 |
| 编译警告数量 | **0（零警告）** |
| 编译错误数量 | **0** |

### 2.3 警告清单

**零警告。** 在 `/W4 /permissive- /utf-8` 选项下，全部 31 个编译单元与 3 个链接步骤均未输出任何
`warning`，对完整构建日志按「warning」不区分大小写检索，命中数为 0。

### 2.4 构建步骤清单（完整日志）

<details>
<summary>展开：cmake --build build 完整输出（34 步）</summary>

```text
[1/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\command_parser.cpp.obj
[2/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\expansion_types.cpp.obj
[3/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\seasonal_event_rules.cpp.obj
[4/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\population_rules.cpp.obj
[5/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\war_rules.cpp.obj
[6/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_command_catalog.cpp.obj
[7/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\world_map_catalog.cpp.obj
[8/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_war.cpp.obj
[9/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_dispatch.cpp.obj
[10/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_validation.cpp.obj
[11/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\expansion_game.cpp.obj
[12/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_mission.cpp.obj
[13/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_internal.cpp.obj
[14/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine.cpp.obj
[15/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_diplomacy.cpp.obj
[16/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_management.cpp.obj
[17/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\state_safety.cpp.obj
[18/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_text.cpp.obj
[19/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\game_engine_season.cpp.obj
[20/34] Building CXX object CMakeFiles\tribe-formal-tests.dir\tests\test_main.cpp.obj
[21/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\save_codec.cpp.obj
[22/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\save_file_transaction.cpp.obj
[23/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\save_repository.cpp.obj
[24/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\console_ui.cpp.obj
[25/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\ending_presentation.cpp.obj
[26/34] Building CXX object CMakeFiles\tribe-formal-tests.dir\formal\tests\map_mission_tests.cpp.obj
[27/34] Building CXX object CMakeFiles\tribe-dawn.dir\formal\src\main.cpp.obj
[28/34] Building CXX object CMakeFiles\tribe_formal_lib.dir\formal\src\application.cpp.obj
[29/34] Building CXX object CMakeFiles\tribe-formal-tests.dir\formal\tests\current_game_tests.cpp.obj
[30/34] Building CXX object CMakeFiles\tribe-formal-tests.dir\formal\tests\save_repository_tests.cpp.obj
[31/34] Building CXX object CMakeFiles\tribe-formal-tests.dir\formal\tests\application_workflow_tests.cpp.obj
[32/34] Linking CXX static library tribe_formal_lib.lib
[33/34] Linking CXX executable tribe-dawn.exe
[34/34] Linking CXX executable tribe-formal-tests.exe
```

</details>

### 2.5 格式检查（format-check，只检查不修改）

执行命令：`cmake --build build --target format-check`（对应 `clang-format --dry-run --Werror`，
覆盖 `CMakeLists.txt` 中 `TRIBE_FORMAT_SOURCES` 列出的全部 50 个源文件）。

| 项目 | 值 |
|---|---|
| 退出码 | 0 |
| 格式问题数量 | **0** |
| 是否执行过 format 目标改文件 | **否**，仅运行 format-check，未运行 format |

输出仅一行：`[1/1] Checking C++ formatting with clang-format`，无任何违规报告。

---

## 3. 测试执行结果

### 3.1 执行命令

按要求两种方式都运行：

```powershell
# 方式一：通过 CTest
ctest --test-dir build --output-on-failure -C Debug

# 方式二：直接运行可执行文件（Ninja 单配置，产物位于 build\tribe-formal-tests.exe）
.\build\tribe-formal-tests.exe
```

> 说明：`CMakeLists.txt` 中通过 `add_test(NAME tribe-formal-tests COMMAND tribe-formal-tests)`
> 只注册了一个 CTest 测试项（整个可执行文件），因此 CTest 视角为 1 个测试；用例粒度的统计以
> 可执行文件直跑输出为准。

### 3.2 结果汇总

| 指标 | CTest | 可执行文件直跑 |
|---|---|---|
| 用例/测试项总数 | 1（测试项） | **33（用例）** |
| 通过数 | 1 | **33** |
| 失败数 | 0 | **0** |
| 跳过数 | 0（框架无跳过机制） | **0** |
| 通过率 | 100% | **100%** |
| 耗时 | 3.86 秒（CTest 自报 Total Test time） | 3.20 秒（进程内计时） |
| 退出码 | 0 | 0 |

### 3.3 用例文件分布（33 个用例，按实际注册顺序）

| 测试文件 | 用例数 |
|---|---|
| `formal/tests/current_game_tests.cpp` | 15 |
| `formal/tests/map_mission_tests.cpp` | 7 |
| `formal/tests/save_repository_tests.cpp` | 7 |
| `formal/tests/application_workflow_tests.cpp` | 4 |
| **合计** | **33** |

### 3.4 CTest 完整输出

<details>
<summary>展开：ctest 完整日志</summary>

```text
Test project C:/Users/asus/Desktop/新建文件夹 (2)/new-game/build
    Start 1: tribe-formal-tests
1/1 Test #1: tribe-formal-tests ...............   Passed    3.85 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   3.86 sec
```

</details>

### 3.5 可执行文件直跑完整输出（33 个用例）

<details>
<summary>展开：tribe-formal-tests.exe 完整日志（33/33 PASS）</summary>

```text
[RUN ] the v5 interface exposes five resources, map roles, and road progress
[PASS] the v5 interface exposes five resources, map roles, and road progress
[RUN ] command catalog keeps whitespace Chinese English and numeric query contracts identical
[PASS] command catalog keeps whitespace Chinese English and numeric query contracts identical
[RUN ] five-resource barter remains available and currency commands are absent
[PASS] five-resource barter remains available and currency commands are absent
[RUN ] gathering and outpost missions keep distinct v5 mission kinds through saves
[PASS] gathering and outpost missions keep distinct v5 mission kinds through saves
[RUN ] uniform population pool blocks army and mission over-allocation
[PASS] uniform population pool blocks army and mission over-allocation
[RUN ] formed armies and garrisons consume trained warriors and can be released
[PASS] formed armies and garrisons consume trained warriors and can be released
[RUN ] population loss requires manual reassignment before any further action
[PASS] population loss requires manual reassignment before any further action
[RUN ] responsible crafter controls quality, effective attributes, and capped war power
[PASS] responsible crafter controls quality, effective attributes, and capped war power
[RUN ] responsible healer improves treatment and rest, and can be dismissed
[PASS] responsible healer improves treatment and rest, and can be dismissed
[RUN ] fixed refugees and disease events apply their two choices
[PASS] fixed refugees and disease events apply their two choices
[RUN ] fixed extortion and faction events include building effects and atomic failures
[PASS] fixed extortion and faction events include building effects and atomic failures
[RUN ] crafted equipment keeps a global serial after being equipped and recreated in the same season
[PASS] crafted equipment keeps a global serial after being equipped and recreated in the same season
[RUN ] using expedition herbs advances exactly one turn while applying recovery and fatigue consistently
[PASS] using expedition herbs advances exactly one turn while applying recovery and fatigue consistently
[RUN ] state validation rejects unsafe text invalid items and incoherent mission states atomically
[PASS] state validation rejects unsafe text invalid items and incoherent mission states atomically
[RUN ] fixed-seed command sequences preserve valid state and make rejected commands byte-identical no-ops
[PASS] fixed-seed command sequences preserve valid state and make rejected commands byte-identical no-ops
[RUN ] the sixteen-location catalog keeps stable ids aliases bidirectional roads and road-map coverage
[PASS] the sixteen-location catalog keeps stable ids aliases bidirectional roads and road-map coverage
[RUN ] map missions reject nonadjacent roads and wrong local resources without changing state
[PASS] map missions reject nonadjacent roads and wrong local resources without changing state
[RUN ] map mission cargo capacity caps harvesting and preserves a full load
[PASS] map mission cargo capacity caps harvesting and preserves a full load
[RUN ] outpost construction spends only carried materials and creates a settlement point
[PASS] outpost construction spends only carried materials and creates a settlement point
[RUN ] fort encounter blocks travel until retreat and keeps the route state coherent
[PASS] fort encounter blocks travel until retreat and keeps the route state coherent
[RUN ] settling a gathered map load is the only path that credits the tribe store
[PASS] settling a gathered map load is the only path that credits the tribe store
[RUN ] map commands share Chinese English and direct-outpost aliases without changing their contracts
[PASS] map commands share Chinese English and direct-outpost aliases without changing their contracts
[RUN ] save repository reports all seven empty slots and a failed load leaves its candidate untouched
[PASS] save repository reports all seven empty slots and a failed load leaves its candidate untouched
[RUN ] save repository round-trips every manual slot and the autosave
[PASS] save repository round-trips every manual slot and the autosave
[RUN ] v6 codec keeps deterministic bytes after load and re-save through a different slot
[PASS] v6 codec keeps deterministic bytes after load and re-save through a different slot
[RUN ] stockpile growth beyond the legacy 64-item decoder limit remains saveable
[PASS] stockpile growth beyond the legacy 64-item decoder limit remains saveable
[RUN ] a corrupt primary recovers its previous backup and a temporary file when it is the only valid copy
[PASS] a corrupt primary recovers its previous backup and a temporary file when it is the only valid copy
[RUN ] valid v5 primary backup and temporary saves migrate atomically to v6 and preserve original bytes
[PASS] valid v5 primary backup and temporary saves migrate atomically to v6 and preserve original bytes
[RUN ] damaged legacy and unsafe v6 text are rejected without changing caller state or creating migration backups
[PASS] damaged legacy and unsafe v6 text are rejected without changing caller state or creating migration backups
[RUN ] application supports a scripted new-game map-save-load-return workflow in an isolated directory
[PASS] application supports a scripted new-game map-save-load-return workflow in an isolated directory
[RUN ] application asks before overwriting a manual save and honours cancellation
[PASS] application asks before overwriting a manual save and honours cancellation
[RUN ] console rendering preserves UTF-8 output at the classroom 80-column baseline and a narrow window
[PASS] console rendering preserves UTF-8 output at the classroom 80-column baseline and a narrow window
[RUN ] mission road overview keeps coloring optional and text safe at classroom widths
[PASS] mission road overview keeps coloring optional and text safe at classroom widths
33/33 tests passed
```

</details>

---

## 4. 存档子系统专项测试分析（重点）

存档子系统由三个实现文件构成，职责分层清晰：

| 文件 | 职责 |
|---|---|
| `formal/src/save_codec.cpp`（992 行） | v6 二进制编解码：魔数/版本/长度/FNV-1a 校验和、v5/v6 兼容解码、逐字段范围校验、v5 装备序号推导 |
| `formal/src/save_file_transaction.cpp`（380 行） | 文件事务：临时写入→复解析验证→`.bak` 轮换→原子替换；`.bak`/`.tmp` 恢复；v5→v6 原子迁移 |
| `formal/src/save_repository.cpp`（276 行） | 七槽位仓库：`save`/`load`/`inspect`/`pathFor`/`parseSlot`/`slotName`，按「主档→`.bak`→`.tmp`」顺序恢复 |

文件格式（依据 `save_codec.cpp` 实际实现）：8 字节魔数 `TRIBESAV` + 4 字节版本 + 4 字节载荷长度 +
4 字节 FNV-1a 校验和 + 载荷，文件头共 20 字节；当前版本 `kSaveVersion = 6`，解码器同时接受 v5 与 v6，
v6 相对 v5 仅在载荷尾部多出 4 字节 `nextItemSerial`。

### 4.1 `save_repository_tests.cpp` 用例逐个说明

该文件共 **7 个用例**，全部 PASS。每个用例都通过 RAII 夹具 `TemporarySaveDirectory` 在系统临时目录
建立唯一存档根，析构时只清理本测试目录，不接触玩家存档。

#### 用例 S1：save repository reports all seven empty slots and a failed load leaves its candidate untouched

- **场景**：空存档目录的初始检查 + 空槽加载失败的安全性。
- **验证行为**：
  1. `inspect()` 恰好返回 7 个槽位（6 个手动槽 + 自动档），且每个状态都是 `SaveStatus::Empty`；
  2. 从空的 Slot1 执行 `load` 返回 `false`，错误串非空；
  3. **加载失败时调用方传入的 candidate 不被污染**——其 `seed` 与调用前完全一致。
- **覆盖路径**：多档位管理（七槽枚举）、空槽 Missing 分支、失败不修改调用方状态。

#### 用例 S2：save repository round-trips every manual slot and the autosave

- **场景**：全部七个槽位的保存/读取往返。
- **验证行为**：
  1. 依次向 Autosave、Slot1~Slot6 写入 7 个不同 seed 的状态，全部 `save` 成功；
  2. `inspect()` 后七个槽位全部为 `Ready`；
  3. 逐槽 `load`，读回的 seed 与写入值（210~216）逐一相等。
- **覆盖路径**：正常存取往返、自动档与手动槽等价、七槽互不串档。

#### 用例 S3：v6 codec keeps deterministic bytes after load and re-save through a different slot

- **场景**：序列化字节确定性（无时间戳/随机量导致的字节漂移）。
- **验证行为**：
  1. Slot1 保存后直接读取磁盘字节，偏移 8 处的版本字段等于 `kSaveVersion`（6）；
  2. 读入后再保存到另一个槽位 Slot2，Slot2 的磁盘字节与 Slot1 **逐字节相等**。
- **覆盖路径**：v6 编码确定性、「保存→读取→再保存」幂等、跨槽字节稳定。

#### 用例 S4：stockpile growth beyond the legacy 64-item decoder limit remains saveable

- **场景**：仓库物品数超过旧版 64 项解码上限。
- **验证行为**：构造含 65 个物品的 `stockpile`（旧解码器上限为 64，新版上限
  `kMaximumStockpileItems = 100000`），保存后重新读取，物品数量保持 65。
- **覆盖路径**：新旧版本容量上限差异、变长集合的计数编解码。

#### 用例 S5：a corrupt primary recovers its previous backup and a temporary file when it is the only valid copy

- **场景**：主档损坏时的两级回退恢复，以及恢复的幂等性。
- **验证行为**：
  1. 连续两次保存 Slot1（seed 221、222），第二次保存会把首版主档轮换为 `.bak`；
  2. 把主档内容截断改写为 `"damaged"`，`inspect()` 判定为 `Recoverable`；
  3. `load` 成功，恢复出的 seed 等于**首版 221**（来自 `.bak`）；紧接着第二次 `load` 仍恢复 221（幂等）；
  4. 对 Slot2：保存后手动把主档复制为 `.tmp`，再写坏主档，`load` 从 `.tmp` 恢复成功，seed 与保存值一致。
- **覆盖路径**：原子写入后的 `.bak` 恢复、仅存 `.tmp` 时的恢复、`inspect` 的 Recoverable 判定、恢复可重复。

#### 用例 S6：valid v5 primary backup and temporary saves migrate atomically to v6 and preserve original bytes

- **场景**：v5→v6 版本迁移，覆盖主档、`.bak`、`.tmp` 三种来源。
- **夹具构造**：辅助函数 `asV5` 把真实 v6 字节去掉尾部 4 字节 `nextItemSerial`、版本改写为 5、
  重算 FNV-1a 校验和，从而得到布局合法的历史 v5 样本。
- **验证行为**（三条子路径）：
  1. **主档即 v5**：加载后 `SaveLoadInfo.migratedFromV5 == true`，状态 seed 正确，
     `.v5.bak` 归档文件中的字节与迁移前 v5 原始字节逐字节一致，主档已变为 v6（版本字段为 6）；
  2. **主档损坏、`.bak` 为 v5**：从 `.bak` 完成迁移并把主档替换为 v6，同样保留 `.v5.bak`；
  3. **主档损坏、`.tmp` 为 v5**：从 `.tmp` 完成迁移，断言同上。
- **覆盖路径**：版本迁移的全部三个恢复来源、迁移原子性、原始字节归档、`SaveLoadInfo` 迁移上报。

#### 用例 S7：damaged legacy and unsafe v6 text are rejected without changing caller state or creating migration backups

- **场景**：被篡改存档与非法文本的拒绝，且拒绝过程零副作用。
- **验证行为**：
  1. 取合法 v5 夹具，把载荷第 20 字节翻转 1 位（校验和失配），`load` 失败；candidate 的 seed 不变；
     **不生成 `.v5.bak` 迁移归档**；磁盘原文件字节保持被篡改后的原样（不被「修复」）；
  2. 取合法 v6 存档，定位其中中文部落名「燧火」，把首字节改成 `0x1b`（ESC 控制字符）并重算校验和，
     `load` 失败；candidate 不变；错误信息包含「控制字符」。
- **覆盖路径**：校验和失配拒绝、UTF-8/控制字符合法性校验、失败不产生迁移备份、失败不回写文件。

### 4.2 已覆盖的关键路径对照

| 要求覆盖的关键路径 | 是否覆盖 | 对应用例 |
|---|---|---|
| 正常存取 | 是 | S2（七槽往返）、S3（字节确定性） |
| 存档格式版本迁移 | 是 | S6（v5→v6，主档/`.bak`/`.tmp` 三来源）；另由 `current_game_tests.cpp` 第 4 个用例验证 v4 版本被拒绝（错误含「需要新开局」） |
| 原子写入与中断恢复 | 是 | S5（`.bak` 与 `.tmp` 两级恢复、恢复幂等） |
| 非法/被篡改存档的拒绝 | 是 | S7（校验和翻转、控制字符注入）；current_game 第 4 用例（魔数内版本改 4） |
| UTF-8 文本合法性校验 | 是 | S7（存档解码层拒绝 ESC）；current_game 第 14 用例「state validation rejects unsafe text...」在 `replaceState` 层覆盖 ANSI 转义串、非法续接字节 `C3 28`、C1 控制区 `C2 80` 等 |
| 多档位管理 | 是 | S1（七空槽）、S2（七槽 Ready 与互不串档） |

### 4.3 缺口分析（诚实评估，当前并未「覆盖完整」）

以下缺口均依据 `save_codec.cpp`、`save_file_transaction.cpp`、`save_repository.cpp` 的实际代码分支
逐条比对得出，即「代码中存在该分支/契约，但 33 个用例没有覆盖」。

#### 缺口 G1：保存事务自身的失败回滚分支未直接测试（严重程度：中）

`writeAndVerify` 包含「临时文件复解析失败→删除 `.tmp`、主档不变」和「`.tmp` 替换主档失败→从 `.bak`
回滚」两条失败路径；现有 S5 测的是**保存成功之后**主档被外部损坏的读恢复，没有测试**保存过程中**
失败时旧主档是否完好、`.tmp` 是否被清理。
- **建议补充用例**：先写入一份有效主档 A 并记录其字节；再向同一槽位发起一次注定失败的保存
  （例如传入无法通过 `GameEngine::validateState` 的状态，使其在写盘前被拒绝）；断言 `save` 返回
  false、主档字节仍等于 A、目录下无残留 `.tmp`。

#### 缺口 G2：主档「不可用（Unavailable）」时不回退备份的策略未测试（严重程度：中）

`SaveRepository::load` 明确区分 Invalid 与 Unavailable：主档存在 IO 层问题（如路径是目录而非普通
文件）时返回 Unavailable，并且**故意不回退 `.bak`**，错误提示「为避免误读旧备份，本次没有自动回退」。
现有用例只覆盖 Missing（S1）与 Invalid（S5/S7），该策略分支无测试。
- **建议补充用例**：在某槽位主档路径创建同名子目录（使 `is_regular_file` 为假），同时放置一份合法
  `.bak`；断言 `load` 失败、状态未被 `.bak` 覆盖、错误信息包含「暂时不可用」。

#### 缺口 G3：`inspect()` 的 Corrupt 终态未断言（严重程度：中低）

`inspect` 的四种状态中，Empty（S1）、Ready（S2）、Recoverable（S5）均有断言，唯独
「主档/`.bak`/`.tmp` 均无效且并非全部缺失 → Corrupt」以及「主档 Unavailable → Corrupt」没有用例。
- **建议补充用例**：主档写入损坏字节且不提供任何 `.bak`/`.tmp`，断言该槽 `inspect` 状态为 Corrupt；
  再构造主档为目录的场景断言同样为 Corrupt。

#### 缺口 G4：文件头各拒绝分支覆盖不全（严重程度：中）

`deserializeFile` 的拒绝点包括：魔数错误、版本不支持、载荷长度字段与实际不符、校验和不符、合法载荷
后存在尾随字节。目前仅覆盖「版本改 4」（current_game 第 4 用例）与「载荷翻转导致校验和失配」（S7），
**魔数篡改、payloadSize 与实际长度不一致、尾随字节、空文件（0 字节）均无测试**。
- **建议补充用例**：以真实 v6 存档为母本，参数化生成四类畸形文件（改首 8 字节魔数、改偏移 12 的长度
  字段、末尾追加 1 字节、写 0 字节文件），分别断言 `load` 失败、candidate 不变、错误信息对应
  「不是游戏存档 / 长度不一致 / 尾部字段 / 为空或超过安全大小」。

#### 缺口 G5：集合计数与字段上限的边界测试不足（严重程度：中低）

S4 只证明 65 项可保存，但下列解码器硬性区间均无越界拒绝测试：`roster` 数量要求 [2,64]、永久小队
`squads` 要求 (0,8]、`leadershipHistory` 要求 (0,256]、`chronicle` 要求 (0,200]、任务背包 ≤64、
仓库 `stockpile` ≤100000、单字符串 ≤1 MiB、整文件 ≤16 MiB。
- **建议补充用例**：在合法 v6 字节上改写对应计数字段为 0、下界-1、上界+1 并重算校验和，断言反序列化
  一律拒绝；对 16 MiB 文件上限，构造 0 字节与略超 16 MiB 的文件断言被 `loadFile` 安全拒绝。

#### 缺口 G6：v5→v6 迁移的失败回滚路径未测试（严重程度：中低）

S6 只覆盖三条**成功**迁移路径。`migrateV5File`/`copyFileAtomically` 中存在「`.v5.bak` 已存在则拒绝
覆盖」「v6 临时档磁盘复解析失败则删除临时档与归档、主档保持原 v5」等回滚点，均无用例。
- **建议补充用例**：先对一个 v5 槽位成功迁移一次（留下 `.v5.bak`），再把主档还原为 v5 后第二次加载，
  断言因归档已存在而迁移失败、主档不被破坏；并断言不会出现第二份归档覆盖第一份。

#### 缺口 G7：`.bak` 只保留一代的轮换语义未验证（严重程度：低）

`writeAndVerify` 每次保存都用当前主档覆盖旧 `.bak`，即备份只保留「上一版」。S5 只连续保存两次，
未验证第三次保存后更早版本确实不可恢复。
- **建议补充用例**：同一槽位依次保存 A、B、C 三个状态，写坏 C 主档后 `load`，断言恢复出的是 B 而非 A。

#### 缺口 G8：槽位文本解析 `parseSlot` / `slotName` 无单元测试（严重程度：低）

`parseSlot` 支持数字、英文、中文三类别名（`"1"`/`"slot1"`/`"存档1"`、`"auto"`/`"自动档"` 等），
非法输入返回 `nullopt`，属于纯函数但目前零覆盖。
- **建议补充用例**：参数化覆盖 7 个槽位的全部合法别名，以及 `"0"`、`"7"`、`"存档7"`、空串、
  带空格变体等非法输入，断言返回值与 `slotName` 显示文本。

#### 缺口 G9：写入前状态校验与内存复解析失败路径未测试（严重程度：低）

`SaveRepository::save` 在序列化前先 `validateState`，序列化后还做一次内存 `deserialize` 复解析。
「非法状态被 `save` 拒绝、不落盘」这一前置闸门没有直接用例（current_game 第 14 用例测的是
`replaceState`，不是 `save`）。
- **建议补充用例**：构造含非法枚举的 `GameState` 直接调用 `save`，断言返回 false 且目标槽位目录下
  不产生任何 `.sav`/`.tmp` 文件。

> 说明：并发写同一槽位、断电时刻磁盘级故障等场景依赖操作系统故障注入，超出当前纯单元测试框架能力，
> 记为「待确认」，不在上述用例建议范围内。

---

## 5. 测试框架说明

### 5.1 `tests/test_harness.hpp` 提供的能力

这是一个自研的极简注册式测试框架，全部内容在单个头文件中：

- **类型别名**：`test::TestFunction = std::function<void()>`，每个用例是一个无参无返回的可调用对象。
- **全局注册表**：`test::registry()` 返回函数内静态的
  `std::vector<std::pair<std::string, TestFunction>>&`，保存「用例名 → 测试函数」。
- **自动注册器**：`test::Registrar` 构造时把名称和函数 `emplace_back` 进注册表。
- **断言宏（仅一个）**：
  - `REQUIRE(expression)`：把表达式转 bool，失败时调用 `test::require`，抛出
    `std::runtime_error`，异常消息格式为 `文件:行号 REQUIRE failed: 表达式文本`。
  - 框架**没有**提供 `REQUIRE_EQ`、浮点比较、异常断言等宏；相等性、字符串包含等判断都由测试代码
    写成普通布尔表达式传给 `REQUIRE`（如 `REQUIRE(x == y)`、`REQUIRE(text.find(...) != npos)`）。
- **用例声明宏**：`TEST_CASE(name)` 借助 `__LINE__` 拼接（`MUD_TEST_JOIN`）生成唯一名的静态测试函数
  和静态 `Registrar` 对象，利用**静态初始化**在 `main` 运行前完成注册，因此各测试文件不需要手工维护
  用例清单。

### 5.2 `tests/test_main.cpp` 的运行方式

`main()` 顺序遍历 `test::registry()`：

1. 先打印 `[RUN ] 用例名`（标准输出设置了 `unitbuf` 逐次刷新，崩溃时也能看到最后运行的用例）；
2. 在 `try/catch` 中执行用例函数：正常返回打印 `[PASS]`；捕获 `std::exception` 打印
   `[FAIL] 名称: what()`；捕获其他异常打印 `[FAIL] 名称: unknown exception`，并累加失败计数；
3. 末尾打印 `通过数/总数 tests passed`；**全部通过进程返回 0，否则返回 1**（因此可直接被 CTest 判定）。

**注册顺序**由链接顺序决定，与 `CMakeLists.txt` 中 `tribe-formal-tests` 的源文件排列一致：
`test_main.cpp` → `current_game_tests.cpp`（15）→ `map_mission_tests.cpp`（7）→
`save_repository_tests.cpp`（7）→ `application_workflow_tests.cpp`（4），与第 3.5 节实际输出顺序吻合。
框架本身不支持跳过（skip）、不支持参数化、不支持 fixture 宏（各文件以匿名命名空间内的 RAII 夹具
和辅助函数替代，如存档测试的 `TemporarySaveDirectory`）。

### 5.3 `application_workflow_tests.cpp` 如何用依赖注入驱动完整游戏流程

应用层入口 `tribe::runApplication(std::istream& input, std::ostream& output,
std::filesystem::path saveRoot, ...)` 把三类外部依赖全部参数化，测试据此实现端到端脚本化：

1. **输入注入**：用 `std::istringstream` 预置一整段多行命令脚本。主流程用例依次喂入
   `seed QUICK 301 → assign 木材 2 → mission 木材 → move 苍林 → gather 木材 → move 营地 → settle →
   save 1 → back → 菜单选择 2（读档）→ 1（选槽）→ back → 4（退出）`，覆盖新局、地图任务、采集、
   结算、保存、返回主菜单、读档、再返回、退出的完整闭环。
2. **输出捕获**：用 `std::ostringstream` 接收全部界面文本，断言关键中文契约真实出现：
   「小队在燧火营地结算」「已保存到手动存档1」「已读取手动存档1」「燧火未熄，感谢游玩」。
3. **存档目录注入**：`saveRoot` 指向 `TemporarySaveDirectory` 在系统临时目录生成的唯一目录，
   RAII 析构清理，与玩家真实 `saves/` 完全隔离。流程跑完后，测试**再用 `SaveRepository` 重新打开
   该目录**，独立验证落盘状态：Slot1 可加载、阶段回到 `Managing`、木材较初始增加（`wood > 12`），
   形成「应用层写入 → 仓库层读回」的交叉验证。
4. **交互分支**：第二个用例用脚本 `save 1` 两次并在覆盖确认处喂入 `n`，验证覆盖前询问与取消语义
   （输出含「已取消覆盖存档」，且原存档仍为 Ready）。
5. 该文件后两个用例则对 `ConsoleUI` 做同样的流注入：分别以 80 列（教室基线）和 24 列（窄窗口）渲染，
   用本地 `validUtf8` 校验输出字节序列合法、关闭配色时不含 ESC（`0x1b`）、开启配色时含 ESC，
   验证终端安全与宽度自适应。

---

## 6. 失败与风险清单（按严重程度排序）

### 6.1 失败用例

**无。** 33 个用例全部通过，CTest 1 个测试项通过，无失败用例可列。

### 6.2 编译/格式警告

**无。** `/W4` 下编译警告 0 条；`clang-format --dry-run --Werror` 格式问题 0 条。

### 6.3 测试覆盖缺口（详见 4.3，按严重程度排序）

| 编号 | 缺口 | 严重程度 |
|---|---|---|
| G1 | 保存事务自身失败（临时档验证失败、替换失败回滚）未直接测试 | 中 |
| G2 | 主档 Unavailable 时「不回退备份」策略未测试 | 中 |
| G4 | 文件头拒绝分支不全：魔数篡改、长度不符、尾随字节、空文件未测 | 中 |
| G3 | `inspect()` 的 Corrupt 终态未断言 | 中低 |
| G5 | 集合计数/字段大小上限（roster、squads、chronicle、16 MiB 等）边界未测 | 中低 |
| G6 | v5→v6 迁移失败回滚（归档已存在拒绝覆盖等）未测 | 中低 |
| G7 | `.bak` 只保留一代的轮换语义未验证 | 低 |
| G8 | `parseSlot`/`slotName` 别名解析无单元测试 | 低 |
| G9 | `save` 前置状态校验闸门未直接测试 | 低 |

### 6.4 其他观察（非缺陷，记录备查）

1. **工具链可移植性**：`CMakeLists.txt` 中 clang-format/clang-tidy 的搜索提示路径写死为
   `C:/Program Files/Microsoft Visual Studio/18/Community/...`，当 VS 安装在其他盘（本机即 `D:\vs`）
   时 `find_program` 找不到，需要命令行手动指定。建议改为通过 `vswhere` 或
   `CMAKE_CXX_COMPILER` 所在工具链相对路径推导 LLVM 工具位置。
2. **CTest 粒度**：`add_test` 只注册了一个整体测试项，CI 面板无法直接看到 33 个用例各自的通过情况，
   失败时需要展开 `--output-on-failure` 日志定位。
3. 并发写、物理断电等故障场景「待确认」，当前测试体系不具备故障注入能力。

---

## 7. 结论与后续建议

**总体结论**：在 commit `e4a8347` 上，Debug 配置以 MSVC 19.51 `/W4 /permissive- /utf-8` 一次构建成功，
零警告零错误；33 个单元/工作流用例全部通过（通过率 100%），clang-format 检查零问题。存档子系统的
正常存取、v5→v6 迁移、`.bak`/`.tmp` 恢复、篡改拒绝、UTF-8 安全、七槽管理六条主干路径均有真实用例
覆盖且运行通过；但如 4.3 节所述，**故障分支与边界值覆盖并不完整，不能称为「覆盖完整」**。

后续建议（按优先级）：

1. **优先补齐 G1/G2/G4 三类故障路径用例**：保存事务失败回滚、Unavailable 不回退策略、文件头四类
   畸形拒绝。这些都是存档可靠性的核心防线，且都可在现有临时目录夹具上低成本构造。
2. **建立损坏存档参数化夹具**：以真实 v6 字节为母本，通过偏移改写 + 重算/不重算校验和的方式批量
   生成魔数、版本、长度、校验和、尾随字节、越界计数等畸形样本（覆盖 G4/G5），避免每个用例重复
   手写字节操作。
3. **补齐迁移回滚与备份轮换语义测试（G6/G7）**：重点验证 `.v5.bak` 已存在时拒绝覆盖、多次保存后
   `.bak` 只保留一代，防止未来重构事务层时悄悄改变恢复语义。
4. **为纯函数工具补充轻量用例（G8/G9）**：`parseSlot`/`slotName` 与 `save` 的前置校验闸门实现简单，
   用例成本低、能显著提高回归保护。
5. **细化 CTest 注册粒度**：可考虑让测试可执行文件支持按用例名过滤/分片运行，并在 CMake 中为关键
   套件注册独立 CTest 项，便于 CI 定位与并行。
6. **修复 clang-format 查找路径的可移植性**：改用 vswhere 或工具链相对路径定位 LLVM 工具，避免在
   非默认盘安装 VS 的机器上 format-check/tidy 目标静默缺失。

---

*报告生成方式：本机真实执行 CMake 配置、Ninja 构建、CTest、可执行文件直跑与 clang-format 检查后整理；
本次改动仅新增本文档，未修改任何 `.cpp`/`.hpp`/`CMakeLists.txt` 或既有测试代码。*
