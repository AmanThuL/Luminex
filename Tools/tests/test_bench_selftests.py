"""Run the cost tools' own self-tests so hosted CI keeps them working; none collects timings."""
from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

BENCH = Path(__file__).resolve().parents[1] / "Bench"


class BenchSelftestTests(unittest.TestCase):
    def test_paired_cost_tools_pass_their_selftests(self) -> None:
        for tool in ("parent_paired.py", "lighting_paired.py", "visibility_paired.py", "frame_data_paired.py"):
            with self.subTest(tool=tool):
                result = subprocess.run([sys.executable, str(BENCH / tool), "--selftest"], cwd=BENCH,
                                        capture_output=True, text=True, timeout=300)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
