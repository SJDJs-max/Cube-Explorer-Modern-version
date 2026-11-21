# 原版界面与交互契约

本轮以 `../raw/CubeExplorer-master` 的 `RubikMain.pas/.dfm`、`FaceCube.pas`、`Incomp.pas/.dfm` 为只读基准。行号是原始文件的一基行号。用户最新布局要求优先于原版固定像素：右侧上部保留魔方与画笔色；下部为选择列、转动网格、重置列，随后是公式输入、busy bar、自定义颜色。初始左侧隐藏，方式一显示原式结果行，方式二弹出独立公式窗口。白天/夜晚切换和新增宽转是明确新增项。

## 主编辑器事件，不能替换成新习惯

来源 `RubikMain.pas:998`，`FaceletCube.Check` 来源 `FaceCube.pas:506`。

| 位置 / 手势 | 原版实际行为 |
| --- | --- |
| 非中心贴纸，左键 | 把当前画笔颜色写入所点的一个贴纸。 |
| 非中心贴纸，Shift+左键 | 写入“当前画笔颜色 + 忽略朝向”标记，即新版小写颜色。不是先读取该贴纸旧颜色。 |
| 非中心贴纸，Ctrl+左键 | 写入 `oriCol`，即仅朝向标记 `@`。 |
| Shift+Ctrl+左键 | Shift 分支优先，忽略朝向。 |
| 非中心贴纸，右键或中键 | 仅清除点中的单个贴纸。不会立即清除整个角/棱，也不调用自动修正。 |
| 中心贴纸，左键 | 从中心取色，更新当前画笔。不可改变中心颜色或直接旋转箭头。 |
| 中心贴纸，右键或中键 | 只有已启用中心方向模式才打开中心方向对话框，否则无操作。 |
| 左键染色后，AutoFix 已勾选 | 调用 `Check(clicked,false)`。当前颜色优先，可能修正同块及冲突块；详见下一节。 |
| AutoFix 未勾选 | 只保存当前贴纸编辑，不强迫解析为合法 `PatternState`。暂态可能不合法。 |

`FacePaintMouseMove` 在中心显示吸管、其他贴纸显示油漆桶光标（923）；`FacePaintPaint` 另画当前画笔颜色大方块（834）。原版当前画笔初始 `UCol`。

原版 `cube.ini` 实际配色：U 黄、D 白、F 红、B 橙、R 绿、L 蓝，AutoFix=0。只有缺少 INI 时，代码默认是 U 蓝、R 黄、F 红、D 绿、L 白、B 橙，AutoFix=true（RubikMain:698–716）。现代版应区分“当前用户原配置”和“代码缺省值”，不要擅自统一成白顶绿前。

0.2 的最终默认值：颜色沿用上述所给 INI；AutoFix 首次开启，与原 `RubikMain.dfm:425–434` 中 `ColorCheck.Checked=True` 相符。已有本版 `preferences.ini` 中的用户选择仍优先于默认值。

## 自动修正的精确范围

`FaceCube.pas:506–857` 共约 351 行，不是单纯的合法性检查，也不是每次重建全部角棱。

1. 忽略朝向：所点贴纸为特殊颜色时，把同一块其他正常颜色转成对应特殊颜色。
2. 仅朝向：所点贴纸为 `oriCol` 时，将同块其他贴纸清空。
3. 恢复普通颜色：同块其他特殊颜色恢复普通颜色，`oriCol` 清空。
4. 合法角已有两个颜色时，手动检查可以补第三个颜色；棱只有一个颜色时不会猜测第二个。
5. 同块不可能的配色：手动模式清除该块中非点击贴纸，保留新画笔；提交模式清除点击贴纸。
6. 重复块：按位置顺序找到第一个重复块。手动模式清除旧重复块；提交模式清除当前点击贴纸。角比较到两个一致颜色即认定重复。
7. 所有角棱没有“有颜色但尚未填完整”的暂态时，进行总朝向/奇偶检查。扭角和在 URF 修正，翻棱和在 UF 修正，置换奇偶通过交换 UF/DF 修正。至少两块身份未知时允许奇偶自由度；只有一块未知时只为奇偶推断缺失身份，不填它的贴纸。
8. 任何修正使原 `Check` 返回 false。手动模式可出现具体警告；提交按钮对全部非中心执行 `Check(i,true)`，若任一返回 false，先重绘修正结果并警告，退出本次提交（RubikMain:1096–1120）。再次点击才继续。
9. 提交前检查与 AutoFix 开关无关。检查后仍有有色半块则拒绝求解（1121–1146）；未知整块、身份/朝向特殊标记则走不完整公式搜索。

迁移模块 `src/app/editor_state.{hpp,cpp}` 保存 54 原始贴纸，不要求每次点击即可构成合法角棱。`CheckResult.accepted` 对应原布尔返回值，`changed` 报实际变更，`corrections` 返回朝向/奇偶警告类别。`apply_moves_raw` 在不合法的编辑暂态也能做真实贴纸置换。

## 控制面板结构和按钮

原版 DFM 的编辑区域从 269 像素开始横排三列：Add Pattern（367）、Apply Move（109）、Reset To（263）。本轮按用户新图保留这三个分组与顺序，新增宽转两行。

| 分组 | 本轮排列 / 操作 | 原版事件与行号 |
| --- | --- | --- |
| 选择列 | 求解当前情况；生成当前情况 | `ButtonAddSolveClick:1096` 共用处理，根据 Sender 区分方向；本轮完整输入弹方式选择，特殊输入直接方式二 |
| 转动网格 | `R U F / L D B / r u f / l d b / M E S / x y z` | 原版没有小写宽转行；其他按钮共用 `ButtonXClick:2790`、`ButtonMouseDown:2818`、`ButtonMouseUp:2830` |
| 转动左键 | 正向 90° | 原版没有 Ctrl/Shift 改为半转的逻辑，不得自行添加替换习惯 |
| 转动右键 | 按下时标题临时增加 `'`，松开时执行逆转并还原标题 | 实际调用三次正转，`usedRightButton` 阻止模拟左键事件再转一次；不应在按下/松开各转一次 |
| 转动中键 | 无自定义处理 | 不能擅自解释为半转 |
| 重置第1行 | 空白 / 角 / 棱 | `ButtonEmptyClick:844`，`ButtonEmptyCClick:5714`，`ButtonEmptyEClick:5730` |
| 重置第2行 | 复原 / 角 / 棱 | `ButtonClearClick:853`，`ButtonClearCClick:5744`，`ButtonClearEClick:5763` |
| 重置第3行 | 随机 / 角 / 棱 | `ButtonRandomClick:862` 共用 |
| 公式输入 | 文本框 + 应用；Enter 与应用相同 | `BApplyManClick:4326`，`EManeuverKeyDown:4433` |
| 当前颜色自定义 | 打开颜色选择器，仅改变所选颜色的显示值，重绘编辑器和结果区，并写配置 | `ButtonCustomizeClick:899` |
| 自动修正 | 勾选状态写配置 | `ColorCheckClick:5269` |
| busy bar | 对应真实准备/搜索任务状态 | 原 ProgressBar DFM:391，SetUpProgressBar:2018 |

注意：按钮 x/y 对应源内部枚举 Us/Rs，但 `CubeDefs.pas:188–193` 的真实贴纸表明确分别是 x/y。不能因枚举名误换旋转轴。

### 重置细节

- 空白清除非中心，保持中心颜色。全体复原将每面非中心恢复该面颜色。角/棱的空白与复原仅改变指定类别，保留另一类。以上会将中心方向归零。
- 随机生成角排列/朝向、棱排列/朝向坐标，不是随机转 25 次。
- **随机角会把棱复原；随机棱会把角复原**，并非保留另一类别。只随机角/棱时中心方向归零，排列为偶排列以与另一类复原相容。
- 全体随机也随机中心方向，并调整角/棱奇偶与中心奇偶一致。原 `CenterDlg.Button1Click` 只是关闭窗口及恢复按钮，不会归零中心方向。

## 结果行及鼠标选择

原 `OutputPaint:1346–1450` 每行包含：编号、完整展开/斜面组合魔方图、Solver/Generator 切换按钮、公式、Pattern Name 编辑框、optimal 复选框、运行/停止小按钮。名称最大 30 字符（543、751）。选中边框在魔方图外缘，不是新式大型卡片。

| 操作 | 原行为 / 来源 |
| --- | --- |
| 普通点击结果图 | 单选并更新锚点；最初无锚点时可切换当前选中状态（2034） |
| Ctrl 点击 | 切换当前项；取消当前项时锚点清空（2104） |
| Shift 点击 | 清空其他选择，选取锚点到当前项的连续区间，当前项成为新锚点（2109） |
| Ctrl+Shift 点击 | 保留已有选择，增加锚点到当前项的区间（2098） |
| 空白处左键 | 清空所有选择和锚点，禁用仅对选择生效的菜单（2046） |
| 结果图右键 | 保存该行到 popup Tag，打开魔方上下文菜单；不自动改选择（2066） |
| Delete | 删除已选结果；有选中项时吞掉按键，避免名称框再处理（2214） |
| Solver/Generator 按钮 | 切换该行显示方向，不重新搜索（1574）；本轮模 U 结果必须保持其真正目标语义，不能随意逆公式冒充另一方向目标 |
| 运行/停止按钮 | 运行中协作取消；空闲继续寻找下一条更短结果（1514） |
| optimal | 切换搜索算法，取消另一算法正在跑的任务（1625）；必须连接真实算法，不能只改标签 |

上下文非对称基本动作：复制求解公式、复制生成公式、移除该魔方、传入编辑器（DFM:4084–4104）。原另有置换取逆、与编辑器魔方相乘/逆相乘（4069–4079），以及空间旋转/反射等对称动作。

## 文件与快捷键

`SaveWorkspace1Click:1717–1784` 始终按**生成方向**保存，不受结果行 Solver/Generator 当前显示影响。优先使用非状态文字的最优公式，其次普通公式；格式是 `生成公式 (步数f)` 或 `(步数f*)`，有名称追加 `   //名称`，每条 CRLF。无公式时写 48 个非中心贴纸的 Singmaster 表示，先棱再角，以空格分块，未知色写 N。

48 贴纸顺序来自 `CubeDefs.pas:100`：

```
U8 F2 U6 R2 U2 B2 U4 L2 D2 F8 D6 R8 D8 B8 D4 L8
F6 R4 F4 L6 B4 R6 B6 L4
U9 F3 R1 U3 R3 B1 U1 B3 L1 U7 L3 F1
D3 R7 F9 D1 F7 L9 D7 L7 B9 D9 B7 R9
```

`LoadWorkspace1Click:1788–1945` 支持替换/追加/取消；逐行读取，`//` 后为名称。文件公式先从复原态应用得图案，再取逆存为内部 solver，加载后行默认 Generator 显示，不自动求解。带 `*)` 只有计量标识吻合才保留最优标记。ESC 可中止大文件读取。源每行限制约 299 字符是历史缓冲限制，不应复制不安全解析。

新版结构化工程可保留完整特殊约束、模 U 方向、真实动作和计步；同时应支持普通原版 TXT 的生成公式格式。不可把部分图案或模 U 生成目标丢失为单一逆公式。

原 `BApplyManClick:4326` 还支持 `-1`、`-` 表示逆转、`3` 表示逆转、平方字符，以及 `(公式)n` 重复；`DoManeuverString:4206` 支持旧 `Rs` 等双层反向组合和 `Ra` 等双层同向组合。新增小写宽转不能误把旧大写加 s/a 的输入改变含义。

快捷键：Delete 删除所选；Escape 中止加载；Enter 应用公式；Alt+X 退出（DFM ShortCut=32856）。Ctrl+F 是 `saveToDefaultFile:6163`：把**最后一条**结果 solver 公式（最优优先）去掉计步后覆盖 `ctrlFsave.txt`，不是保存整个工作区。原 Ctrl+Alt+R、Alt+S、Alt+V 为隐藏摄像机相关功能，可按用户范围排除。

原窗口标题显示当前文件名；没有独立可编辑地址栏。新版可以显示当前文件路径，但不能将其解释成网页导航。

## 主菜单 / 工具栏对应

主菜单在 `RubikMain.dfm:3774–3997`，没有独立顶部 ToolBar 组件。结果行的运行/停止图标是动态创建的 TSpeedButton（RubikMain:724–761）。0.2 主菜单对应文件(F)、编辑(Y)、运行(Z)、选项(O)、帮助(H)，没有另设导航栏；日夜及 2D/3D 视图放在选项菜单。

| 菜单 | 原条目（顺序） | 本轮处理依据 |
| --- | --- | --- |
| File | Load Maneuvers；Save Maneuvers；Save as HTML；Printer Setup；Print All Cubes；Print Selected Cubes；Exit | 非摄像机基础功能；HTML 可用现代静态图或内嵌脚本替代已淘汰 Java Applet，但内容与计步不变 |
| Edit | Delete selected Cubes；Arrange Selected Symmetric Cubes；Add Symmetry Info；Add Random Cubes；Add all Nonisomorphic Inverse Cubes；Generate WCA Scramble；按名称/步数/Cube ID 排序；Clear Main Window；Expand First Cube | Arrange/Symmetry Info/Nonisomorphic/Expand First Cube 属对称深内容；Expand 实际生成48对称态，不是放大窗口 |
| Run | Start Search for Selected；Stop Search for Selected；Define Maximum Autorun Threads；Start Autorun Two-Phase；Start Autorun Optimal | 真实搜索和任务队列，不能只改变按钮字样 |
| Options | Allow Slice Moves；Two-Phase Algorithm；Huge Optimal Solver；Web Server；Free Threads Immediately；Skip Isomorphics on Load；Isomorphy Includes Inversion；Use Center Facelet Twists；Edit Center Facelet Twists；Generate Statistics | 中层/大表/同构/Web等应区分现有核心能力，不伪装已实现；中心模式/二阶段设置/统计为可保留基础功能 |
| Help | Contents；About | 中文离线帮助与来源说明 |

File 空结果时禁用保存/HTML/打印全部；没有选中项时禁用打印所选（RubikMain:2621）。Edit/Run 对所选操作随真实选择启用（2654）。Add Random Cubes 默认询问 10、原上限100000，只加入状态不自动求解（4534）。Sort 名称再步数、步数再名称、或角棱坐标 Cube ID（5185）。Start Search for Selected 弹 RunSetup，包含二阶段/最优及“当前长度超过 0/18/19/20 才启动”。二阶段选项原为目标长度16–23与Use Triple Search（Options.dfm）。

## 不完整公式窗口与本轮明确改动

原 `Incomp.dfm` 为独立窗口：可选中文本区、Stop Search、Add Solutions to Main Window、Clear Window、Faces to Exclude（F/D/L/B）、Allow Slice Moves、M/E/S 排除、Center Twists to Ignore（六面）。`Incomp.pas:2442–2559` 停止、清空、加入主窗、中层模式重载表的事件分别独立；运行时切换中层会拒绝并恢复勾选状态。

本轮用户最终指定方式二使用**独立弹出的公式窗口**，上方大文本区，下方左侧四行可选动作与中层/双层/整体旋转总开关，右侧三个大按钮。打开不自动搜索，开始/停止可重启；新增预算与规则放进默认折叠的“搜索设置”。宽转与旋转直接作为合法动作集合配置，公式合并/模 U 在算法中完成。打开窗口和找到结果均不得自动展开主窗口左侧或添加结果。

`BAddClick:2466–2508` 每次遍历全部文本行，既不清空、也不记录已添加项；没有运行中禁用 Add 的逻辑。新窗口同样允许运行中 Add，每次复制所有已验证缓冲，重复 Add 会再次加入全部结果。`BClearClick:2511–2514` 清空窗口，运行时禁用，停止后启用。`FormClose:2452–2463` 取消线程并清空文本；本版额外等线程真实退出再关闭，避免留下后台任务。复制操作直接传原始动作与计数，不通过解析显示文本重建公式。

原 Pattern Editor 标签页是五个 3×3 模板、六种抽象 Type 与可选应用面的完整状态枚举（DFM:437–1034，`RunPatButtonClick:1601`），不同于 Incomp 的角棱未知/朝向约束公式求解。`PatternSearch.pas` 仅 297 行，按角/棱回溯赋值，使用基本的单面八种旋转/镜像和合法性检查，不依赖 SymSearch 的高级群条件搜索。现代版已迁移为 `template_search.cpp` 和真实“图案编辑器”页。

模板中颜色代表相等/不同类而非固定面色；全灰模板忽略，混合模板中的灰仍为第六类。`IsContinuous:233–252` 要求每条棱两侧“棱与邻角同色”的关系一致，不是枚举开关。Find Generators（42–45）仅决定匹配状态加入主窗后是否继续计算生成公式。原 AddCube 的全魔方 48 同构/逆置换过滤本次未迁移，其余模板核心已迁移；详见 `template-migration.md`。

## 本轮模型验证

`tests/editor_tests.cpp` 独立测试原版局部/提交修正区别、两角色补第三色、重复块、总朝向和奇偶修正、9个重置按钮语义、合法随机坐标、54类动作对不合法暂态的真实置换/逆置换及旋转色系归一。补充旧公式对层组合、逆转/平方/括号记法和 48 贴纸输入后，Release 编译 503 项检查通过。
