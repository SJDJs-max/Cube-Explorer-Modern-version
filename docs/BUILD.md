# 本地构建与便携工具

工程根目录为 `D:\Cube\cube explorer\new version`。所有源码、下载、工具、依赖缓存、临时文件、日志、剪枝表副本及可执行文件均放在此目录内。`D:\Cube\cube explorer\raw` 始终只读；不读取或复用“失败产物”。

以下命令在 PowerShell 中执行，工作目录先切换到工程根目录。脚本路径中的空格必须保留引号。

```powershell
Set-Location -LiteralPath 'D:\Cube\cube explorer\new version'
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Test -BuildMcp -InstallMcpDependencies
.\scripts\package.ps1
```

`bootstrap.ps1` 只准备缺失的便携工具和数据副本，不调用全局安装器，也不修改系统 PATH。`environment.ps1` 为当前构建进程设置 PATH、TEMP/TMP、pip、Python 字节码及 PyInstaller 缓存目录。C++ 内核不依赖 Python；Python 仅用于 MCP 适配层及其打包。

## 固定工具版本和完整性

| 工具 | 固定版本 | 解压位置 |
|---|---|---|
| GCC / MinGW-w64 | GCC 15.1.0，Qt 官方匹配包 | `tools\bin` 等子目录 |
| CMake | 3.30.5 | `tools\bin` 等子目录 |
| Ninja | 1.12.1 | `tools\Ninja` |
| Qt base | 6.12.0，Windows x64 / MinGW | `tools\Qt` |
| CPython 构建包 | 3.13.17 x64，Python Software Foundation 的 NuGet 包 | `tools\python-package` |
| Qt base 对应源码 | 6.12.0 | `tools\qtbase-source` |

官方下载地址和摘要由以下文件固定：

- `downloads\toolchain-lock.json`：GCC、CMake、Ninja 的地址和 SHA256，附原官方下载 SHA1 校验记录。
- `downloads\qtbase-source.json`：Qt **二进制包来源**及 SHA256；文件名中的 source 指下载来源。
- `downloads\bootstrap-lock.json`：安装位置、关键可执行文件 SHA256、Python 包、Qt 对应源码及原始剪枝表摘要。
- `downloads\qtbase-source.sha256`：Qt 对应源码压缩包的官方摘要记录。

已有下载包先校验 SHA256；缺失时下载到工作区中的 `.partial` 文件，校验通过后才改为正式文件名。摘要不符立即停止，不自动覆盖可疑文件。已安装工具检查关键可执行文件摘要和必要文件；完整且匹配时不重复下载或解压。

MCP 的全部 Python 包版本另列于 `mcp\requirements.lock`，由 `build-mcp.ps1 -InstallDependencies` 安装并运行 `pip check`。该文件目前固定版本，不含每个 wheel 的 SHA256。工具压缩包的字节锁定与 Python 包的版本锁定是两个不同的完整性层级。此流程用于用相同源码和固定依赖重建功能包，不宣称不同机器产生的 EXE 每个字节都相同。

## 离线准备与只检查

```powershell
# 工具和压缩包已经准备好时，只校验，不下载、不解压。
.\scripts\bootstrap.ps1 -Offline -VerifyOnly

# 仅准备 C++ 核心编译工具与原始表，不要求 Qt 或 Python。
.\scripts\bootstrap.ps1 -CoreOnly
.\scripts\build-core.ps1 -Test

# 所需压缩包已放到 downloads 中时，离线解压缺失工具。
.\scripts\bootstrap.ps1 -Offline
```

`-Offline` 缺少必需的缓存包时会明确失败；不会偷偷联网。`-VerifyOnly` 工具或表副本缺失时会失败。`-SkipSources` 可跳过 Qt 对应源码树的准备，用于只做本地二进制开发；最终分发仍应按项目许可与配套源码交付要求处理。

当前机器的 Windows `tar.exe` / libarchive 已实测能够直接读取并解压所选 `.7z` 包：从锁定的 Ninja 压缩包解出的 `ninja.exe` 与实际工具的 SHA256 一致。脚本优先使用 Windows tar；失败时查找已有的 `7z.exe`（PATH、工作区 `tools\7zip` 或已安装的 7-Zip）。两者均不可用时明确提示，不安装全局解压工具。Qt 源码的 `.tar.xz` 解压要求兼容 tar；此场景不能直接用一次 `7z x` 得到源码树。

## 原始剪枝表

首次实现时，四份表从 `D:\Cube\cube explorer` 的原版 5.14 发行包目录复制到 `tables\original`。该原目录只作为读取来源，源码基准仍是 `raw\CubeExplorer-master` 中的 5.15。新求解器按照原坐标顺序、文件长度与固定指纹读取表；不能混入 QTM 或 slice-mode 的另一套表。

| 文件 | 字节数 | SHA256 |
|---|---:|---|
| `RawFlipSlice` | 257720 | `74968A47A6A81EEE2C02FC3455880D675ED4F99BCC75284827B5DC08DED480C1` |
| `flipUDslice.move` | 6958440 | `9826AAEDE5395CAD22CA38BA2282B204E0A9D0E7C068330B8847CC24D940AF86` |
| `phase1PF.prun` | 28181682 | `E80A287108DE80BF5318B545955D400B5A5139C62BBA2D5E207F905D6DA89832` |
| `phase2PF.prun` | 22321152 | `7D12DDB25A087E8CEACE34FAC61B52668C489846B9296DFC6F05D49233831831` |

源码包包含 `tables\original` 时无需重新寻找表。若副本缺失，bootstrap 默认从工程父目录读取上述四个固定文件，也可显式指定另一个只读来源：

```powershell
.\scripts\bootstrap.ps1 -OriginalTableDirectory 'D:\Cube\cube explorer'
```

来源与副本都必须符合锁定的 SHA256；脚本只复制文件，不生成、覆盖或更新原版表。运行时还检查文件长度和固定 FNV-1a 指纹；FNV 用于快速损坏/版本检查，不能代替构建阶段的 SHA256。公式求解的新增动作/目标 PDB 在内存中单独建立，不会改写这四份常规求解表。

## 构建、测试和打包入口

- `scripts\build-core.ps1 -Test`：先独立编译 C++ 核心并运行 CTest。
- `scripts\build.ps1 -Test`：Release 核心、JSON 引擎及 Qt 界面，运行核心、引擎和 UI 检查；存在 MCP 环境时也运行相应接口检查。
- `scripts\build.ps1 -Test -BuildMcp -InstallMcpDependencies`：在上一步基础上按锁文件准备 MCP 环境并构建 MCP EXE。
- `scripts\package.ps1`：使用当前已构建文件整理便携发行包。
- `scripts\package.ps1 -Rebuild`：先触发构建及测试，再打包。
- `scripts\verify-raw.ps1`：用 `docs\raw-sha256.json` 检查原始源码字节未变化。该基准清单记录本机原始源码的绝对路径；迁移到其他电脑时应保留并明确映射原始参考位置，不应改写清单来掩盖差异。

MCP EXE 保留标准输入输出用于协议，不能以 PyInstaller `--windowed`/`--noconsole` 打包。Qt 桌面 EXE 及其 DLL、平台插件、帮助、表文件应一起分发，不能只复制一个主 EXE。

0.3.1 将原 `rubik.ico` 的原字节副本放在 `assets/rubik.ico`，同时编入 Qt 资源和 Windows PE 图标资源。Windows 资源使用单独的 CMake OBJECT 目标；MinGW windres 通过 PATH 中的 gcc 预处理，避免含空格的工程路径被拆分。构建环境脚本已准备所需 PATH，无需新增工具包或修改原源码。

算法对应关系和性能边界分别见 `docs\twophase-migration.md`、`docs\pattern-migration.md`。测试日志和性能日志在 `logs`，不存在以更换语言直接保证性能的结论。
