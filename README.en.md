<div align="center">

# Cube Explorer - Modern version

[简体中文](README.md) | **English**

[![Release](https://img.shields.io/github/v/release/SJDJs-max/Cube-Explorer-Modern-version?style=flat-square)](https://github.com/SJDJs-max/Cube-Explorer-Modern-version/releases)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg?style=flat-square)](https://github.com/SJDJs-max/Cube-Explorer-Modern-version/releases)

</div>

A modernized **C++20 + Qt 6** Windows desktop Rubik's Cube solver and algorithm-finding tool, rebuilt on top of **Herbert Kociemba's** original **Cube Explorer 5.15 (HTM)** under the **GPL-3.0** license.

- **Author**: 云起Arrebol
- **Original Algorithm & Pruning Tables**: [Herbert Kociemba — CubeExplorer](https://github.com/hkociemba/CubeExplorer)
- **MCC Ergonomics Model Reference**: Trangium (`MovecountCoefficient` / `algSpeed`)

---

## ✨ Key Improvements Over Original Cube Explorer 5.15

> **Note**: Herbert Kociemba's original Cube Explorer 5.15 (Delphi) targets traditional 18 outer-face moves and **does not** support wide/slice/rotation formula enumeration, modulo-U search, per-move angle bans, MCC ergonomics sorting, or modern multi-core parallel IDA*. This project preserves the classic two-phase semantics and familiar workflow while overhauling the engine, algorithms, and desktop UI.

### 1. New Formula Search Engine: Native Wide / Slice / Rotation Support & Deep Speedup
- **Modern speedcubing capabilities absent in 5.15**:
  - Native support for **true wide turns (`u/r/f/d/l/b`), slice turns (`M/E/S`), and free whole-cube rotations (`x/y/z`, cost 0)**, with separate **Solve** and **Generate** directions;
  - Search-level **Modulo-U** (anchored to the input logical U face), U-symmetric prefix pruning, alternating-triple rejection (e.g., `R r R`), and canonical commuting-move deduplication;
  - Every emitted formula is **independently verified** by a physical sticker-geometry simulator completely separate from the cubie search tables.
- **Modern multi-core & RAM architecture (wide/slice speeds match traditional outer turns)**:
  - **Frame-orbit-aware pruning tables** prevent lower-bound collapse across wide/slice holding frames;
  - **O(1) incremental lookup cursors (`FastCursorTable`)** prune the majority of child branches before state transition even runs;
  - Modern memory-tier pruning tables (overlapping 6-edge perm, combined 2-corner + 4-edge; Strong mode unlocks 7-edge / 3-corner + 4-edge on high-gen sets), **parallel table construction**, **order-preserving multi-threaded IDA***, and **local-orbit free-rotation quotienting** (including single-axis rotations such as `R U F + y`);
  - Compared to this project's earlier single-threaded unoptimized baseline, wide/slice searches run **14×–40× faster** with 100% bit-for-bit identical output hashes across all 10 benchmark cases:

| Formula Enumeration Benchmark | Early Baseline | Current Optimized | Speedup |
| :--- | ---: | ---: | :---: |
| ZBLS `RUr` (3-gen wide, depth 13) | 644 ms | **30 ms** | **21.5×** |
| ZBLS `RUM` (3-gen slice, depth 13) | 307 ms | **22 ms** | **14.0×** |
| ZBLS `RUF` (3-gen outer, depth 13) | 1,143 ms | **50 ms** | **22.9×** |
| ZBLS `RUFr` (4-gen wide, depth 12) | 2,825 ms | **70 ms** | **40.4×** |
| ZBLS `RUFM` (4-gen slice, depth 12) | 2,476 ms | **67 ms** | **37.0×** |
| ZBLS `RUFL` (4-gen outer, depth 12) | 1,614 ms | **65 ms** | **24.8×** |
| ZBLS `URFDr` (5-gen wide, depth 10) | 294 ms | **18 ms** | **16.3×** |
| Full cube `RUD` (3-gen outer, depth 16) | 2,337 ms | **120 ms** | **19.5×** |

### 2. Method 1 · Optimal HTM Solver Speedup (30×–90×)
- Re-engineered on top of the original 3-axis (UD / RL / FB) pruning tables with zero extra disk files:
  - **Coordinate-only DFS** (eliminating per-node cubie multiplication), **tightest-axis-first short-circuiting**, **zero-memory leaf G1 filtering**, and **forward/inverse 6-axis root bound selection**;
  - Lazy-built in-memory **5.8 MB exact 8-corner (`CornerPerm × Twist` mod 16 symmetry)** L3-resident pruning table (~58 ms one-time build);
  - **Multi-threaded frontier IDA*** (depth ≥ 7), lifting throughput from ~1.3M nodes/s (single-threaded port of the 5.15 algorithm) to **37–39M nodes/s**:
    - 16-move optimal: `6.95 s → 0.075 s` (**~92× faster**)
    - 17-move optimal: timed out at 8 s → **proven in `0.40 s`**
    - 18-move global optimal (20-move scramble): timed out at 8 s → **proven in `8.3 s`**

### 3. Native C++20 MCC Ergonomics Sorting & Fine-Grained Move Control
- **Native MCC scoring & lossless re-sorting**:
  - Search remains **streaming by ascending move count** (no combinatorial explosion, moves and search turn counts remain untouched), while computing **MCC (Movecount Coefficient)** in microseconds per result (`[MCC x.x]`);
  - Switch instantly between **Search Order / Moves → MCC / MCC → Moves**, with a **MCC Settings** dialog for all 10 ergonomics parameters and the **Ignore pre-AUF (leading U / d+U)** toggle.
- **Per-move angle bans & usage caps**:
  - **Right-click any move checkbox** in the Formula Window to ban individual angles (e.g., ban `F2` / `L2` while keeping `F` / `F'`) or cap per-layer occurrences;
  - Advanced settings include a full **18×3 (54-move) Fine Moves Matrix** dialog and a **Max Rotations** cap (automatically enabled when rotations are checked).

### 4. Modern Desktop UI & Speedcubing Utilities
- **Dark/Light Theme & Instant Chinese/English Toggle** with proportional HiDPI scaling and custom vector Qt styling.
- **Classic / 2D Net / 3D Cube Views** (corrected 3D Y-axis orbit drag; camera-only rotation without mutating stickers).
- **One-Click Speedcubing Presets**: **ZBLS** (grey U layer + Ctrl orientation markers on the 4 U edges), **OLL**, **F2L**, and **OLLCP / COLL**.
- **Cross Quick Solver**: choose any of the 6 bottom colors and freely toggle 0–4 F2L slots (`BL / BR / FL / FR` covering Cross / XC / XXC / XXXC / Full F2L); automatically rotates the chosen bottom color to physical `D` with `Mod-U` disabled.
- **Inverse Formula Button**: one-click inverse application right beside the formula input box for fast setups.

### 5. CPU / Memory Protection & MCP AI Integration
- **3-Tier Performance Profile** (`Options → Performance`): **Quiet (~50% cores)**, **Balanced (Default, reserves 1–2 cores + below-normal thread priority)**, and **Full Speed**, plus physical-RAM-adaptive pruning-table cache limits.
- **Built-in Model Context Protocol (MCP) stdio server** for single/batch solving, progress tracking, cancellation, and paginated JSONL output.

---

## 📖 Quick Start

### 1. Portable Windows x64 Release (No Installation Needed)
Download `CubeExplorerModern-win64.zip` from [GitHub Releases](https://github.com/SJDJs-max/Cube-Explorer-Modern-version/releases), extract the entire folder, and launch:
```text
CubeExplorerModern/CubeExplorerModern.exe
```
Qt runtimes, pruning tables, offline help (`help/index.html`), and the bundled MCP server are all included.

### 2. Sticker Editor Shortcuts
- **Left-click**: paint sticker with the active brush color
- **Shift + Left-click**: ignore cubie orientation (permutation only)
- **Ctrl + Left-click**: orientation-only marker (ideal for ZBLS / OLL top-layer EO)
- **Right-click**: clear sticker to unknown (`?`)
- **Left-click center**: pick that center color as the active brush
- Or enter a scramble/formula at the bottom and click **Apply** or **Inv**.

### 3. Method 1 (Two-Phase & Optimal) vs. Method 2 (Formula Window)
- **Method 1**: for complete 54-sticker states, add to the main result list and check **Optimal** to run the multi-threaded exact optimal solver (`*` indicates proven optimal).
- **Method 2**: for complete or partial patterns (ZBLS / OLL / F2L / OLLCP / Cross), open the **Formula Window**, pick allowed layers, right-click any letter to ban individual angles (e.g., `F2`, `L2`) or use **Fine Moves…**, click **Start Search**, and switch between **Search Order / Moves → MCC / MCC → Moves** anytime.

### 4. MCP Integration (Optional)
See [mcp/README.md](mcp/README.md) and the generated `mcp-config.json` inside the portable folder.

---

## 📁 Repository Structure

```text
Cube-Explorer-Modern-version/
├── include/cube/          # Public C++20 headers (model, twophase, optimal, pattern, mcc, runtime_config)
├── src/
│   ├── core/              # Core solver engines (model / twophase / optimal / pattern / mcc / runtime_config)
│   ├── engine/            # Headless JSON engine executable (CubeExplorerEngine)
│   └── app/               # Qt 6 desktop GUI (main window, formula window, 2D/3D canvas, Cross dialog, i18n)
├── tests/                 # 15 C++ / Qt unit, differential geometry, and MCC parity test suites
├── mcp/                   # Official MCP Python stdio server & integration tests
├── scripts/               # Portable Windows toolchain bootstrap, build, test, package & verification scripts
├── assets/                # Application icon & Qt resources
├── help/                  # Offline user guide (help/index.html)
├── docs/                  # Migration notes, performance benchmarks & build documentation
├── CMakeLists.txt         # CMake build definition
├── LICENSE                # GPL-3.0 license
└── NOTICE.txt             # Attribution & third-party component notices
```

---

## 🛠️ Building & Testing From Source (Windows x64)

```powershell
# 1. Bootstrap portable toolchain (CMake / Ninja / MinGW / Qt 6 / Python)
.\scripts\bootstrap.ps1

# 2. Build Release and run all 15 CTest suites + engine + UI smoke + MCP tests
.\scripts\build.ps1 -Test

# 3. Package portable release (dist/CubeExplorerModern and dist/CubeExplorerModern-win64.zip)
.\scripts\package.ps1
```

See [docs/BUILD.md](docs/BUILD.md) and [docs/CORE-VALIDATION.md](docs/CORE-VALIDATION.md) for details.

---

## 📜 License & Acknowledgements

Derived from Herbert Kociemba's Cube Explorer 5.15 (GPL-3.0) and distributed under the **GNU General Public License v3.0** ([LICENSE](LICENSE)). Third-party component licenses and attributions (Qt 6, MinGW, Python / MCP SDK, Trangium's MCC model) are documented in [NOTICE.txt](NOTICE.txt).
