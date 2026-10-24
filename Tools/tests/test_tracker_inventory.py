from __future__ import annotations

import contextlib
import copy
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from Tools.check_tracker_inventory import REQUIRED_CASES, TrackerInventoryError, check_inventory, main


def inventory(version: int = 2) -> dict:
    cases = [{"name": name} for name in REQUIRED_CASES]
    cases.append({"name": "cached aliases remain synchronized across paced frames"})
    if version == 1:
        for case in cases:
            case.update({"class-name": "", "tags": ["tracker"],
                         "source-location": {"filename": "RendererTrackerTests.cpp", "line": 1}})
    return {"version": version, "listings": {"tests": cases}}


class TrackerInventoryTests(unittest.TestCase):
    def test_both_supported_listing_versions(self) -> None:
        for version in (1, 2):
            with self.subTest(version=version):
                self.assertEqual(check_inventory(inventory(version)), 3)

    def test_version_two_optional_listing_details_are_not_required(self) -> None:
        value = inventory()
        for case in value["listings"]["tests"]:
            case["tags"] = ["tracker"]
        self.assertEqual(check_inventory(value), 3)

    def test_either_missing_renderer_case_fails_even_with_alias(self) -> None:
        for required in REQUIRED_CASES:
            with self.subTest(required=required):
                value = inventory()
                value["listings"]["tests"] = [case for case in value["listings"]["tests"]
                                               if case["name"] != required]
                with self.assertRaisesRegex(TrackerInventoryError, "missing or duplicated"):
                    check_inventory(value)

    def test_alias_only_fails(self) -> None:
        value = inventory()
        value["listings"]["tests"] = value["listings"]["tests"][-1:]
        with self.assertRaisesRegex(TrackerInventoryError, "missing or duplicated"):
            check_inventory(value)

    def test_duplicate_required_cases_fail(self) -> None:
        for index in (0, 1):
            with self.subTest(index=index):
                value = inventory()
                value["listings"]["tests"].append(copy.deepcopy(value["listings"]["tests"][index]))
                with self.assertRaisesRegex(TrackerInventoryError, "missing or duplicated"):
                    check_inventory(value)

    def test_duplicate_other_cases_fail(self) -> None:
        value = inventory()
        value["listings"]["tests"].append(copy.deepcopy(value["listings"]["tests"][-1]))
        with self.assertRaisesRegex(TrackerInventoryError, "duplicate case names"):
            check_inventory(value)

    def test_unknown_and_noninteger_versions_fail(self) -> None:
        for version in (None, 0, 3, -1, True, False, 1.0, 2.0, "1", "2", [], {}):
            with self.subTest(version=version):
                value = inventory()
                value["version"] = version
                with self.assertRaisesRegex(TrackerInventoryError, "unsupported Catch2"):
                    check_inventory(value)

    def test_missing_version_fails(self) -> None:
        value = inventory()
        del value["version"]
        with self.assertRaisesRegex(TrackerInventoryError, "unsupported Catch2"):
            check_inventory(value)

    def test_malformed_listing_structure_fails(self) -> None:
        for value in (None, [], "", {}, {"version": 1}, {"version": 2, "listings": []},
                      {"version": 1, "listings": {}},
                      {"version": 2, "listings": {"tests": {}}},
                      {"version": 2, "listings": {"tests": []}}):
            with self.subTest(value=value), self.assertRaises(TrackerInventoryError):
                check_inventory(value)

    def test_every_case_requires_nonempty_string_name(self) -> None:
        for case in (None, [], "case", {}, {"name": None}, {"name": 1}, {"name": ""}, {"name": "  "}):
            with self.subTest(case=case):
                value = inventory()
                value["listings"]["tests"].append(case)
                with self.assertRaisesRegex(TrackerInventoryError, "nonempty name"):
                    check_inventory(value)

    def run_main(self, text: str) -> tuple[int, str, str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "inventory.json"
            path.write_text(text, encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                result = main([str(path)])
            return result, out.getvalue(), err.getvalue()

    def test_main_validates_producer_file(self) -> None:
        result, out, err = self.run_main(json.dumps(inventory()))
        self.assertEqual(result, 0)
        self.assertIn("3 cases", out)
        self.assertEqual(err, "")

    def test_invalid_json_fails_without_success_message(self) -> None:
        result, out, err = self.run_main('{"version":')
        self.assertEqual(result, 1)
        self.assertEqual(out, "")
        self.assertIn("tracker inventory check failed", err)

    def test_missing_file_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with contextlib.redirect_stderr(io.StringIO()) as err:
                result = main([str(Path(directory) / "missing.json")])
        self.assertEqual(result, 1)
        self.assertIn("tracker inventory check failed", err.getvalue())

    def test_cli_returns_failure_for_missing_case(self) -> None:
        value = inventory()
        value["listings"]["tests"].pop(0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "inventory.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            script = Path(__file__).resolve().parents[1] / "check_tracker_inventory.py"
            completed = subprocess.run([sys.executable, str(script), str(path)],
                                       capture_output=True, text=True, check=False)
        self.assertEqual(completed.returncode, 1)
        self.assertIn("missing or duplicated", completed.stderr)
        self.assertEqual(completed.stdout, "")


if __name__ == "__main__":
    unittest.main()
