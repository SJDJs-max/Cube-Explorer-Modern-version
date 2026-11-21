"""Verify packaged hashes, ZIP integrity, source completeness and offline help links."""
import hashlib
from html.parser import HTMLParser
import json
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit
import zipfile

root = Path(__file__).resolve().parents[1]
release = root / "dist/CubeExplorerModern"
manifest = json.loads((release / "SHA256SUMS.json").read_text(encoding="utf-8"))
for row in manifest:
    file = release / row["path"]
    assert file.resolve().is_relative_to(release.resolve()), row["path"]
    with file.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    assert file.stat().st_size == row["bytes"] and digest == row["sha256"], row["path"]

class HelpLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []
    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if "id" in attributes:
            self.ids.add(attributes["id"])
        if tag == "a" and "href" in attributes:
            self.links.append(attributes["href"])

help_links_checked = 0
for help_name in ("help/index.html", "help/index.en.html"):
    help_file = release / help_name
    assert help_file.exists(), help_name
    parser = HelpLinks()
    parser.feed(help_file.read_text(encoding="utf-8"))
    for link in parser.links:
        url = urlsplit(link)
        if not url.scheme:
            if url.path:
                assert (help_file.parent / unquote(url.path)).exists(), f"{help_name}: {link}"
            elif url.fragment:
                assert url.fragment in parser.ids, f"{help_name}: {link}"
    help_links_checked += len(parser.links)

for file in (release / "corresponding-source.zip", root / "dist/CubeExplorerModern-win64.zip"):
    with zipfile.ZipFile(file) as archive:
        assert archive.testzip() is None, file
        if file.name == "corresponding-source.zip":
            for name in ("src/core/model.cpp", "src/core/twophase.cpp", "src/core/pattern.cpp", "src/core/optimal.cpp",
                         "src/core/template_search.cpp", "include/cube/template_search.hpp", "tests/user_regression_tests.cpp",
                         "src/app/mode_choice.hpp", "src/app/ui_localization.hpp", "src/app/theme_toggle.hpp",
                         "src/app/formula_window.cpp", "src/app/result_row.hpp", "src/app/main.cpp",
                         "assets/rubik.ico", "src/app/app.rc.in", "tests/pattern_performance_tests.cpp",
                         "src/engine/main.cpp", "mcp/server.py", "scripts/bootstrap.ps1", "scripts/build.ps1",
                         "scripts/package_release.py", "downloads/bootstrap-lock.json", "tables/original/phase1PF.prun"):
                assert name in archive.namelist(), name
archive = root / "dist/CubeExplorerModern-win64.zip"
with archive.open("rb") as stream:
    archive_digest = hashlib.file_digest(stream, "sha256").hexdigest()
assert archive.with_suffix(".zip.sha256").read_text().split()[0] == archive_digest
print(json.dumps({"status": "PASS", "manifest_files": len(manifest), "help_links": help_links_checked, "zip_crc": "PASS", "archive_sha256": archive_digest}))
