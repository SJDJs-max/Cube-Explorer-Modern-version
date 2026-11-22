<div align="center">

# Cube Explorer - Modern version

**简体中文** | [English](README.en.md)

[![Release](https://img.shields.io/github/v/release/SJDJs-max/Cube-Explorer-Modern-version?style=flat-square)](https://github.com/SJDJs-max/Cube-Explorer-Modern-version/releases)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg?style=flat-square)](https://github.com/SJDJs-max/Cube-Explorer-Modern-version/releases)

</div>

基于 **Herbert Kociemba** 原始 **Cube Explorer 5.15（HTM）** 深度现代化重构的 Windows 桌面端三阶魔方求解与公式研究工具。采用 **C++20 + Qt 6** 原生构建，遵循 **GPL-3.0** 开源协议。

- **制作**：云起Arrebol
- **原始算法与剪枝表致谢**：[Herbert Kociemba — CubeExplorer](https://github.com/hkociemba/CubeExplorer)
- **MCC 手感评分模型致谢**：Trangium (`MovecountCoefficient` / `algSpeed`)

---

## ✨ 相比原版 Cube Explorer 5.15 的核心改进

> **说明**：Herbert Kociemba 原始 Cube Explorer 5.15（Delphi）主要面向传统 18 外层转动，**并不支持**公式模式下的双层宽转、中层、模 U、细粒度禁单角度、MCC 手感排序及现代多核并发。本项目在保留原版二阶段核心与交互习惯的基础上，完成了从架构、算法到界面的全面重构。

### 1. 全新公式搜索引擎：原生支持宽转 / 中层 / 转体，并深度提速
- **原版所没有的现代速拧公式能力**：
  - 原生支持**真实双层宽转（`u/r/f/d/l/b`）、中层（`M/E/S`）与免费整体旋转（`x/y/z`，计 0 步）**，区分**求解**与**生成**两个方向；
  - 完整支持搜索层**模 U**（锁定输入逻辑 U 面）、U 对称省去冗余首步、禁止 `R r R` 类交替三连、可交换动作规范去重；
  - 每一条输出的公式均经**独立于搜索表的物理贴纸几何模型**逐条回放验证。
- **现代多核与大内存架构优化（宽转 / 中层速度追平传统外层）**：
  - 引入 **Frame 轨道感知剪枝表**，避免宽转/中层在多握持框架下下界坍缩；
  - 新增 **O(1) 增量查表游标（`FastCursorTable`）**，在状态转移前直接完成大部分子节点剪枝；
  - 采用重叠 6 棱置换、2 角+4 棱联合表（强化模式下高 gen 支持 7 棱 / 3 角+4 棱）、**多线程并行建表**、**多线程保序并行 IDA*** 以及**单轴转体局部轨道商空间剪枝**（如 `R U F + y`）；
  - 相比本项目早期单线程未优化基线，宽转/中层搜索提速 **14×–40×**，速度与传统外层持平甚至更快（10 组基准输出哈希 100% 逐字节一致）：

| 典型公式枚举用例 | 本项目早期基线 | 当前优化版 | 提速倍率 |
| :--- | ---: | ---: | :---: |
| ZBLS `RUr`（3-gen 宽转，13 步） | 644 ms | **30 ms** | **21.5×** |
| ZBLS `RUM`（3-gen 中层，13 步） | 307 ms | **22 ms** | **14.0×** |
| ZBLS `RUF`（3-gen 传统外层，13 步） | 1,143 ms | **50 ms** | **22.9×** |
| ZBLS `RUFr`（4-gen 宽转，12 步） | 2,825 ms | **70 ms** | **40.4×** |
| ZBLS `RUFM`（4-gen 中层，12 步） | 2,476 ms | **67 ms** | **37.0×** |
| ZBLS `RUFL`（4-gen 传统外层，12 步） | 1,614 ms | **65 ms** | **24.8×** |
| ZBLS `URFDr`（5-gen 宽转，10 步） | 294 ms | **18 ms** | **16.3×** |
| 全态 `RUD`（3-gen 外层，16 步） | 2,337 ms | **120 ms** | **19.5×** |

### 2. 方法一 · 最优求解器大幅提速（30×–90×）
- 在原版标准三轴（UD / RL / FB）剪枝表基础上，彻底重构搜索核心：
  - **纯坐标 DFS**（移除每节点 40 字节状态乘法）、**最紧轴优先短路评估**、**叶节点零内存 G1 过滤**、**正/逆魔方 6 轴下界择优**；
  - 内置懒加载 **5.8 MB 精确 8 角（`CornerPerm × Twist` mod 16 对称）剪枝表**（约 58 ms 构建，全程驻留 L3 缓存）；
  - **多核前沿并行 IDA***（深度 ≥ 7 自动并发），吞吐从按原版逻辑迁移的单线程 1.3M 节点/秒提升至 **37–39M 节点/秒**：
    - 16 步最优：`6.95 s → 0.075 s`（**约 92× 提速**）
    - 17 步最优：8 s 内超时 → **`0.40 s` 证明最优**
    - 18 步全局最优（20 步随机打乱）：8 s 内超时 → **`8.3 s` 证明全局最优**

### 3. 原生 MCC 手感排序、起手预旋转与细粒度动作控制
- **原生 C++20 MCC 手感评分与无损重排**：
  - 搜索仍按**步数从低到高流式输出**（绝不引发计算量爆炸，不改动公式与步数），每条公式微秒级实时计算 **MCC（Movecount Coefficient）** 并标注 `[MCC x.x]`；
  - 右下角「显示顺序」一键切换 **「默认 / HTM / MCC」** 毫秒级重排，支持在「高级设置 → MCC 设置」中微调 10 项手感参数及 **「去起手 U（忽略 pre-AUF）」** 开关。
- **专为 Cross / XCross 设计的起手预旋转（4 种 / 24 种，终点底色必在 D 面）**：
  - 在「高级设置 → 起手预旋转」中支持切换 **无预旋转** / **水平预旋转 `y/y2/y'`（4 种）** / **全空间预旋转 `x/y/z`（24 种）**；
  - 预旋转**仅发生在所有转动之前**，后续不再转体（除非已勾选整体旋转），且严格保证做完后底色中心落在物理 D 底面——既支持 `<R, U, L>` 下通过 `[y'] R' U' R` 解决 `F' U' F`，也支持高手起手 `x / z` 斜握配合双层/中层转动（如 `[x'] R r U`）将底色刚好带回 D 面；输出时预旋转自动以中括号 `[y']` 标示。
- **细粒度动作与次数控制**：
  - 在公式窗口**右键任意字母复选框**即可单独禁用某个角度（如只禁 `F2` / `L2`，保留 `F` / `F'`）或设置单层最大出现次数，引擎严格禁止通过 `F F` 或 `F B F` 等方式绕过；
  - 高级设置内提供完整 **18×3 共 54 动作「精细限制」矩阵弹窗** 与 **最大转体次数** 限制（勾选整体旋转时自动启用）。

### 4. 现代化界面与速拧快捷工具
- **日夜主题 + 中英双语一键切换（含双语离线帮助）**：支持高 DPI 等比例缩放、现代化矢量控件与完整中文右键菜单；按 `F1` 打开的离线帮助文档同步跟随当前界面语言；「选项」内支持动态切换 **展开主窗口 / 收起主窗口**。
- **三视图无缝切换 + 左侧列表实时联动**：支持 **经典展开 / 2D 展开 / 3D 魔方**，左侧结果缩略图自动跟随主视角切换；在 3D 模式下拖动或重置主魔方视角时，左侧 3D 缩略图实时同步跟随旋转（仅改观察角度，不改贴纸朝向）。
- **快捷一键复制**：方法一结果行右上角提供**简约复制小图标**（点击即复制纯净公式并变为绿色对勾）；方法二公式窗口内支持**双击任意公式行快捷复制**或右键复制，并弹出悬浮「✓ 已复制到剪贴板」提示框。
- **增强公式输入 + 逆序按钮**：支持无空格连续输入（如 `RUR'U'` = `R U R' U'`）、`R'2` / `r'2` / `Rw'2` 半转写法与常见全角/弯引号；输入框旁提供「逆」按钮一键施加逆公式。
- **自定义快捷键**：`选项 → 快捷键设置` 支持自由绑定快捷键并检测冲突（默认 `Ctrl+Enter` 全局直接应用公式、`Ctrl+Shift+Enter` 应用逆公式、`F2` 切换视角、`F5/F6` 求解/生成等）。
- **常用速拧预设模板**：主界面一键加载 **ZBLS**（顶层全灰 + 顶层 4 棱 Ctrl 朝向标记）、**OLL**、**F2L**、**OLLCP / COLL**。
- **Cross 快速求解弹窗**：默认白底（可自由切换 6 种底色），以**三色色块 + 具体颜色 Pair（如 `白-蓝红 Pair`）**直观勾选 0–4 组 F2L Pair（覆盖 Cross / XC / XXC / XXXC / 完整 F2L），并默认联动「起手预旋转」与底面 D 锁定。

### 5. CPU / 内存保护与 AI（MCP）接口
- **三档性能模式**：`选项 → 性能模式` 提供 **静音（50% 核） / 均衡（默认，留核 + 低优先级防卡顿） / 全速（满核）**，并按可用物理内存自动调节剪枝表缓存上限。
- **内置标准 MCP（Model Context Protocol）stdio 服务**：支持 AI 客户端直接调用单条/批量求解、进度查询、取消与分页结果。

---

## 📖 快速使用指南

### 1. 直接运行便携版（Windows x64）
前往 GitHub **Releases** 下载 `CubeExplorerModern-win64.zip`，解压后**保留整个文件夹**，直接双击：
```text
CubeExplorerModern/CubeExplorerModern.exe
```
无需额外安装 Qt、Python 或运行库。内置完整中文离线帮助：`help/index.html`。

### 2. 贴纸编辑与染色快捷键
- **左键点击贴纸**：涂上当前画笔颜色
- **Shift + 左键**：标记该块**忽略朝向**（仅关心位置）
- **Ctrl + 左键**：标记该块**仅关心朝向**（不关心具体颜色身份，常用于 ZBLS / OLL 顶层色向约束）
- **右键点击贴纸**：清空单个贴纸为未知态（灰色 `?`）
- **左键点击中心块**：快速吸取该中心颜色为当前画笔
- 也可在底部「输入公式」框中输入打乱/公式，点 **应用** 或 **逆** 直接生成魔方状态。

### 3. 方法一：常规二阶段 & 最优求解
- 适用于完整 54 贴纸状态：点击「求解当前情况」→ 选择 **方法一（常规/最优）**，添加至主窗口结果列表；
- 勾选结果行中的 **最优** 后点击求解，即启动多核精确最优搜索，证毕后步数旁标注 `*`。

### 4. 方法二：公式求解 / 生成（速拧找公式核心）
- 适用于任意完整或部分图案（如 ZBLS / OLL / F2L / OLLCP / Cross）：
  1. 在主界面点击预设按钮（如 **ZBLS** / **Cross 快速求解**）或自行涂色/打乱；
  2. 点击「求解当前情况」或「生成当前情况」→ 打开独立**公式窗口**；
  3. 勾选允许的层（支持外层、双层宽转、中层、整体转体）；
  4. **右键任意层复选框**可单独禁掉 `F2`、`L2` 等不顺手角度，或在展开「高级设置」后点 **精细限制** 批量调整，也可开启 **起手预旋转（4 种 / 24 种）** 搜索最佳 Cross / XCross 握法；
  5. 点击「开始搜索」即可按步数从低到高流式输出，随时切换右下角 **`显示顺序：默认 / HTM / MCC`** 筛选最顺手的公式，**双击任意公式行**即可快捷复制到剪贴板；展开「高级设置」可在 **MCC 设置** 中微调 10 项手感参数与「去起手 U」。

### 5. MCP 服务接入（可选）
便携包内已附带独立打包的 `mcp/CubeExplorerMcp.exe` 与配置模板 `mcp-config.json`，详见 [mcp/README.md](mcp/README.md)。

---

## 📁 仓库目录结构

```text
CubeExplorer-Modern/
├── include/cube/          # 公共 C++20 头文件（魔方模型、二阶段、最优、公式枚举、MCC、运行配置）
├── src/
│   ├── core/              # 核心求解引擎（model / twophase / optimal / pattern / mcc / runtime_config）
│   ├── engine/            # 独立无界面 JSON 引擎可执行程序（CubeExplorerEngine）
│   └── app/               # Qt 6 桌面 GUI（主窗口、公式窗口、2D/3D 视图、Cross 弹窗、主题与双语）
├── tests/                 # 15 套 C++ / Qt 自动化单元测试、几何差分测试与 MCC 对拍测试
├── mcp/                   # 官方 MCP Python stdio 服务与桥接集成测试
├── scripts/               # Windows 便携工具链引导、构建、测试、打包与发布校验脚本
├── assets/                # 应用图标与 Qt 资源
├── help/                  # 离线中文使用说明（help/index.html）
├── docs/                  # 算法迁移说明、性能分析与构建文档
├── CMakeLists.txt         # CMake 构建定义
├── LICENSE                # GPL-3.0 许可证
└── NOTICE.txt             # 第三方组件与源码归属声明
```

> 注：构建产物（`build/`、`dist/`）、本地工具链（`tools/`、`downloads/`）与原版剪枝表二进制（`tables/`）均通过 `.gitignore` 排除在代码仓库之外；Windows 便携发布包（含完整剪枝表、Qt 运行时与 MCP 程序）随 GitHub Release 分发。

---

## 🛠️ 从源码构建与测试（Windows x64）

```powershell
# 1. 首次准备便携工具链（CMake / Ninja / MinGW / Qt 6 / Python）
.\scripts\bootstrap.ps1

# 2. 编译 Release 并运行全部 15 套 CTest + 引擎集成 + UI 冒烟 + MCP 测试
.\scripts\build.ps1 -Test

# 3. 打包无版本号便携发布目录（dist/CubeExplorerModern 与 dist/CubeExplorerModern-win64.zip）
.\scripts\package.ps1
```

更多细节请参考 [docs/BUILD.md](docs/BUILD.md) 与 [docs/CORE-VALIDATION.md](docs/CORE-VALIDATION.md)。

---

## 📜 开源许可与声明

本项目基于 Herbert Kociemba 的 Cube Explorer 5.15（GPL-3.0）开发并继续以 **GNU General Public License v3.0** 发布，完整许可正文见 [LICENSE](LICENSE)，第三方库（Qt 6、MinGW、Python / MCP SDK 等）与算法致谢详见 [NOTICE.txt](NOTICE.txt)。
