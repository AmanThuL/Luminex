#!/usr/bin/env python3
"""Fails unless RojoRHI's pinned commit is reachable from the component's origin/main."""
from __future__ import annotations
import argparse, configparser, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOUNT = "RojoRHI"
URL = "https://github.com/AmanThuL/rojo-rhi.git"

def git(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)

def check(root: Path, url: str = URL, fetch: bool = False) -> list[str]:
    modules = configparser.ConfigParser()
    modules.read(root / ".gitmodules", encoding="utf-8")
    section = f'submodule "{MOUNT}"'
    if not modules.has_section(section) or modules[section].get("url") != url:
        return [f".gitmodules must name {url} for {MOUNT}"]
    entry = git(root, "ls-tree", "HEAD", MOUNT).stdout.split()
    if len(entry) < 3 or entry[1] != "commit":
        return [f"{MOUNT} is not a submodule entry in HEAD"]
    mount = root / MOUNT
    if not (mount / ".git").exists():
        return [f"{MOUNT}/ is empty: run `git submodule update --init`"]
    if fetch and git(mount, "fetch", "-q", "origin", "main").returncode != 0:
        return [f"cannot fetch origin main in {MOUNT}"]
    if git(mount, "merge-base", "--is-ancestor", entry[2], "origin/main").returncode != 0:
        return [f"{MOUNT} pin {entry[2][:12]} is not reachable from rojo-rhi main"]
    return []

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true")
    errors = check(ROOT, fetch=parser.parse_args().fetch)
    print("\n".join(errors) if errors else "submodule pin passed")
    sys.exit(1 if errors else 0)
