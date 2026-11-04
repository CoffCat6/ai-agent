#!/usr/bin/env python3
"""Validate the CMake source lists against the files on disk.

Checks (all scoped to the repository's ``src/`` tree):
  1. every ``src/**/*.cpp`` file appears in at least one CMake source list;
  2. every ``src/...`` path referenced by CMake actually exists on disk;
  3. no ``src/...`` path is listed twice inside the same CMakeLists.txt.

Glob entries (``src/ui/*.qml``) are ignored: they are not explicit source
lists and cannot be checked file-by-file.

Usage:
    python scripts/check_sources.py [--root DIR]

Exit codes: 0 = clean, 1 = differences found (printed), 2 = usage/IO error.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path, PurePosixPath

# Directories that hold generated/third-party content, never source lists.
SKIP_DIRS = {
    ".git",
    ".github",
    ".vs",
    ".idea",
    "build",
    "deploy",
    "Testing",
    "node_modules",
}

# A CMake-literal src path. Handles "${CMAKE_SOURCE_DIR}/src/...", relative
# "../src/..." and plain "src/...". Paths containing wildcards are skipped
# later (they are globs, not source lists).
SRC_PATH_RE = re.compile(r"(?P<path>(?:(?:\$\{CMAKE_SOURCE_DIR\}|\.\./)*)src/[^\s\"'();\]]+)")


def strip_comments(text: str) -> str:
    """Remove CMake line comments while preserving quoted strings."""
    out: list[str] = []
    in_string = False
    i = 0
    while i < len(text):
        ch = text[i]
        if in_string:
            out.append(ch)
            if ch == "\\" and i + 1 < len(text):
                i += 1
                out.append(text[i])
            elif ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if ch == "#":
            while i < len(text) and text[i] not in "\r\n":
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def normalize(path: str) -> str:
    """Strip CMake wrappers and return a repo-relative POSIX path."""
    path = path.strip().strip('"').strip("'")
    while path.startswith("${CMAKE_SOURCE_DIR}/"):
        path = path[len("${CMAKE_SOURCE_DIR}/"):]
    while path.startswith("../"):
        path = path[3:]
    while path.startswith("./"):
        path = path[2:]
    return path.replace("\\", "/")


def iter_cmake_files(root: Path):
    for path in sorted(root.rglob("CMakeLists.txt")):
        rel = path.relative_to(root)
        if any(part in SKIP_DIRS for part in rel.parts[:-1]):
            continue
        if rel.parts and rel.parts[0].startswith("build"):
            continue
        yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=None, help="repository root (default: parent of scripts/)")
    args = parser.parse_args()

    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parent.parent
    if not (root / "CMakeLists.txt").is_file():
        print(f"[check_sources] ERROR: {root} does not look like the repo root (no CMakeLists.txt)")
        return 2

    # 1. Files on disk.
    disk_cpp = sorted(
        p.relative_to(root).as_posix() for p in (root / "src").rglob("*.cpp") if p.is_file()
    )

    # 2. Source lists referenced by CMake.
    listed: dict[str, list[str]] = defaultdict(list)  # path -> CMake files listing it
    duplicates: list[tuple[str, str, int]] = []       # (cmake rel path, src path, count)

    for cmake_file in iter_cmake_files(root):
        rel_cmake = cmake_file.relative_to(root).as_posix()
        text = strip_comments(cmake_file.read_text(encoding="utf-8", errors="replace"))
        per_file: Counter[str] = Counter()
        for match in SRC_PATH_RE.finditer(text):
            raw = match.group("path")
            if "*" in raw or "?" in raw or "$" in raw:
                continue
            path = normalize(raw)
            if not path.startswith("src/"):
                continue
            # Only files have an extension; bare paths (src/viewmodels) are
            # include directories, not source entries.
            if not PurePosixPath(path).suffix:
                continue
            per_file[path] += 1
            if rel_cmake not in listed[path]:
                listed[path].append(rel_cmake)
        for path, count in per_file.items():
            if count > 1:
                duplicates.append((rel_cmake, path, count))

    listed_cpp = {p for p in listed if p.endswith(".cpp")}
    missing_in_cmake = sorted(set(disk_cpp) - listed_cpp)
    not_on_disk = sorted(p for p in listed if not (root / p).is_file())

    print(f"[check_sources] repo: {root}")
    print(f"[check_sources] src/**/*.cpp on disk: {len(disk_cpp)}")
    print(f"[check_sources] src/*.cpp listed in CMake: {len(listed_cpp)}")
    print(f"[check_sources] src paths referenced by CMake: {len(listed)}")

    failed = False

    if missing_in_cmake:
        failed = True
        print(f"\nMISSING IN CMake ({len(missing_in_cmake)}): these .cpp files exist on disk but are not in any source list")
        for path in missing_in_cmake:
            print(f"  - {path}")

    if not_on_disk:
        failed = True
        print(f"\nNOT ON DISK ({len(not_on_disk)}): CMake lists these src paths but the files do not exist")
        for path in not_on_disk:
            print(f"  - {path}  (listed in {', '.join(listed[path])})")

    if duplicates:
        failed = True
        print(f"\nDUPLICATES ({len(duplicates)}): a src path is listed more than once in the same CMakeLists.txt")
        for rel_cmake, path, count in duplicates:
            print(f"  - {path}  x{count} in {rel_cmake}")

    if failed:
        print("\n[check_sources] FAILED")
        return 1

    print("\n[check_sources] OK: CMake source lists match the src/ tree")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except OSError as exc:
        print(f"[check_sources] ERROR: {exc}", file=sys.stderr)
        sys.exit(2)