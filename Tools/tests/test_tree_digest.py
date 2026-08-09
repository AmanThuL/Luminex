from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from Tools.tree_digest import tree_digest


class TreeDigestTests(unittest.TestCase):
    def test_digest_is_stable_and_covers_paths_and_contents(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nested").mkdir()
            (root / "b.txt").write_text("second", encoding="utf-8")
            (root / "nested" / "a.txt").write_text("first", encoding="utf-8")

            initial = tree_digest(root)
            self.assertEqual(initial, tree_digest(root))

            (root / "nested" / "a.txt").write_text("changed", encoding="utf-8")
            self.assertNotEqual(initial, tree_digest(root))
