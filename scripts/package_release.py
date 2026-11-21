"""Assemble the portable app, corresponding source, notices and release hashes."""
from __future__ import annotations
import hashlib
import json
import re
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist" / "CubeExplorerModern"
assert DIST.resolve().is_relative_to(ROOT.resolve())

def copy_tree(source: Path, target: Path) -> None:
    shutil.copytree(source, target, dirs_exist_ok=True)

def archive_files(output: Path, paths: list[Path], relative_to: Path) -> None:
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(paths):
            archive.write(path, path.relative_to(relative_to).as_posix())

for directory in ("help", "docs", "tables"):
    copy_tree(ROOT / directory, DIST / directory)
for name in ("README.md", "LICENSE", "NOTICE.txt"):
    shutil.copy2(ROOT / name, DIST / name)
shutil.copy2(ROOT / "mcp/README.md", DIST / "docs/MCP.md")
(DIST / "docs/validation").mkdir(exist_ok=True)
brush_evidence = ROOT / "logs/modern-brush"
if brush_evidence.is_dir():
    target = DIST / "docs/validation/brush-layout"
    target.mkdir(exist_ok=True)
    for path in sorted(brush_evidence.glob("original-editor-*.png")):
        shutil.copy2(path, target / path.name)
    for name in ("main-window.txt", "editor-checks.txt"):
        if (brush_evidence / name).is_file():
            shutil.copy2(brush_evidence / name, target / name)
for name in ("build-final.txt", "core-final-verbose.txt", "portable-ui-smoke.txt", "mcp-release-integration.json",
             "mcp-release-legacy.jsonl", "pattern-bench-rotations-probe.csv", "ui-v2-smoke.txt",
             "build-v2-final.txt", "optimal-v2-tests.txt", "optimal-v2-bench.txt", "cube-canvas-v2.txt",
             "engine-optimal-v2.txt", "mcp-exe-optimal-v2.json", "mcp-v2-release.json"):
    if (ROOT / "logs" / name).exists():
        shutil.copy2(ROOT / "logs" / name, DIST / "docs/validation" / name)
for name in ("build-v3-final.txt", "build-ui-v3-last.txt", "ui-v3-smoke.txt", "ui-v3-release.txt",
             "formula-window-tests-v3.txt", "original-editor-view-tests.txt", "result-row-interaction-v2.txt",
             "result-row-original-layout-v3.txt", "template-editor-tests.txt", "template-search-tests.txt",
             "engine-template-v3.txt", "mcp-source-template-v3.txt", "mcp-schema-template-v3.txt",
             "mcp-exe-template-v3.txt", "build-mcp-template-v3.txt",
             "mcp-v3-release.json", "mcp-v3-legacy.jsonl", "engine-v3-release.json", "core-v3-verbose.txt"):
    if (ROOT / "logs" / name).exists():
        shutil.copy2(ROOT / "logs" / name, DIST / "docs/validation" / name)
for name in ("ui-v3-initial-day.png", "ui-v3-initial-night.png", "ui-v3-ordinary-day.png",
             "ui-v3-ordinary-night.png", "ui-v3-formula-day.png", "ui-v3-formula-night.png",
             "ui-v3-template-day.png", "ui-v3-template-night.png"):
    if (ROOT / "tests" / name).exists():
        shutil.copy2(ROOT / "tests" / name, DIST / "docs/validation" / name)
if (ROOT / "build/twophase-dev/validation-500.txt").exists():
    shutil.copy2(ROOT / "build/twophase-dev/validation-500.txt", DIST / "docs/validation/twophase-500.txt")
for name in ("build-v4-final.txt", "core-v4-verbose.txt", "ui-v4-smoke.txt", "ui-v4-release.txt",
             "ui-v4-dpi150.txt", "ui-v4-native.txt", "engine-v4-release.json", "mcp-v4-release.json", "mcp-v4-legacy.jsonl",
             "formula-window-v4-tests.txt", "user-regression-tests.txt", "user-rud16-standard.txt",
             "user-rud16-strong.txt", "user-rud16-parity.txt", "twophase-startup-baseline-v4.txt",
             "twophase-startup-optimized-v4.txt", "twophase-baseline-v4.txt",
             "twophase-final-single-v4.txt", "twophase-final-triple-v4.txt"):
    if (ROOT / "logs" / name).exists():
        shutil.copy2(ROOT / "logs" / name, DIST / "docs/validation" / name)
for name in ("ui-v4-initial-day.png", "ui-v4-initial-night.png", "ui-v4-ordinary-day.png",
             "ui-v4-ordinary-night.png", "ui-v4-formula-day.png", "ui-v4-formula-night.png",
             "ui-v4-method-choice.png", "ui-v4-color-picker.png", "ui-v4-scale-390.png",
             "ui-v4-scale-765.png", "ui-v4-statistics.png", "ui-v4-native-initial-day.png",
             "ui-v4-native-ordinary-day.png", "ui-v4-native-formula-day.png"):
    if (ROOT / "tests" / name).exists():
        shutil.copy2(ROOT / "tests" / name, DIST / "docs/validation" / name)
for name in ("baseline-500.tsv", "final-single-500.tsv", "final-triple-500.tsv"):
    if (ROOT / "bench/twophase-v4" / name).exists():
        shutil.copy2(ROOT / "bench/twophase-v4" / name, DIST / "docs/validation" / ("twophase-" + name))
for name in ("font-probe-main-windows.json", "font-probe-main-offscreen.json"):
    if (ROOT / "temp" / name).exists():
        shutil.copy2(ROOT / "temp" / name, DIST / "docs/validation" / name)
# Keep this release's evidence together with the historical records above.
for pattern in ("*-v5-*.txt", "*-v5-*.json", "*-v5-*.jsonl", "twophase-v031-*.txt"):
    for path in (ROOT / "logs").glob(pattern):
        if not path.name.startswith(("build-ui-v5-first", "build-ui-v5-second", "build-ui-v5-third")):
            shutil.copy2(path, DIST / "docs/validation" / path.name)
for pattern in ("ui-v5-*.png", "cube-canvas-v031-*.png", "result-row-scale-*.png", "original-icon-extracted.png"):
    for path in (ROOT / "tests").glob(pattern):
        shutil.copy2(path, DIST / "docs/validation" / path.name)
for directory in ("twophase-v031", "v5"):
    source = ROOT / "bench" / directory
    if source.exists():
        for path in source.iterdir():
            if path.is_file() and path.suffix in {".csv", ".tsv", ".json", ".py", ".cpp", ".txt"}:
                target = DIST / "docs/validation" / directory
                target.mkdir(exist_ok=True)
                shutil.copy2(path, target / path.name)
(DIST / "README.md").write_text(
    "# Cube Explorer - Modern version\n\n基于 Herbert Kociemba 原始 Cube Explorer（C++20 / Qt · GPL-3.0）｜制作：云起Arrebol\n\n"
    "双击 `CubeExplorerModern.exe`。保留整个文件夹，无需另外安装 Python 或 Qt。\n\n"
    "中文帮助：`help/index.html`。AI 接入：`mcp-config.json` 与 `docs/MCP.md`。\n\n"
    "验证：`docs/CORE-VALIDATION.md`。完整新源码：`corresponding-source.zip`，解压后阅读 `docs/BUILD.md`。\n\n"
    "画笔与视图布局见 `docs/CUBE-CANVAS.md`。算法调整与实测见 `docs/ALGORITHM-CHANGES-0.3.1.md`。\n\n"
    "许可、第三方出处及 Qt 源码位置：`NOTICE.txt`。校验清单：`SHA256SUMS.json`。\n", encoding="utf-8")
(DIST / "data").mkdir(exist_ok=True)
license_dir = DIST / "licenses"
for component in ("gcc", "mingw-w64", "winpthreads"):
    copy_tree(ROOT / "tools/licenses" / component, license_dir / component)
qt_source = ROOT / "tools/qtbase-source/qtbase-everywhere-src-6.12.0"
copy_tree(qt_source / "LICENSES", license_dir / "qt/LICENSES")
shutil.copy2(ROOT / "downloads/qtbase-everywhere-src-6.12.0.tar.xz", license_dir / "qt")
shutil.copy2(ROOT / "downloads/qtbase-source.sha256", license_dir / "qt")
copy_tree(ROOT / "tools/Qt/sbom", license_dir / "qt/sbom")

config = {"mcpServers": {"cube-explorer": {
    "command": str(DIST / "mcp/CubeExplorerMcp.exe"),
    "args": ["--workspace", str(DIST / "data")],
}}}
(DIST / "mcp-config.json").write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

# Include the complete application source plus input data required to rebuild.
# Tools can be reconstructed from the locked portable bootstrap instructions.
source_files = [ROOT / name for name in ("CMakeLists.txt", "README.md", "NOTICE.txt", "LICENSE", "AGENTS.md", ".gitignore", ".clang-format")]
for directory in ("include", "src", "assets", "tests", "scripts", "docs", "help", "tables", "lsll"):
    source_files.extend(p for p in (ROOT / directory).rglob("*") if p.is_file() and p.suffix not in {".png", ".pyc"})
for path in (ROOT / "mcp").iterdir():
    if path.is_file() and path.suffix in {".py", ".md", ".lock", ".spec"}:
        source_files.append(path)
source_files.extend(p for p in (ROOT / "downloads").iterdir() if p.suffix in {".json", ".sha256"})
archive_files(DIST / "corresponding-source.zip", source_files, ROOT)

manifest = []
for path in sorted(DIST.rglob("*")):
    if path.is_file() and path.name != "SHA256SUMS.json" and "data" not in path.relative_to(DIST).parts[:1]:
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        manifest.append({"path": path.relative_to(DIST).as_posix(), "bytes": path.stat().st_size, "sha256": digest})
(DIST / "SHA256SUMS.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
release_archive = ROOT / "dist/CubeExplorerModern-win64.zip"
archive_files(release_archive,
              [p for p in DIST.rglob("*") if p.is_file() and "data" not in p.relative_to(DIST).parts[:1]], DIST.parent)
with release_archive.open("rb") as stream:
    digest = hashlib.file_digest(stream, "sha256").hexdigest()
release_archive.with_suffix(".zip.sha256").write_text(f"{digest}  {release_archive.name}\n", encoding="utf-8")
print(json.dumps({"release": str(DIST), "files": len(manifest), "size_mb": round(sum(x["bytes"] for x in manifest) / 1048576, 1)}, ensure_ascii=False))
