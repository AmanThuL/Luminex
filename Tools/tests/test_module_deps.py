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
        (root / "compile_commands.json").write_text("[]", encoding="utf-8")
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


INCLUDE_CONTRACT = {
    "schemaVersion": 1,
    "roots": ["Source", "RHI"],
    "budgets": {"production": 1000},
    "units": {
        "core": {"paths": ["Source/Core"], "targets": ["Core"], "units": [], "thirdParty": ["spdlog"]},
        "rhi-public": {"paths": ["RHI/Include"], "targets": ["RHI"], "units": [], "thirdParty": []},
        "backend": {
            "paths": ["RHI/Backends"],
            "targets": ["RHI"],
            "units": ["rhi-public"],
            "thirdParty": ["metal-cpp"],
        },
        "asset": {
            "paths": ["Source/Engine"],
            "targets": ["Engine"],
            "units": ["core"],
            "headers": ["RHI/Format.h"],
            "thirdParty": ["stb"],
        },
    },
    "targets": {"Core": {"deps": []}, "RHI": {"deps": ["Core"]}, "Engine": {"deps": ["Core"]}},
}

PACKAGE_STEM = ".xmake/packages/s/spdlog/v1.17.0/f028856e7c66484a8fbaa9d2364f2244/include"

INCLUDE_TREE = {
    "Source/Core/Log.h": "#include <spdlog/spdlog.h>\n",
    "RHI/Include/RHI/Format.h": "",
    "RHI/Include/RHI/Device.h": "",
    "RHI/Backends/Metal4/Source/Metal4Common.h": "",
    "RHI/Backends/Metal4/Source/Metal4Device.cpp": '#include "Metal4Common.h"\n#include <Metal/Metal.hpp>\n',
    "Source/Metal4Common.h": "",
    "Source/Engine/Allowed.h": '#include "RHI/Format.h"\n',
    "Source/Engine/Bad.h": '#include "RHI/Device.h"\n',
    "Source/Engine/Mid.h": '#include "Engine/Bad.h"\n',
    "Source/Engine/Uses.h": '#include "Core/Log.h"\n',
    "Source/Engine/Widget.h": "#include <imgui_impl_sdl3.h>\n",
    "Source/Engine/Absent.h": "#include <nowhere/absent.h>\n",
    "ThirdParty/imgui/backends/imgui_impl_sdl3.h": "",
    "ThirdParty/metal-cpp/Metal/Metal.hpp": "",
    f"{PACKAGE_STEM}/spdlog/spdlog.h": "",
}

INCLUDE_DIRS = [
    "Source",
    "RHI/Include",
    "RHI/Backends/Metal4/Source",
    "ThirdParty/imgui",
    "ThirdParty/imgui/backends",
    "ThirdParty/metal-cpp",
    PACKAGE_STEM,
]


def write_include_tree(root: Path) -> None:
    for name, body in INCLUDE_TREE.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")


def include_entry(root: Path, file: str) -> dict:
    """A compilation entry carrying the tree's include directories, joined and split by turns."""
    arguments = ["clang++", "-c", "-std=c++23"]
    for index, name in enumerate(INCLUDE_DIRS):
        if index % 2 == 0:
            arguments.append(f"-I{name}")
        else:
            arguments.extend(["-I", name])
    arguments.extend(["-o", "out.o", file])
    return {"directory": str(root), "arguments": arguments, "file": file}


def include_db(root: Path) -> dict[str, dict]:
    compiled = ["Source/Core/Log.cpp", "RHI/Backends/Metal4/Source/Metal4Device.cpp", "Source/Engine/Engine.cpp"]
    return {file: include_entry(root, file) for file in compiled}


class IncludeResolutionTests(unittest.TestCase):
    def test_include_dirs_reads_joined_and_split_tokens_and_the_command_string(self) -> None:
        entry = {
            "directory": "/repo",
            "arguments": ["clang++", "-IRHI/Include", "-isystem", "/pkg/include", "-iframeworkFrames"],
        }
        self.assertEqual(
            modules.include_dirs(entry),
            [Path("/repo/RHI/Include"), Path("/pkg/include"), Path("/repo/Frames")],
        )

    def test_command_string_entries_load_like_argument_entries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "compile_commands.json"
            entry = {"directory": "/repo", "command": "clang++ -c -ISource a.cpp", "file": "a.cpp"}
            path.write_text(json.dumps([entry]), encoding="utf-8")
            database = modules.load_compile_commands(path)
            self.assertEqual(modules.include_dirs(database["a.cpp"]), [Path("/repo/Source")])

    def test_quoted_include_resolves_next_to_the_includer_before_the_include_directories(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            write_include_tree(root)
            contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
            dirs = modules.include_dirs(include_entry(root, "x.cpp"))
            resolved = modules.resolve_include(
                Path("RHI/Backends/Metal4/Source/Metal4Device.cpp"), "Metal4Common.h", True, dirs, root, contract
            )
            self.assertEqual(resolved.kind, "project")
            self.assertEqual(resolved.path, Path("RHI/Backends/Metal4/Source/Metal4Common.h"))
            self.assertEqual(resolved.unit, "backend")

    def test_angle_include_takes_the_package_or_the_third_party_directory_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            write_include_tree(root)
            contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
            dirs = modules.include_dirs(include_entry(root, "x.cpp"))
            includer = Path("Source/Engine/Widget.h")
            for spec, name in (
                ("spdlog/spdlog.h", "spdlog"),
                ("imgui_impl_sdl3.h", "imgui"),
                ("Metal/Metal.hpp", "metal-cpp"),
            ):
                resolved = modules.resolve_include(includer, spec, False, dirs, root, contract)
                self.assertEqual((resolved.kind, resolved.name), ("third-party", name), spec)

    def test_unresolvable_angle_include_is_a_system_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            write_include_tree(root)
            contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
            dirs = modules.include_dirs(include_entry(root, "x.cpp"))
            resolved = modules.resolve_include(Path("Source/Engine/Absent.h"), "vector", False, dirs, root, contract)
            self.assertEqual(resolved, modules.Resolved("system", None, None, None))

    def test_parse_includes_reads_quoted_and_angle_specs_in_order(self) -> None:
        text = '#pragma once\n#include "Engine/Asset.h"\n#  include <vector>\nint x; // #include "no.h"\n'
        self.assertEqual(modules.parse_includes(text), [("Engine/Asset.h", True), ("vector", False)])

    def test_third_party_name_maps_package_and_vendored_directories(self) -> None:
        root = Path("/repo")
        self.assertEqual(modules.third_party_name(Path(f"/cache/{PACKAGE_STEM}/spdlog/spdlog.h"), root), "spdlog")
        self.assertEqual(
            modules.third_party_name(Path("/repo/ThirdParty/imgui/backends/imgui_impl_metal4.h"), root), "imgui"
        )
        self.assertIsNone(modules.third_party_name(Path("/opt/homebrew/include/SDL3/SDL.h"), root))


class IncludeCheckTests(unittest.TestCase):
    def check(self, root: Path, names: list[str], allowlist: list[dict] | None = None) -> list[str]:
        contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
        errors: list[str] = []
        modules.check_includes(
            [Path(name) for name in names], {}, include_db(root), contract, allowlist or [], errors, root
        )
        return errors

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        write_include_tree(self.root)

    def test_reach_through_two_project_headers_reports_the_chain(self) -> None:
        errors = self.check(self.root, ["Source/Engine/Mid.h"])
        self.assertEqual(
            errors,
            [
                "Source/Engine/Mid.h: asset reaches rhi-public via "
                "Source/Engine/Mid.h -> Source/Engine/Bad.h -> RHI/Include/RHI/Device.h"
            ],
        )

    def test_header_allowance_admits_the_named_header_but_not_its_neighbour(self) -> None:
        self.assertEqual(self.check(self.root, ["Source/Engine/Allowed.h"]), [])
        self.assertEqual(len(self.check(self.root, ["Source/Engine/Bad.h"])), 1)

    def test_third_party_reached_through_a_project_header_is_not_a_direct_include(self) -> None:
        self.assertEqual(self.check(self.root, ["Source/Engine/Uses.h"]), [])

    def test_third_party_named_by_the_file_itself_is_a_direct_include(self) -> None:
        self.assertEqual(
            self.check(self.root, ["Source/Engine/Widget.h"]),
            ["Source/Engine/Widget.h: asset includes imgui directly"],
        )

    def test_allowlist_entry_suppresses_exactly_one_violation(self) -> None:
        allowlist = [
            {
                "kind": "include",
                "file": "Source/Engine/Bad.h",
                "reaches": "rhi-public",
                "reason": "the loader still names a device",
                "until": "R1.2",
            }
        ]
        errors = self.check(self.root, ["Source/Engine/Bad.h", "Source/Engine/Mid.h"], allowlist)
        self.assertEqual(len(errors), 1)
        self.assertIn("Source/Engine/Mid.h", errors[0])
        self.assertTrue(allowlist[0].get("used"))

    def test_unused_allowlist_entry_is_an_error(self) -> None:
        allowlist = [
            {"kind": "include", "file": "Source/Engine/Gone.h", "reaches": "render", "reason": "r", "until": "R1.4"}
        ]
        self.check(self.root, ["Source/Engine/Allowed.h"], allowlist)
        errors: list[str] = []
        modules.check_allowlist_use(Path("Tools/module_allowlist.json"), allowlist, errors)
        self.assertEqual(
            errors, ["Tools/module_allowlist.json: unused entry include Source/Engine/Gone.h (R1.4)"]
        )

    def test_backend_metal_cpp_include_is_allowed_and_the_quoted_neighbour_resolves(self) -> None:
        self.assertEqual(self.check(self.root, ["RHI/Backends/Metal4/Source/Metal4Device.cpp"]), [])


class HardeningTests(unittest.TestCase):
    def test_unreadable_json_is_a_could_not_run_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "contract.json"
            path.write_bytes(b"\xff\xfe not text")
            with self.assertRaises(modules.ModuleContractError):
                modules.read_json(path)
            with self.assertRaises(modules.ModuleContractError):
                modules.load_targets(path, root)

    def test_a_path_owned_by_two_units_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h", "Source/App/Options.cpp"])
            contract = json.loads(json.dumps(CONTRACT))
            contract["units"]["app-shell"]["paths"].append("Source/Core")
            with self.assertRaisesRegex(modules.ModuleContractError, "Source/Core"):
                modules.load_contract(write_contract(root, contract), root)

    def test_nested_directory_entry_beats_its_parent(self) -> None:
        contract = {
            "units": {
                "app-shell": {"paths": ["Source/App"]},
                "app-model": {"paths": ["Source/App/Model"]},
            }
        }
        self.assertEqual(modules.owner_of(Path("Source/App/Model/Options.cpp"), contract), "app-model")
        self.assertEqual(modules.owner_of(Path("Source/App/Shell.cpp"), contract), "app-shell")

    def test_a_sibling_directory_with_a_shared_prefix_does_not_match(self) -> None:
        contract = {"units": {"app-shell": {"paths": ["Source/App"]}}}
        self.assertIsNone(modules.owner_of(Path("Source/AppX/x.cpp"), contract))



if __name__ == "__main__":
    unittest.main()
