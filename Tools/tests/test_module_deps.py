from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from Tools import check_module_deps as modules


CONTRACT = {
    "schemaVersion": 1,
    "roots": ["Source"],
    "budgets": {"production": 1000, "tests": 1500},
    "units": {
        "core": {"paths": ["Source/Core"], "targets": ["Core"], "units": [], "thirdParty": ["spdlog"]},
        "app-shell": {
            "paths": ["Source/App"],
            "targets": ["App"],
            "units": ["core"],
            "thirdParty": ["imgui"],
        },
        "app-model": {
            "paths": ["Source/App/Options.h", "Source/App/Options.cpp"],
            "targets": ["App"],
            "units": ["core"],
            "thirdParty": [],
        },
    },
    "targets": {
        "Core": {"deps": []},
        "App": {"deps": ["Core"]},
        "Tests": {"deps": ["Core"]},
    },
}


def write_tree(root: Path, files: list[str]) -> None:
    for name in files:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("", encoding="utf-8")


def run_main(argv: list[str]) -> int:
    """Run the checker with its reporting captured so the test output stays quiet."""
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return modules.main(argv)


def write_contract(root: Path, contract: dict) -> Path:
    path = root / "contract.json"
    path.write_text(json.dumps(contract), encoding="utf-8")
    return path


class ContractLoadingTests(unittest.TestCase):
    def test_contract_round_trips_and_normalises_optional_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h", "Source/App/Options.cpp"])
            contract = modules.load_contract(write_contract(root, CONTRACT), root)
            self.assertEqual(contract["schemaVersion"], 1)
            self.assertEqual(sorted(contract["units"]), ["app-model", "app-shell", "core"])
            self.assertEqual(contract["units"]["core"]["headers"], [])
            self.assertEqual(contract["units"]["app-shell"]["thirdParty"], ["imgui"])

    def test_contract_path_that_does_not_exist_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h"])
            with self.assertRaisesRegex(modules.ModuleContractError, "Source/App/Options.cpp"):
                modules.load_contract(write_contract(root, CONTRACT), root)

    def test_unknown_unit_reference_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h"])
            contract = {
                "schemaVersion": 1,
                "roots": ["Source"],
                "budgets": {"production": 1000, "tests": 1500},
                "units": {"core": {"paths": ["Source/Core"], "targets": ["Core"], "units": ["ghost"]}},
                "targets": {"Core": {"deps": []}},
            }
            with self.assertRaisesRegex(modules.ModuleContractError, "ghost"):
                modules.load_contract(write_contract(root, contract), root)

    def test_unsupported_schema_version_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = dict(CONTRACT, schemaVersion=2)
            with self.assertRaisesRegex(modules.ModuleContractError, "schemaVersion"):
                modules.load_contract(write_contract(root, contract), root)


class AllowlistTests(unittest.TestCase):
    def write_allowlist(self, root: Path, entries: list[dict]) -> Path:
        path = root / "allowlist.json"
        path.write_text(json.dumps({"schemaVersion": 1, "entries": entries}), encoding="utf-8")
        return path

    def test_empty_allowlist_loads_as_no_entries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(modules.load_allowlist(self.write_allowlist(root, [])), [])

    def test_every_supported_kind_loads(self) -> None:
        entries = [
            {"kind": "shared-source", "file": "a.cpp", "targets": ["App", "Tests"], "reason": "r", "until": "R1.3"},
            {"kind": "include", "file": "a.cpp", "reaches": "imgui", "reason": "r", "until": "R1.3"},
            {"kind": "target-dep", "target": "TextureBake", "dep": "Render", "reason": "r", "until": "R1.2"},
            {"kind": "framework", "target": "TextureBake", "framework": "Metal", "reason": "r", "until": "R1.2"},
            {"kind": "header", "file": "a.h", "reason": "r", "until": "R1.4"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_allowlist(Path(directory), entries)
            self.assertEqual(len(modules.load_allowlist(path)), 5)

    def test_unknown_kind_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_allowlist(Path(directory), [{"kind": "wish", "reason": "r", "until": "R1.3"}])
            with self.assertRaisesRegex(modules.ModuleContractError, "wish"):
                modules.load_allowlist(path)

    def test_entry_without_until_is_an_error(self) -> None:
        entry = {"kind": "header", "file": "a.h", "reason": "r"}
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_allowlist(Path(directory), [entry])
            with self.assertRaisesRegex(modules.ModuleContractError, "until"):
                modules.load_allowlist(path)

    def test_entry_missing_a_kind_field_is_an_error(self) -> None:
        entry = {"kind": "target-dep", "target": "TextureBake", "reason": "r", "until": "R1.2"}
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_allowlist(Path(directory), [entry])
            with self.assertRaisesRegex(modules.ModuleContractError, "dep"):
                modules.load_allowlist(path)


class OwnershipTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = json.loads(json.dumps(CONTRACT))
        for unit in self.contract["units"].values():
            unit.setdefault("headers", [])
            unit.setdefault("thirdParty", [])

    def test_file_entry_beats_the_directory_that_contains_it(self) -> None:
        self.assertEqual(modules.owner_of(Path("Source/App/Options.cpp"), self.contract), "app-model")
        self.assertEqual(modules.owner_of(Path("Source/App/Shell.cpp"), self.contract), "app-shell")
        self.assertEqual(modules.owner_of(Path("Source/Core/Log.cpp"), self.contract), "core")

    def test_unmapped_file_has_no_owner_and_is_reported(self) -> None:
        self.assertIsNone(modules.owner_of(Path("Source/Stray/Stray.cpp"), self.contract))
        errors: list[str] = []
        modules.check_ownership([Path("Source/Stray/Stray.cpp")], {}, self.contract, [], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("no unit owns", errors[0])

    def test_project_files_lists_sources_and_headers_under_the_roots(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.cpp", "Source/Core/Log.h", "Source/Core/notes.md", "Docs/Other.cpp"])
            self.assertEqual(
                modules.project_files(root, self.contract),
                [Path("Source/Core/Log.cpp"), Path("Source/Core/Log.h")],
            )

    def test_source_compiled_by_two_targets_is_an_error(self) -> None:
        targets = {
            "App": {"files": ["Source/App/Options.cpp"], "deps": [], "packages": [], "frameworks": []},
            "Tests": {"files": ["Source/App/Options.cpp"], "deps": [], "packages": [], "frameworks": []},
        }
        errors: list[str] = []
        modules.check_ownership([Path("Source/App/Options.cpp")], targets, self.contract, [], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("App, Tests", errors[0])

    def test_shared_source_allowlist_entry_accepts_the_two_targets(self) -> None:
        targets = {
            "App": {"files": ["Source/App/Options.cpp"], "deps": [], "packages": [], "frameworks": []},
            "Tests": {"files": ["Source/App/Options.cpp"], "deps": [], "packages": [], "frameworks": []},
        }
        allowlist = [
            {
                "kind": "shared-source",
                "file": "Source/App/Options.cpp",
                "targets": ["App", "Tests"],
                "reason": "the model library does not exist yet",
                "until": "R1.3",
            }
        ]
        errors: list[str] = []
        modules.check_ownership([Path("Source/App/Options.cpp")], targets, self.contract, allowlist, errors)
        self.assertEqual(errors, [])

    def test_source_compiled_by_a_target_outside_its_unit_is_an_error(self) -> None:
        targets = {"Tests": {"files": ["Source/Core/Log.cpp"], "deps": [], "packages": [], "frameworks": []}}
        errors: list[str] = []
        modules.check_ownership([Path("Source/Core/Log.cpp")], targets, self.contract, [], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("Tests", errors[0])
        self.assertIn("core", errors[0])

    def test_header_that_no_target_compiles_is_not_an_error(self) -> None:
        errors: list[str] = []
        modules.check_ownership([Path("Source/App/Options.h")], {}, self.contract, [], errors)
        self.assertEqual(errors, [])


class TargetDumpTests(unittest.TestCase):
    def test_single_valued_dependency_string_is_read_as_a_one_element_list(self) -> None:
        dump = {"RHI": {"kind": "static", "files": "RHI/Source/Device.cpp", "deps": "Core", "packages": {}, "frameworks": "Metal"}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "targets.json"
            path.write_text("configuring\n" + json.dumps(dump) + "\n", encoding="utf-8")
            targets = modules.load_targets(path, root)
            self.assertEqual(targets["RHI"]["deps"], ["Core"])
            self.assertEqual(targets["RHI"]["packages"], [])
            self.assertEqual(targets["RHI"]["frameworks"], ["Metal"])
            self.assertEqual(targets["RHI"]["files"], ["RHI/Source/Device.cpp"])

    def test_malformed_dump_is_a_could_not_run_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "targets.json"
            path.write_text("not json at all\n", encoding="utf-8")
            with self.assertRaises(modules.ModuleContractError):
                modules.load_targets(path, root)


class MainTests(unittest.TestCase):
    def build_tree(self, root: Path) -> tuple[Path, Path, Path]:
        write_tree(root, ["Source/Core/Log.cpp", "Source/App/Options.h", "Source/App/Options.cpp", "Source/App/Shell.cpp"])
        contract = write_contract(root, CONTRACT)
        allowlist = root / "allowlist.json"
        allowlist.write_text(json.dumps({"schemaVersion": 1, "entries": []}), encoding="utf-8")
        dump = {
            "Core": {"kind": "static", "files": ["Source/Core/Log.cpp"], "deps": {}, "packages": {}, "frameworks": {}},
            "App": {
                "kind": "binary",
                "files": ["Source/App/Options.cpp", "Source/App/Shell.cpp"],
                "deps": "Core",
                "packages": {},
                "frameworks": {},
            },
        }
        targets = root / "targets.json"
        targets.write_text(json.dumps(dump), encoding="utf-8")
        return contract, allowlist, targets

    def test_clean_tree_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            code = run_main(
                ["--root", str(root), "--contract", str(contract), "--allowlist", str(allowlist), "--targets", str(targets)]
            )
            self.assertEqual(code, 0)

    def test_violation_returns_one(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            write_tree(root, ["Source/Stray.cpp"])
            code = run_main(
                ["--root", str(root), "--contract", str(contract), "--allowlist", str(allowlist), "--targets", str(targets)]
            )
            self.assertEqual(code, 1)

    def test_missing_target_dump_returns_two(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, _ = self.build_tree(root)
            code = run_main(
                [
                    "--root", str(root),
                    "--contract", str(contract),
                    "--allowlist", str(allowlist),
                    "--targets", str(root / "absent.json"),
                ]
            )
            self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
