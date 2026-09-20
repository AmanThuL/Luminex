from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from Tools.check_submodule_pin import check

IDENTITY = ["-c", "user.name=Test", "-c", "user.email=test@example.invalid"]
LOCAL_URL = ["-c", "protocol.file.allow=always"]


def run(root: Path, *args: str) -> str:
    """Run one git command that must succeed, and return its standard output."""
    completed = subprocess.run(
        ["git", "-C", str(root), *IDENTITY, *LOCAL_URL, *args],
        capture_output=True,
        text=True,
        check=True,
    )
    return completed.stdout.strip()


def commit(root: Path, name: str) -> str:
    (root / name).write_text(name, encoding="utf-8")
    run(root, "add", name)
    run(root, "commit", "-q", "-m", name)
    return run(root, "rev-parse", "HEAD")


class SubmodulePinFixture(unittest.TestCase):
    """A superproject mounting a local component repository, both built with `git init`."""

    def setUp(self) -> None:
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        self.component = Path(directory) / "component"
        self.superproject = Path(directory) / "superproject"

        self.component.mkdir()
        run(self.component, "init", "-q", "-b", "main")
        commit(self.component, "first")
        self.main_pin = commit(self.component, "second")
        run(self.component, "checkout", "-q", "-b", "side")
        self.side_pin = commit(self.component, "side-only")
        run(self.component, "checkout", "-q", "main")

        self.superproject.mkdir()
        run(self.superproject, "init", "-q", "-b", "main")
        commit(self.superproject, "root")
        run(self.superproject, "submodule", "add", "-q", str(self.component), "RojoRHI")
        self.mount = self.superproject / "RojoRHI"

    def pin(self, revision: str) -> None:
        """Point the superproject's committed gitlink at one component revision."""
        run(self.mount, "checkout", "-q", revision)
        run(self.superproject, "add", ".gitmodules", "RojoRHI")
        run(self.superproject, "commit", "-q", "-m", "mount")


class SubmodulePinTests(SubmodulePinFixture):
    """`check` accepts only a mounted pin that the component's `origin/main` reaches."""

    def test_a_pin_on_origin_main_passes(self) -> None:
        self.pin(self.main_pin)
        self.assertEqual(check(self.superproject, url=str(self.component)), [])

    def test_a_pin_on_a_side_branch_fails(self) -> None:
        self.pin(self.side_pin)
        errors = check(self.superproject, url=str(self.component))
        self.assertTrue(any("not reachable" in error for error in errors))

    def test_a_url_other_than_the_component_fails(self) -> None:
        self.pin(self.main_pin)
        errors = check(self.superproject, url="https://example.invalid/other.git")
        self.assertTrue(any("example.invalid/other.git" in error for error in errors))

    def test_an_uninitialised_submodule_asks_for_the_init(self) -> None:
        self.pin(self.main_pin)
        shutil.rmtree(self.mount)
        errors = check(self.superproject, url=str(self.component))
        self.assertTrue(any("git submodule update --init" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
