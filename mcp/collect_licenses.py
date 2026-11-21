"""Collect installed distributions' actual license records, including vendored files.

Build-only: scanning only *.dist-info/licenses misses old wheel layouts and the
licenses of vendored components shipped inside package directories.
"""
from __future__ import annotations

import argparse
import hashlib
from importlib import metadata
import json
from pathlib import Path
import shutil


def collect(destination: Path) -> None:
    root = Path(__file__).resolve().parent.parent
    destination = destination.resolve()
    if not destination.is_relative_to(root):
        raise ValueError("License output must remain inside the Cube Explorer workspace.")
    destination.mkdir(parents=True, exist_ok=True)
    manifest = []
    for distribution in sorted(metadata.distributions(), key=lambda item: item.metadata["Name"].lower()):
        package = distribution.metadata["Name"]
        entry = {"package": package, "version": distribution.version,
                 "license_expression": distribution.metadata.get("License-Expression"), "files": []}
        package_directory = destination / f"{package}-{distribution.version}"
        for recorded in distribution.files or []:
            relative = Path(str(recorded))
            if not relative.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE", "AUTHORS")):
                continue
            source = Path(distribution.locate_file(recorded)).resolve()
            if not source.is_relative_to(root) or relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"License source escapes the workspace: {recorded}")
            if not source.is_file():
                continue
            target = package_directory / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            entry["files"].append({"path": str(target.relative_to(destination)).replace("\\", "/"),
                                   "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
        if not entry["files"]:
            raise ValueError(f"No installed license document found for {package} {distribution.version}.")
        manifest.append(entry)
    shutil.copy2(root / "tools/python-package/tools/LICENSE.txt", destination / "Python-LICENSE.txt")
    shutil.copy2(root / "mcp/requirements.lock", destination / "requirements.lock")
    (destination / "manifest.json").write_text(json.dumps({"scope": "Pinned runtime and build environment, including vendored notices",
                                                          "packages": manifest}, indent=2), encoding="utf-8")
    print(f"Collected license documents for {len(manifest)} distributions.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    collect(parser.parse_args().output)
