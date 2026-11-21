# Core validation evidence

## 0.3.1 最终验证与便携交付 · 2026-11-13

本轮完成紧凑布局、白顶绿前默认色、参考项目透视 3D、原图标、公式窗口最小化和普通求解展开居中；同时对照原 Pascal 源码改进两种算法。原 `raw` 的 136 个文件仍与最初 SHA-256 完全一致。所有新文件和产物位于 `new version`。

### 统一构建与实际交付程序

Release **14/14 组 CTest 全部通过**，详 `logs/build-v5-final.txt`、`logs/core-v5-verbose.txt`。包括模型 29,807、公式 90,827、独立交叉模型 96,061、跨核心 1,172、最优 55,488、用户回归 621、新几何差分 12,577、编辑 503、模板核心 1,950、画布 24,606、原式染色区 484、公式窗口 146、结果行 83 项，以及常规二阶段测试。结果行另外有带截图的 91 项检查，截图增加的检查不重复累计到统一 CTest。

主窗口 140 项通过。公式窗口专项实际执行最小化和恢复，并确认不意外启动搜索；画布覆盖 24 个朝向、多相机角度、54 种动作的命中与颜色，以及极端长宽比。原版图标按原字节复制并嵌入 Qt 与 Windows PE 资源，Windows 实际提取关联图标成功。

同一发布目录在仅含 Windows/System32、Windows 的 PATH 下，GUI 140 项、Engine 399 项、实际 MCP EXE 375 项通过；MCP 使用默认引擎发现，不传入开发引擎或表路径。额外原生 Windows 窗口 141 项（含实际屏幕居中）、150% DPI 140 项通过，三次 stderr 均为空。三种旧 MCP 协议握手也通过。证据为 `ui-v5-{release,native,dpi150}.txt`、`engine-v5-release.json`、`mcp-v5-release.json`、`mcp-v5-legacy.jsonl`。API 检查数量包含轮询，随调度可能略有变化；这些是本机便携环境验证，不冒充另一台机器的实测。

### 算法调整与性能

常规二阶段恢复原阶段二同面幂次剪枝，减少无效阶段二入口的精确距离还原。三轮各 500 个固定输入、目标 20 步：单轴累计时间中位数 7.860→6.025 秒，减少 23.35%；可选三轴 0.622→0.475 秒，减少 23.62%。单轴 1500 对输入的全部首候选、中间候选和最终公式逐字一致。首次出候选没有相同幅度提升，主要收益在继续寻找更短公式。

公式模式提前结束已经足够排除分支的下界计算，恢复重叠五棱分组，减少不可达朝向表状态及热循环的重复变换工作；逐条独立几何验证保留，固定映射改为预计算。三个用户用例的三轮冷进程中位数分别为 338→176 ms、1192→529 ms、3612→2327 ms；连续 10000 条宽转输出 69→22 ms，含免费旋转为 97→25 ms。30 次搜索的输出公式、计步和最优证明标记逐字节一致。

上述对比均为**冻结的 C++ 0.3 与 C++ 0.3.1**，不是 Delphi EXE 性能对比。详 [本轮算法报告](ALGORITHM-CHANGES-0.3.1.md)、[公式性能报告](FORMULA-PERFORMANCE-0.3.1.md)、[二阶段迁移说明](twophase-migration.md)。

### 用户三个回归的结果

- 第一例 RUr：完整到 11 步无结果；指定的错误公式通过真实目标检查拒绝，没有加入字符串黑名单。
- 第二例 RUFr：完整到 11 步输出 5 条，无 U 开头；实际动作、步数、终态正确。
- 第三例 RUD：完整到 16 步输出 18 条，其中 15 步 2 条、16 步 16 条；无末尾 U，不越过更早已达到的模 U 目标，真实计步一致。

新增几何差分包含 4378 个实际接受案例，与上一版逐步物理旋转和解码对照；其他原有回归继续检查宽转、标记、免费旋转、浅层完整解集、取消和无限枚举。验证没有变成抽样或只验证完整魔方；每条发布公式仍通过独立面片回放。有限样例验证不代表穷尽所有可能输入。

### 交付

运行 `dist/CubeExplorerModern-0.3.1/CubeExplorerModern.exe`，保留整个目录。完整包 `dist/CubeExplorerModern-0.3.1-win64.zip`，包含中文帮助、MCP EXE 与配置、对应应用源码、Qt 对应源码、许可、验证日志和文件校验清单。原 0.3.0 目录和用户数据保留。`scripts/verify_release.py` 验证文件 SHA-256、两个 ZIP 的 CRC、源码完整性、帮助链接及外部压缩包 SHA-256。

以下为历史版本记录，不作为 0.3.1 的实测结果。

## 历史记录：0.3 最终验证与便携交付 · 2026-11-13

本轮按最新反馈移除图案编辑器 GUI，更新字体、RGB、标记、布局、主题快捷键和比例缩放。公式搜索默认无限、按层追加输出，保留算法计步；普通二阶段修复表重复初始化并接入真实可选三轴。核心验证后再接 GUI/MCP，原 `raw` 的 136 个文件 SHA-256 仍与最初清单一致。

统一 Release 的 **13/13 组 CTest 全部通过**，耗时 17.39 秒；`logs/build-v4-final.txt` 与 `logs/core-v4-verbose.txt` 记录同一次运行。包含用户回归 621 项、模型 29807、公式 90827、交叉模型 96061、跨核心 1172、最优 55488、编辑 503、模板核心 1950、画布 1288、原式染色区 484、公式窗口 143、结果行 83 项，另有二阶段测试。可选截图检查会使部分 UI 专项计数增加；不会把历史模板 GUI 测试计入当前版本。

### 用户指定的三个回归

ZBLS 基础状态固定为 `?@?@U@?@????RRRRRR???FFFFFFDDDDDDDDD???LLLLLL???BBBBBB`：先忽略所有含 U 色块，保留 F2L，再标记四个 U 棱朝向。逻辑 U 为黄、D 为白，使用真实几何打乱和独立贴纸回放。

| 用例 | 结果 |
|---|---|
| `R U' R' U F' U L' U2 L F U`，RUr | 指定的 `U' r' U2 R r U R' U' R' U2 r'` 不满足目标，求解/生成四种模 U 残差均拒绝；没有字符串黑名单。完整到 11 步无结果 |
| `M'URU'R'U'RU'r'U2rU'r'`，RUFr | 核心确认根 U 对称；完整到 11 步得 5 条，无 U 开头，真实计步和终态均正确 |
| `B F' L2 U L2 U' L2 B' U' F`，RUD | 完整到 16 步：15 步 2 条、16 步 16 条，共 18 条；无末尾 U，不越过更早目标，计步正确 |

证据为 `tests/user_regression_tests.cpp`、`logs/user-regression-tests.txt`。同时检查无限搜索到第 300 层取消、流式输出、最优证明，以及关闭/开启去重和剪枝的浅层等价类集合一致。

RUD16 的 Standard 和 Strong 在独立冷进程中得到同一 18 条完整公式集合，分别 4.336 秒/19555176 节点、4.277 秒/15872760 节点。保持 Standard 默认；日志 `user-rud16-{standard,strong,parity}.txt`。这些是本机固定输入的时间，不是所有输入的速度保证。

### 界面、API 与便携运行

主窗口 136 项真实操作检查通过；公式窗口 143 项覆盖不限预算、可选排初始未选、真实宽转、最优证明、分层追加、取消/重启、可取消背压和四档缩放。额外恢复隐藏中心方向的 CEX 保存/加载，防止加载其他魔方时串用旧方向。

统一原生 Engine 413 项、MCP 源码端 372 项、桥接/schema 13 项通过。便携目录在只含 Windows/System32 与 Windows 的 PATH 下，GUI 136 项、Engine 419 项、实际 MCP EXE 375 项通过，MCP 未指定开发引擎/表路径，默认发现交付文件。API检查数包含轮询，随调度略有浮动。

证据：`logs/ui-v4-release.txt`、`engine-v4-release.json`、`mcp-v4-release.json`。原生 Windows GUI 和额外 150% DPI 又分别通过 136 项，日志 `ui-v4-native.txt` / `ui-v4-dpi150.txt`，三次 GUI stderr 均为空。三种旧 MCP 协议握手记录在 `mcp-v4-legacy.jsonl`。截图来自实际 Qt 程序，非静态样稿。便携验证在本机完成，不冒充另一台机器实测。

另外通过 `QTextLayout` / `QGlyphRun::rawFont()` 检查真实主窗口控件的字形字体：Windows 和 offscreen 的汉字均为 Microsoft YaHei，英文为 Calibri，没有落到宋体；证据 `font-probe-main-{windows,offscreen}.json` 随包放在 `docs/validation`。不是只检查所请求的字体名称。

### 性能原因与数据

原新版每次创建二阶段求解器都会重读、校验与初始化约 66 MB 表，约 136–140 ms。按路径共享只读表后，首次约 141 ms，后续 0.043–0.101 ms。同一 500 状态、目标 20 步：本轮修改前新版单轴总搜索 10.722 秒，优化后单轴 7.673 秒（全部公式逐条相同），可选三轴 0.653 秒（全部独立验证）。保留独立回放，未为速度削减验证。

这是**修改前后新版**的比较，未完成与原版 Delphi EXE 的内核时间对比。详见 `twophase-migration.md`；公式剪枝与无历史集合去重证明见 `pattern-migration.md`。

### 交付

运行 `dist/CubeExplorerModern-0.3.0/CubeExplorerModern.exe`。完整包 `dist/CubeExplorerModern-0.3.0-win64.zip`；保留整个目录，无需另装 Qt/Python。旧交付 EXE 被占用，本版使用独立版本目录，未覆盖旧程序和用户数据。

包内包含中文帮助、MCP 配置、完整对应源码、Qt 对应源码、许可及校验清单。`scripts/verify_release.py` 检查清单、ZIP CRC、源码完整性和帮助链接；外部 `.zip.sha256` 校验压缩包。基础菜单按源码核对，已排除的相机、深层对称等范围见 `UI-MIGRATION.md`。

以下保留旧版证据，仅用于历史追溯。

## 历史记录：0.2 验证与交付

该历史版本为 0.2.0：主窗口恢复原式编辑区及一个共同输出列表，公式模式使用独立弹窗且只有 Add 才传入主窗口，新增真实最优 HTM 搜索并迁移真实图案编辑器。最新统一 Release 的 13/13 组 CTest 全部通过，耗时 8.10 秒；完整记录为 `logs/build-v3-final.txt`。便携目录中的 GUI、Engine 和 MCP EXE 也已使用仅含 Windows/System32 与 Windows 的 PATH 运行通过。

| 专项 | 最近已记录的检查 | 证据及说明 |
| --- | ---: | --- |
| `model_tests` | 29,807 | 独立物理模型；先前核心记录 `logs/core-final-verbose.txt` |
| `twophase_tests` | 另有 500 输入专项 | 合法坐标与打乱样本、精确复原和取消；详见下文历史核心记录 |
| `optimal_tests` | 55,488 | 三轴下界、独立 BFS 真距离、候选与证明区分；`logs/optimal-v2-tests.txt` |
| `pattern_tests` | 90,827 | 宽转、模 U、枚举与剪枝；`logs/pattern-tests-quotient.txt` |
| `pattern_model_cross_tests` | 96,061 | 独立贴纸/部分图案交叉验证；先前核心记录 |
| `integration_tests` | 1,172 | 跨核心动作、目标和计数；`logs/core-integration-first.txt` |
| `editor_tests` | 503 | 原 Check 行为、九类重置、非法编辑暂态真实转动、旧公式与 48 贴纸输入 |
| `cube_canvas_tests` | 1,270 | 正方形 2D、等比例 3D、命中与相机隔离；`logs/cube-canvas-v2.txt` |
| `original_editor_view_tests` | 368 | 原式编辑图、画笔色和 AutoFix 控件定位与交互；`logs/original-editor-view-tests.txt` |
| `formula_window_tests` | 58 | 610×840 布局、竖排开关、真实宽转、只缓冲/显式 Add、停止重启和安全关闭 |
| `template_search_tests` | 1,950 | 独立完整模板集合、标签/灰色、D4、连续约束、合法性与预算；`logs/template-search-tests.txt` |
| `template_editor_tests` | 222 | 原式五模板页、类型选择、面/全部三态、真实枚举、取消重启及关闭 |
| `result_row_interaction_tests` | 51 | 原式结果行的子控件选择、传送及实际搜索交互 |

当前主窗口 smoke 已记录 110 项检查通过（`logs/ui-v3-smoke.txt`），包括鼠标与修饰键、独立弹窗不自动展开/添加、重复 Add、普通与真实最优搜索、工作区/TXT 往返、错误目标拒绝及失败导出保全原文件；新增图案编辑器状态到真实生成公式、Escape 取消加载及原五打乱入口均已走实际调用链。弹窗专项截图为 `logs/formula-window-v3.png`、`logs/formula-window-v3-night.png`、`logs/formula-window-v3-settings.png`。

同轮原生引擎通过 350 项，GUI smoke 通过 110 项，MCP 源服务真实客户端集成通过 315 项（轮询会使计数浮动），Schema / 桥接 12 项；原始 136 文件 SHA-256 与初始清单一致。以上均以 `logs/build-v3-final.txt` 为当前完整证据。

便携运行记录：`logs/ui-v3-release.txt` 的 110 项 GUI 检查通过且 stderr 为空；`logs/engine-v3-release.json` 的 362 项真实引擎检查通过；`logs/mcp-v3-release.json` 的 312 项官方 SDK 客户端到实际 MCP EXE / 引擎检查通过，未指定引擎与表目录，默认发现发布目录中的文件。原生及 MCP 的轮询会使检查数量略有浮动。`logs/mcp-v3-legacy.jsonl` 另外记录三种旧协议握手通过。这是在本机移除开发 PATH 后的验证，不等同于另一台物理机器实测。

交付主程序为 `dist/CubeExplorerModern/CubeExplorerModern.exe`，完整便携包为 `dist/CubeExplorerModern-0.2.0-win64.zip`，须保留整个程序目录。包内 `SHA256SUMS.json` 记录文件校验值，外部同名 `.zip.sha256` 记录压缩包校验值；对应源码、中文帮助、算法说明、Qt 对应源码及依赖许可均随包提供。选定日志和真实窗口截图放在 `docs/validation`。早期 69 项嵌入式界面记录属于中间版本，不能替代本版的独立弹窗验收。

当前范围：普通二阶段不宣称全局最优，只有独立最优搜索完成证明才授予 `*`。原独立 Pattern Editor 的五模板/六抽象类真实枚举已恢复，保留基础单面 D4 及 Continuous；相机、深层对称界面、全魔方 48 同构/逆置换过滤、旧 HTTP 服务、普通 slice-mode 和专用大型中心表尚未迁移。有限测试不能证明所有输入的速度和正确性，输出仍逐条独立验证，预算中止仍明确报告未穷尽。

## 历史记录：0.1 最终交付验收 · 2026-11-12

本节及其后的核心性能/验证溯源保留当时的运行证据。下面的“最终”“发布”和便携运行结论均指 0.1；当前 0.3 交付结论见文首。

Release CTest 五组全部通过（约 6.5 秒）。物理模型 29,807 项、公式搜索 90,827 项、独立模式交叉验证 96,061 项、跨核心集成 1,172 项，四组合计 217,867 个检查点；另有普通二阶段专项 500 输入验证。

原生引擎最终 134 项真实进程检查通过。GUI 的按钮、特殊标记、开窗不搜索、停止 / 重启、普通求解与生成、回放均通过，截图已检查。官方 SDK 客户端到发布 MCP EXE / 实际引擎的最终 211 项检查通过（轮询会让计数略变），另有 9 个桥接 / Schema 测试，以及 2024-11-05、2025-03-26、2025-06-18 三种协议兼容检查。

发布 GUI、Engine 和 MCP 均在子进程 PATH 仅含 Windows/System32 与 Windows 的环境下运行成功。MCP 未指定引擎和表路径，正确发现发布包中的文件；服务端不依赖开发 Python 或 Qt。GUI stderr 为空，MCP 退出后无遗留引擎进程。这是本机独立运行验证，不等同于在另一台物理电脑测试过。

raw 的 136 个原文件仍与最初 SHA-256 一致。发布目录为 dist/CubeExplorerModern，压缩包为 dist/CubeExplorerModern-0.1.0-win64.zip，包含中文帮助、MCP 配置、对应应用源码、Qt 对应源码、许可与校验清单。

可复查记录：logs/build-final.txt、logs/core-final-verbose.txt、logs/portable-ui-smoke.txt、logs/mcp-release-integration.json、logs/mcp-release-legacy.jsonl、logs/pattern-bench-rotations-probe.csv 和 build/twophase-dev/validation-500.txt；选定实际输出随包放在 docs/validation。构建复现见 BUILD.md。

## 范围与实测性能

常规二阶段不宣称全局最优。相机、旧 HTTP 服务、原常规 slice-mode 与专用大型中心表未迁移；中心约束明确使用公式模式。预算中止不代表无解。有限测试不能保证所有输入的速度或正确性，因此每条结果仍做独立几何回放验证。

公式搜索允许所有外层、宽转及免费整体旋转、取第一条公式时，本机测得：

| 用例 | 步数 | 准备 / 总耗时 | 节点 |
| --- | ---: | ---: | ---: |
| 完整 Sune | 7 | 383 / 384 ms | 1,975 |
| Sune 顶层仅朝向、底层完整 | 7 | 24 / 32 ms | 29,896 |
| 完整 U-perm | 9 | 3 / 183 ms | 1,227,499 |

用例有缓存预热差异，不能外推为所有深层枚举的速度保证。关闭免费旋转时优化条件可能不成立。关闭缓存、完整缓存、朝向压缩 / 存在性预检的完整浅层输出顺序一致，含部分图案、中心方向及求解 / 生成两方向，证明及边界见 pattern-migration.md。


All commands run under the project directory after sourcing `scripts/environment.ps1`. This sets process-local PATH and moves tool temporary files/caches into the project. `scripts/verify-raw.ps1` checks the 136 original files against the initial SHA-256 manifest.

## Model and physical semantics

The original Pascal corner/edge six-face moves are transcribed explicitly. Search transitions combine those cubie primitives with an independently defined 24-orientation reference frame. Verification uses geometric rotation of the 54 stickers, without reading the cubie transition tables.

`model_tests`: 29,807 assertions passed in the first implementation. Coverage includes all original 12 sticker permutations, all 54 moves from 24 orientations, inverse/fourth-power identities, wide/slice/rotation identities, and a 20,000-action randomized physical replay. Modulo-U remains tied to the input logical U after rotations.

`pattern_model_cross_tests`: 96,061 assertions passed in the final implementation. Independent marker geometry checks four partial-cubie constraint modes (known, unknown, identity-only, orientation-only), all 54 actions, and 200 pattern text round trips including reference frames.

## Ordinary two-phase

Uses the original packed modulo-3 pruning tables and their exact 16-symmetry coordinate conventions. Initial 500 fixed-seed 25-move scrambles all produced solutions of at most 21 HTM moves, each independently replayed. This proves those tested solutions, not global optimality or exhaustive correctness.

The original binary is 5.14 while source is 5.15. Any direct comparison must label this. The original HTTP handler includes a fixed 300 ms sleep/poll; end-to-end HTTP response times are not solver-kernel timings.

Additional mixed-corpus validation passed 500 inputs (250 scramble walks and 250 independently sampled legal cubie states), shared-table parallelism, table-preparation and search cancellation, preserving a candidate on cancellation, and strict node/time budgets. Recorded high-resolution search wall time was 256.141 ms and batch wall time 261.584 ms for 8,895,256 search nodes in that run; these are local observations, not a comparison with the original executable.

The attempted original-5.14 comparison command (copy distribution into this workspace, start its EXE hidden, and query the local test port) was rejected by automatic approval review with `blocked by policy` and no more specific reason. It did not execute. No original-versus-new kernel speedup claim is made.

## Formula search release gate

The following gate passed before GUI and MCP integration, and passed again in the final Release:

- Empty/U/U2/U' states must terminate at zero turn cost; a logical R cannot become zero by rotating the view.
- Search output cost plus rotation count must equal actual move-token count, regardless of formatting.
- Wide moves and slices must have exact independently verified physical effects.
- Pruned versus unpruned shallow enumeration must agree by commutation equivalence class for solve and generate directions.
- Partial markers, U symmetry, alternating-triple rejection, allowed-move profiles, and zero-cost rotation closure must agree with their specified semantics.
- Cancellation/time/node/result limits must be reported distinctly from exhaustive search completion.

GUI and MCP integration followed this gate. Final portable-build evidence is recorded above.
