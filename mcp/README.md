# Cube Explorer MCP

本适配器使用官方 [MCP Python SDK 2.3.0](https://github.com/modelcontextprotocol/python-sdk/tree/v2.3.0)，通过 stdio 提供九个工具。公式、步数、旋转次数、求解终止状态和独立验证结果全部来自常驻 C++ 引擎。Python 只传递结构化参数和结果，不求解、不修改公式、不从显示文字重新计步。

## 连接

交付目录 `mcp` 中的 `CubeExplorerMcp.exe` 必须与其 `_internal` 目录一起保留。默认查找同目录或上一级目录的 `CubeExplorerEngine.exe`；引擎需要交付目录中的 Qt/GCC DLL 和 `tables/original` 数据。下面是支持 stdio 的 MCP 客户端常用配置格式，具体配置入口以客户端为准：

```json
{
  "mcpServers": {
    "cube-explorer": {
      "command": "D:\\Cube\\cube explorer\\new version\\dist\\CubeExplorerModern-0.3.2\\mcp\\CubeExplorerMcp.exe",
      "args": [
        "--workspace",
        "D:\\Cube\\cube explorer\\new version\\dist\\CubeExplorerModern-0.3.2\\data"
      ]
    }
  }
}
```

可选启动参数：`--engine EXE路径`、`--tables 表目录`、`--workspace 结果目录`、`--request-timeout 秒数`。后者只约束通信响应时间，默认 30 秒，与搜索预算 `time_limit_ms` 不同。源码启动时使用本目录 `.venv/Scripts/python.exe server.py`。EXE 使用控制台子系统保留 stdin/stdout；由 MCP 客户端启动，不要双击后向标准输入粘贴普通公式。标准输出只包含 MCP JSON-RPC 消息。

## 工具和返回值

| 工具 | 参数 | 用途 |
| --- | --- | --- |
| `get_capabilities` | 无 | 查询引擎模式、动作、参数、限制 |
| `validate_cube` | `request: SearchSpec` | 检查输入和选项，不搜索 |
| `apply_moves` | `request: SearchSpec`, `moves: string` | 本地模拟公式，返回魔方、步数和旋转次数 |
| `submit_solve` | `items: SearchSpec[]`, `parallelism: 1..16` | 提交 1..10000 项批量任务，立即返回 `job_id` |
| `validate_templates` | `request: TemplateSearchSpec` | 检查原图案编辑器的模板约束和预算，不枚举 |
| `submit_templates` | `items: TemplateSearchSpec[]`, `parallelism: 1..16` | 批量枚举合法匹配魔方，可同时计算真实生成公式 |
| `get_job` | `job_id` | 立即取得任务和每项状态 |
| `get_results` | `job_id`, `cursor=0`, `limit=100` | 取得已产生的结果；每页 1..1000 条 |
| `cancel_job` | `job_id`, 可选 `item_id` | 取消整批或一项，保留已完成结果 |

每个工具同时返回 `structuredContent` 和内容一致的 JSON 文本，发布输入和输出 JSON Schema。成功封装为 `{"id":"mcp-...","ok":true,"result":{...}}`；引擎错误为 `{"id":"...","ok":false,"error":{"code":"...","message":"..."}}`，同时设置 MCP `isError=true`。通信/进程错误使用 `engine_unavailable`。Schema 错误由 SDK 作为 MCP 工具错误返回。

批量结果 `result.results[]` 每行含 `sequence`、`item_id`、`solution`。求解任务的 `solution` 包含原始 `formula`、`moves`、每个动作的 `actions`、算法给出的 `turns`、`rotations`、`verified`、`terminal` 和 `terminal_facelets`；模板任务返回 `kind="cube_state"` 的魔方状态，结构见下文。使用 `next_cursor` 翻页；运行中空页不等于结束。`get_job` 的 `done` 和各项 `state/stop_reason/summary` 区分完成、搜索上限、取消及无效输入。搜索被限制后仍可能有有效结果，不代表无解或已经完整枚举。

任务属于当前引擎进程。MCP 连接关闭时取消工作并关闭引擎；重连不会恢复内存任务。引擎将请求、状态及结果保存到 `--workspace/jobs` 中供审阅，适配器不把进程退出伪装为新任务。每条结果先成功写入并刷新 JSONL，随后才发布可分页的序号；分页直接读取该文件。内存仅保留每条结果的 8 字节文件偏移，不再重复保存全部公式 JSON。无限枚举仍会持续增加结果文件和偏移索引，客户端可随时取消，已写入结果保留。引擎意外退出后明确报错，不能继续用旧 `job_id` 查询。通信超时不能确定提交是否成功，客户端应检查状态，避免盲目重复提交。

## SearchSpec 全部参数

未显式传入的字段保持缺省，由引擎决定；适配器不会把公式模式开关注入常规求解。

| 字段 | 类型 / 当前引擎默认值 | 说明 |
| --- | --- | --- |
| `id` | 可选字符串 | 批次内唯一项目标识；不提供则引擎使用序号 |
| `mode` | 必填 `two_phase` / `optimal` / `formula` | 常规二阶段 / 真实最优 HTM / 公式枚举；界面仍是两种方法，optimal 对应方法一的“最优”选项 |
| `direction` | `solve` / `generate`，默认 `solve` | 求解到目标 / 从复原状态生成目标 |
| `scramble` | 字符串 | 从复原魔方施加的公式；空串表示复原 |
| `facelets` | 54 字符 | URFDLB 六面顺序；`?` 未知、小写仅块身份、`@` 仅朝向约束用于公式模式 |
| `pattern` | 见下文 | 直接输入 cubie 数组；与 `scramble/facelets` 三选一 |
| `center_ori` | 长度 6，元素 -1..3 | 可选中心方向约束，顺序 URFDLB；只用于公式模式 |
| `move_symbols` | 字符串，公式默认 `URFDLB` | 每字母启用 1/2/3 次转动；允许 `URFDLBurfdlbMESxyz` |
| `allowed_moves` | 动作字符串数组 | 精确启用如 `R`、`R2`、`r'`、`x`；与 `move_symbols` 二选一 |
| `min_turns` | 公式默认 0 | 枚举最小计费转动数；到达目标立即停止，不为凑步数绕行 |
| `max_turns` | 二阶段 / 最优 30，公式 -1 | 二阶段 / 最优 0..60；公式 -1 表示不设深度上限，0 只搜零步，有限值 0..2147483647 |
| `target_turns` | 仅二阶段，默认 20 | 达到该长度即停止；不是最优证明 |
| `max_phase1_turns` | 仅二阶段，默认 20 | 第一阶段上限 0..30 |
| `use_triple_search` | 仅二阶段，默认 `false` | 原版可选 UD/RL/FB 三轴二阶段搜索，共享整项时间和节点预算；输出精确目标不变 |
| `initial_solution` | 仅最优，可选公式字符串 | 已有合法候选上界；使用当前请求 solve / generate 的方向，最多 60 个外层动作；引擎先独立验证 |
| `excluded_solutions` | 仅最优，可选公式字符串数组 | 已展示公式，使用当前请求方向，至多 10000 条；用于寻找下一公式，不据此宣称最优 |
| `max_results` | 公式默认 0 | 0 为不设条数上限，最大 1000000 |
| `time_limit_ms` | 二阶段 5000 / 最优 30000 / 公式 0 | 0 为不限时，最大 86400000；公式包括剪枝准备时间，二阶段 / 最优不含共享原表首次加载 |
| `node_limit` | 所有求解模式默认 0 | 0 为不设节点上限 |
| `modulo_u` | 公式默认 `true` | 接受只差 U/U'/U2 的目标，不追加末尾 U |
| `remove_u_symmetric_prefix` | 公式默认 `true` | 对适用 U 对称输入在搜索根部跳过冗余 U |
| `reject_alternating_triples` | 公式默认 `true` | 在搜索中禁止 R r R / r R r 类连续三项 |
| `deduplicate_commuting` | 公式默认 `true` | 交换相邻可交换动作所得的重复公式只保留一份 |
| `heuristic` | `none/standard/strong`，默认 `standard` | 公式搜索下界；不影响目标和动作计数 |
| `memoize_dead_ends` | 公式默认 `true` | 缓存完整搜完且没有目标的子树；不缓存有解前缀、不删不同成功公式 |
| `quotient_free_rotations` | 公式默认 `true` | 仅在引擎对动作集合完成机械证明后合并免费旋转的无解上下文；可设 false 对照，实际启用见 `summary.free_rotation_quotient` |

`two_phase` 与 `optimal` 只支持完整普通三阶的精确复原及 18 个外层动作 HTM；公式开关、中心方向或不完整约束会明确拒绝。最优特有参数不能传入另两种模式。公式模式的每个单层、宽转或中层动作计一步，包括半转；`x/y/z` 计 0 步、单独计旋转次数。正常需要的开头 U 可能在 `formula` 文本中带中括号，但 `moves/actions` 保留实际 U，步数来自搜索。`formula/generate` 允许目标相差 U，目标集合直接在算法中处理。

公式枚举默认不设深度、条数、时间或节点预算，可用 `cancel_job` 终止；需要有限任务时显式指定预算。`get_job.items[].progress` 包含 `depth`、`depth_started`、累计 `depth_start_count`、`results` 和节点数，每一新搜索深度均强制上报。轮询可能错过瞬时的 `depth_started=true`，可通过累计计数观察已开启多少层。每条公式的 `optimality_proven` 仅在算法证明其为最短层时为 true；继续枚举的更长公式不会标最优，`min_turns` 跳过未证明的较短层也不会伪造证明。项目 summary 的 `results` 是已发布条数，核心以流式回调输出，不需在内存累积公式列表。

普通二阶段 summary 的 `search_axes` 为 1 或 3，每条结果的 `search_axis` 为 0=UD、1=RL、2=FB（输入逻辑中心坐标）。三轴返回实时改进的真实候选，共享 `target_turns` 终止条件；并行调度可能使两次搜索返回不同的合法公式。`use_triple_search` 不能用于 `optimal/formula`。原始剪枝表在进程内共享并常驻，相同规范路径只加载一次。性能与原算法迁移说明见 `docs/twophase-migration.md`，其中性能对照基线是本轮修改前的新版 C++，不是原版 Delphi 的实测。

`optimal` 使用原始剪枝表的 UD / RL / FB 三轴 IDA*。每次返回一条新公式；读取每条结果和项目 summary 中的 `optimality_proven` 判断是否已证明最短。`solved=true` 仅表示有合法公式：超时、取消或节点预算可能保留未证明最优的 `initial_solution`。`excluded_solutions` 排除较短公式后找到的更长公式也不会被标最优。最优的 `search_complete=true` 表示给定深度范围的规范路径已穷尽，并不等同于 `optimality_proven`。

例如 generate 请求的 `initial_solution` 应从复原态生成输入目标；`excluded_solutions` 可以直接使用之前同方向返回的 `formula`。引擎会完成所需方向变换，不需要客户端手动取逆。核心说明见 `docs/optimal-migration.md`。

`pattern` 必须含 `cp[8],co[8],ep[12],eo[12]`；-1 表示该位置的身份或朝向未知。角块顺序 `URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB`，棱块顺序 `UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR`。身份分别为 0..7 / 0..11；朝向分别为 0..2 / 0..1。可附加 `frame:0..23` 和 `center_ori[6]`。建议通过 `validate_cube/apply_moves` 返回的 `pattern` 继续运算，不猜测 frame 编号。

## 调用示例

常规求解：

```json
{"items":[{"id":"cube-1","mode":"two_phase","direction":"solve","scramble":"R U R' U'","target_turns":20,"time_limit_ms":5000}],"parallelism":1}
```

启用原版可选三轴搜索时，在该项目加入 `"use_triple_search":true`；批次 `parallelism` 控制同时执行的项目数，每个启用三轴的项目内部最多使用三个搜索线程。

宽转与免费整体旋转的公式枚举（只开启需要的动作会减少搜索规模）：

```json
{"items":[{"id":"formula-1","mode":"formula","scramble":"r U r'","move_symbols":"RUruxyz","min_turns":0,"max_turns":7,"max_results":20,"modulo_u":true,"time_limit_ms":10000,"node_limit":10000000}],"parallelism":1}
```

真实最优搜索，带先前候选上界；预算中止时保留候选但不会误报最优：

```json
{"items":[{"id":"optimal-1","mode":"optimal","scramble":"R U F2","initial_solution":"F2 U' R'","max_turns":20,"time_limit_ms":30000}],"parallelism":1}
```

寻找下一条生成公式，可把之前返回的生成式直接放入排除数组：

```json
{"items":[{"mode":"optimal","direction":"generate","scramble":"R U R' U'","excluded_solutions":["R U R' U'"],"max_turns":10,"time_limit_ms":30000}],"parallelism":1}
```

获取结果：

```json
{"job_id":"提交返回的job_id","cursor":0,"limit":100}
```

收到 `next_cursor` 后把它作为下次 `cursor`。也可批量混合二阶段、最优、公式求解、生成及不同限制；语义非法的一个项目不妨碍其他合法项目，形状不符合 MCP Schema 的请求会在提交前整体拒绝。

## 原图案编辑器：TemplateSearchSpec

`submit_templates` 对应原版图案编辑器的五个 3×3 面模板，使用独立的 `mode="template"` 原生枚举器。它枚举完整合法魔方状态；不把匹配状态标为“0 步求解公式”。这与 `formula` 模式的部分魔方公式搜索是不同操作。

| 字段 | 默认值 / 类型 | 含义 |
| --- | --- | --- |
| `id` | 可选字符串 | 批次内唯一标识 |
| `mode` | `template` | MCP 工具自动补入；原生 JSON 请求必须提供 |
| `templates` | 必填，5×9 整数数组 | 每模板按行排列，标签 0..5 是六种相等关系类别，不是固定 URFDLB 颜色 |
| `allowed_templates` | 6×5 布尔数组，默认全 true | 行依次 U/R/F/D/L/B，列对应五模板；面必须匹配其某个勾选模板 |
| `continuous` | false | 使用原版 `IsContinuous` 相邻面连续关系约束 |
| `max_results` | 100 | 匹配状态条数上限；0 不限，最大 1000000 |
| `time_limit_ms` | 30000 | 枚举与可选生成搜索共用总时限；0 不限，最大 86400000；首次共享二阶段表加载不计入 |
| `node_limit` | 20000000 | 模板枚举节点上限；0 不限，不包含独立生成搜索的节点 |
| `find_generators` | false | 为每个匹配状态运行真实二阶段算法，计算从复原态到该状态的精确生成公式 |
| `generator_options` | 可选对象 | 仅在 `find_generators=true` 时使用；每个匹配状态独立应用下述预算 |

`generator_options` 字段与常规二阶段相同：`max_turns=30`（0..60）、`target_turns=20`（0..60）、`max_phase1_turns=20`（0..30）、`time_limit_ms=5000`（0 不限）、`node_limit=0`（0 不限）、`use_triple_search=false`。每次生成的时限还受模板任务剩余总时间限制；模板的节点预算和生成节点预算分别统计。三轴诊断分别在 `generator_search.search_axes` 和 `generator.search_axis` 返回。

标签 5 是原灰色：整个模板全 5 时该模板未使用；混合模板中的 5 仍为第六种相等关系类别。每个面接受模板的八种平面旋转/镜像变体，当前不做全魔方同构或逆置换去重。全灰请求、或某面没有可用模板，是合法的空约束结果，会穷尽并返回零条，不是参数错误。`validate_templates` 只验证参数合法性，不能保证存在匹配魔方。

例如同时允许纯色面和“一行与其余贴纸不同”的面，枚举前三个状态并计算生成公式：

```json
{
  "items": [{
    "id": "face-patterns",
    "templates": [
      [0,0,0,0,0,0,0,0,0],
      [1,1,1,0,0,0,0,0,0],
      [5,5,5,5,5,5,5,5,5],
      [5,5,5,5,5,5,5,5,5],
      [5,5,5,5,5,5,5,5,5]
    ],
    "continuous": false,
    "max_results": 3,
    "find_generators": true,
    "generator_options": {"max_turns": 30, "time_limit_ms": 5000}
  }],
  "parallelism": 1
}
```

返回后继续调用同一套 `get_job`、`get_results`、`cancel_job`；取消和分页行为与公式任务一致。每条 `solution` 包含 `kind="cube_state"`、完整 `pattern`、54 字符 `facelets`、`verified=true` 和 `continuous`，根层没有 `formula/turns/moves/actions`。该状态已通过核心的独立合法性和模板约束验证，可直接把 `pattern` 交给 `submit_solve` 继续求解。

开启 `find_generators` 后，`generator_search` 报告实际搜索的 `solved/stop_reason/nodes/elapsed_ms`；仅找到并独立回放验证了公式时，才附 `generator`，其公式、动作、步数、阶段长度与常规生成结果相同。达到生成预算或被取消仍保留已匹配魔方，缺少 `generator` 表示未取得公式，不能当成零步。确实匹配复原魔方时，真实生成算法可以返回空公式和零步。项目 summary 中 `matched_states` 是状态数，`generated_states` 是取得生成公式的状态数，`search_complete` 仅表示模板枚举已穷尽。

原生 `submit` 支持在一个批次混合四种模式；MCP 的 `submit_solve` 保持原有三种求解模式 schema，模板枚举通过独立的 `submit_templates` 提交，方便客户端识别返回类型。

## 构建和验证

所有依赖固定在 `requirements.lock`，虚拟环境位于 `mcp/.venv`。`scripts/build-mcp.ps1 -InstallDependencies` 按锁定版本安装并创建 console onedir EXE；已有环境可省略该开关。缓存与临时文件由 `scripts/environment.ps1` 定位到本工作区。

```powershell
. .\scripts\environment.ps1
& .\mcp\.venv\Scripts\python.exe .\tests\mcp_bridge_tests.py
& .\mcp\.venv\Scripts\python.exe .\tests\mcp_integration_tests.py
& .\scripts\build-mcp.ps1
& .\mcp\.venv\Scripts\python.exe .\tests\mcp_integration_tests.py --server-exe .\mcp\dist\CubeExplorerMcp\CubeExplorerMcp.exe
```

桥接测试覆盖乱序回复、并发 ID、超时迟到回复、调用取消、坏协议及引擎异常退出。最终验收使用真实官方 SDK stdio 客户端连接实际 C++ 引擎，包含常规精确目标、真实三轴、模 U 零步、宽转、生成、免费旋转、无限枚举流式结果与取消、最短层证明、批量、磁盘分页、错误隔离和取消。官方参考：[stdio 客户端](https://py.sdk.modelcontextprotocol.io/client/transports/)、[结构化输出](https://py.sdk.modelcontextprotocol.io/servers/structured-output/)。

另有 `tests/mcp_legacy_protocol_tests.py` 检查旧客户端协议 2024-11-05、2025-03-26、2025-06-18 的实际握手、工具发现及调用。打包目录 `licenses` 保留 Python 和所有锁定依赖的随附许可，包括旧格式直接位于 dist-info 根目录的许可、包内 vendored 组件的许可和 NOTICE；`manifest.json` 记录版本、复制路径和 SHA256。`_internal/*.dist-info` 保留运行依赖元数据。
