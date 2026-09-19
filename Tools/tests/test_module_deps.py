from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

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

    def test_forbidden_header_must_exist_so_a_stale_boundary_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h", "Source/App/Options.cpp"])
            for header in ("Source/Core/Missing.h", str(root / "Source/Core/Log.h"),
                           "Source/Core/./Log.h", "Source//Core/Log.h", "Source/Core/../Core/Log.h",
                           "Source/App/Options.cpp"):
                with self.subTest(header=header):
                    contract = json.loads(json.dumps(CONTRACT))
                    contract["units"]["app-model"]["forbidHeaders"] = [header]
                    with self.assertRaisesRegex(modules.ModuleContractError, "must name an existing repository header"):
                        modules.load_contract(write_contract(root, contract), root)

    def test_private_headers_require_canonical_existing_owned_unique_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h", "Source/App/Options.cpp"])
            invalid = ["Source/Core/Missing.h", str(root / "Source/Core/Log.h"),
                       "Source/Core/./Log.h", "Source//Core/Log.h", "Source/App/Options.cpp"]
            for headers in ([name] for name in invalid):
                with self.subTest(headers=headers):
                    contract = json.loads(json.dumps(CONTRACT))
                    contract["units"]["core"]["privateHeaders"] = headers
                    with self.assertRaises(modules.ModuleContractError):
                        modules.load_contract(write_contract(root, contract), root)
            for headers in (["Source/App/Options.h"], ["Source/Core/Log.h"] * 2, "Source/Core/Log.h"):
                with self.subTest(headers=headers):
                    contract = json.loads(json.dumps(CONTRACT))
                    contract["units"]["core"]["privateHeaders"] = headers
                    with self.assertRaises(modules.ModuleContractError):
                        modules.load_contract(write_contract(root, contract), root)

    def test_private_inventory_rejects_symlink_and_case_aliases(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/App/Options.h", "Source/App/Options.cpp"])
            (root / "Source/Core/Alias.h").symlink_to("Log.h")
            (root / "Source/AliasCore").symlink_to("Core", target_is_directory=True)
            aliases = ["Source/Core/Alias.h", "Source/AliasCore/Log.h"]
            if (root / "Source/Core/log.h").is_file():
                aliases += ["Source/Core/log.h", "source/Core/Log.h"]
            for alias in aliases:
                with self.subTest(alias=alias):
                    contract = json.loads(json.dumps(CONTRACT))
                    contract["units"]["core"]["privateHeaders"] = [alias]
                    with self.assertRaisesRegex(modules.ModuleContractError, "case or symlink alias"):
                        modules.load_contract(write_contract(root, contract), root)

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

    def test_serialised_used_flag_does_not_hide_a_stale_entry(self) -> None:
        entry = {"kind": "header", "file": "a.h", "reason": "r", "until": "R1.2", "used": True}
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_allowlist(Path(directory), [entry])
            self.assertFalse(modules.load_allowlist(path)[0].get("used"))

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

    def test_source_that_no_target_compiles_is_an_error(self) -> None:
        errors = []
        modules.check_ownership([Path("Source/Core/Log.cpp")], {}, self.contract, [], errors)
        self.assertEqual(errors, ["Source/Core/Log.cpp: no target compiles this source"])

    def test_target_source_outside_the_walked_roots_is_not_exempt(self) -> None:
        errors = []
        modules.check_ownership([], {"Core": {"files": ["Other/Log.cpp"]}}, self.contract, [], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("no unit owns", errors[0])

    def test_shared_source_still_needs_its_owning_target(self) -> None:
        file = "Source/App/Options.cpp"
        targets = {name: {"files": [file]} for name in ("Tests", "Other")}
        allowance = {"kind": "shared-source", "file": file, "targets": ["Tests", "Other"]}
        errors = []
        modules.check_ownership([Path(file)], targets, self.contract, [allowance], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("no compiling target belongs to unit app-model", errors[0])


class TargetDumpTests(unittest.TestCase):
    def test_target_file_is_relative_to_the_selected_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "targets.json"
            path.write_text(json.dumps({"App": {"targetfile": "build/App"}}))
            self.assertEqual(modules.load_targets(path, root)["App"]["targetfile"], str(root / "build/App"))

    def test_xmake_dump_pins_the_selected_project_root(self) -> None:
        with patch.object(modules.subprocess, "run", return_value=FakeCompleted(stdout="{}")) as run:
            modules.run_target_dump(Path("/repo/worktree"))
        self.assertEqual(run.call_args.args[0], ["xmake", "lua", "-P", "/repo/worktree", modules.TARGET_DUMP_SCRIPT])

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
        entries = [
            {"directory": str(root), "file": file, "arguments": ["clang++", "-ISource", "-c", file]}
            for target in dump.values() for file in target["files"]
        ]
        (root / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
        return contract, allowlist, targets

    def test_clean_tree_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            code = run_main(
                ["--root", str(root), "--contract", str(contract), "--allowlist", str(allowlist), "--targets", str(targets)]
            )
            self.assertEqual(code, 0)

    def test_empty_database_cannot_certify_includes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            (root / "compile_commands.json").write_text("[]")
            self.assertEqual(run_main([
                "--root", str(root), "--contract", str(contract),
                "--allowlist", str(allowlist), "--targets", str(targets),
            ]), 2)

    def test_violation_returns_one(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            write_tree(root, ["Source/Stray.cpp"])
            code = run_main(
                ["--root", str(root), "--contract", str(contract), "--allowlist", str(allowlist), "--targets", str(targets)]
            )
            self.assertEqual(code, 1)

    def test_link_flag_with_no_target_file_is_exit_two(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(
                root,
                ["Source/Core/Log.cpp", "Source/App/Options.h", "Source/App/Options.cpp", "Source/App/Shell.cpp"],
            )
            contract_dict = json.loads(json.dumps(CONTRACT))
            contract_dict["targets"]["App"]["frameworks"] = []
            contract = write_contract(root, contract_dict)
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
                    "targetfile": "",
                },
            }
            targets = root / "targets.json"
            targets.write_text(json.dumps(dump), encoding="utf-8")
            (root / "compile_commands.json").write_text("[]", encoding="utf-8")
            code = run_main(
                [
                    "--root", str(root),
                    "--contract", str(contract),
                    "--allowlist", str(allowlist),
                    "--targets", str(targets),
                    "--link",
                ]
            )
            self.assertEqual(code, 2)

    def test_link_flag_is_skipped_without_the_flag(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract, allowlist, targets = self.build_tree(root)
            contract_dict = json.loads(contract.read_text(encoding="utf-8"))
            contract_dict["targets"]["App"]["frameworks"] = []
            contract.write_text(json.dumps(contract_dict), encoding="utf-8")
            code = run_main(
                ["--root", str(root), "--contract", str(contract), "--allowlist", str(allowlist), "--targets", str(targets)]
            )
            self.assertEqual(code, 0)

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
    "thirdPartyPrefixes": {"SDL3/": "libsdl3"},
}

PACKAGE_STEM = ".xmake/packages/s/spdlog/v1.17.0/f028856e7c66484a8fbaa9d2364f2244/include"

INCLUDE_TREE = {
    "Source/Core/Log.h": "#include <spdlog/spdlog.h>\n",
    "RHI/Include/RHI/Format.h": '#include "Device.h"\n',
    "RHI/Include/RHI/Device.h": "",
    "RHI/Backends/Metal4/Source/Metal4Common.h": "",
    "RHI/Backends/Metal4/Source/Metal4Device.cpp": '#include "Metal4Common.h"\n#include <Metal/Metal.hpp>\n',
    "Source/Metal4Common.h": "",
    "Source/Engine/Allowed.h": '#include "RHI/Format.h"\n',
    "Source/Engine/Bad.h": '#include "RHI/Device.h"\n',
    "Source/Engine/Mid.h": '#include "Engine/Bad.h"\n',
    "Source/Engine/Uses.h": '#include "Core/Log.h"\n',
    "Source/Engine/Widget.h": "#include <imgui_impl_sdl3.h>\n",
    "Source/Engine/DoubleWidget.h": "#include <imgui_impl_sdl3.h>\n#include <imgui_impl_sdl3.h>\n",
    "Source/Engine/Absent.h": "#include <nowhere/absent.h>\n",
    "Source/Engine/Cyc1.h": '#include "Engine/Cyc2.h"\n',
    "Source/Engine/Cyc2.h": '#include "Engine/Cyc1.h"\n#include "RHI/Device.h"\n',
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
    def test_database_normalises_absolute_files_and_relative_directories(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            file = root / "Source/Core/Log.cpp"
            path = root / "commands.json"
            path.write_text(json.dumps([
                {"directory": "build", "file": str(file), "arguments": ["clang++", "-I../Source", "-c", "../Source/Core/Log.cpp"]},
            ]))
            entry = modules.load_compile_commands(path, root)["Source/Core/Log.cpp"]
            self.assertEqual(entry["directory"], str(root / "build"))
            self.assertEqual(entry["file"], str(file))
            self.assertEqual([p.resolve() for p in modules.include_dirs(entry)], [root / "Source"])

    def test_invalid_command_quoting_is_a_could_not_run_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "commands.json"
            path.write_text(json.dumps([{"file": "a.cpp", "command": 'clang++ "'}]))
            with self.assertRaises(modules.ModuleContractError):
                modules.load_compile_commands(path)

    def test_header_without_unit_sources_uses_its_targets_context(self) -> None:
        file = "RHI/Backends/Metal4/Source/Metal4Device.mm"
        entry = {"directory": "/repo", "arguments": ["clang++", "-IRHI/Include"]}
        result = modules.context_for(Path("RHI/Include/RHI/Device.h"), "rhi-public",
                                     {"RHI": {"files": [file]}}, {file: entry}, INCLUDE_CONTRACT)
        self.assertEqual(result, [Path("/repo/RHI/Include")])

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
            database = modules.load_compile_commands(path, Path("/repo"))
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

    def test_unresolvable_angle_include_with_a_third_party_prefix_is_charged_to_that_package(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            write_include_tree(root)
            contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
            dirs = modules.include_dirs(include_entry(root, "x.cpp"))
            resolved = modules.resolve_include(
                Path("Source/Engine/Absent.h"), "SDL3/SDL.h", False, dirs, root, contract
            )
            self.assertEqual((resolved.kind, resolved.name), ("third-party", "libsdl3"))

    def test_angle_include_resolved_outside_every_package_root_is_charged_by_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as external_directory:
            root = Path(directory).resolve()
            write_include_tree(root)
            contract = modules.load_contract(write_contract(root, INCLUDE_CONTRACT), root)
            external = Path(external_directory).resolve()
            sdl_header = external / "SDL3" / "SDL.h"
            sdl_header.parent.mkdir(parents=True, exist_ok=True)
            sdl_header.write_text("", encoding="utf-8")
            dirs = modules.include_dirs(include_entry(root, "x.cpp")) + [external]
            resolved = modules.resolve_include(
                Path("Source/Engine/Absent.h"), "SDL3/SDL.h", False, dirs, root, contract
            )
            self.assertEqual((resolved.kind, resolved.name), ("third-party", "libsdl3"))
            quoted = modules.resolve_include(
                Path("Source/Engine/Absent.h"), "SDL3/SDL.h", True, dirs, root, contract
            )
            self.assertEqual((quoted.kind, quoted.name), ("third-party", "libsdl3"))

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
            [Path(name) for name in names],
            {"RHI": {"files": ["RHI/Backends/Metal4/Source/Metal4Device.cpp"]}},
            include_db(root), contract, allowlist or [], errors, root
        )
        return errors

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        write_include_tree(self.root)

    def test_private_header_visibility_rejects_foreign_direct_and_transitive_reach(self) -> None:
        contract = json.loads(json.dumps(INCLUDE_CONTRACT))
        contract["units"]["core"]["privateHeaders"] = ["Source/Core/Log.h"]
        (self.root / "Source/Core/Public.h").write_text('#include "Core/Log.h"\n')
        (self.root / "Source/Engine/Uses.h").write_text('#include <Core/Public.h>\n')
        loaded = modules.load_contract(write_contract(self.root, contract), self.root)
        for name, chain in [
            ("Source/Engine/Uses.h", "Source/Engine/Uses.h -> Source/Core/Public.h -> Source/Core/Log.h"),
            ("Source/Core/Public.h", ""),
        ]:
            errors = []
            modules.check_includes([Path(name)], {}, include_db(self.root), loaded, [], errors, self.root)
            if chain:
                self.assertEqual(errors, [f"{name}: asset reaches private header Source/Core/Log.h owned by core via {chain}"])
            else:
                self.assertEqual(errors, [])
        (self.root / "Source/Engine/Uses.h").write_text('#include "Core/Log.h"\n')
        errors = []
        allowance = [{"kind": "include", "file": "Source/Engine/Uses.h", "reaches": "core",
                      "reason": "cannot override private visibility", "until": "test"}]
        modules.check_includes([Path("Source/Engine/Uses.h")], {}, include_db(self.root), loaded,
                               allowance, errors, self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("reaches private header", errors[0])

    def test_private_visibility_tracks_direct_and_public_wrapper_symlinks(self) -> None:
        contract = json.loads(json.dumps(INCLUDE_CONTRACT))
        contract["units"]["core"]["privateHeaders"] = ["Source/Core/Log.h"]
        (self.root / "Source/Core/Log.h").write_text("")
        (self.root / "Source/Core/Public.h").write_text('#include "Core/Log.h"\n')
        (self.root / "Source/Core/Alias.h").symlink_to("Log.h")
        (self.root / "Source/Engine/PrivateAlias.h").symlink_to("../Core/Log.h")
        (self.root / "Source/Engine/PublicAlias.h").symlink_to("../Core/Public.h")
        loaded = modules.load_contract(write_contract(self.root, contract), self.root)
        for spec in ("Core/Alias.h", "Engine/PrivateAlias.h", "Engine/PublicAlias.h"):
            with self.subTest(spec=spec):
                (self.root / "Source/Engine/Uses.h").write_text(f'#include "{spec}"\n')
                errors = []
                modules.check_includes([Path("Source/Engine/Uses.h")], {}, include_db(self.root),
                                       loaded, [], errors, self.root)
                self.assertEqual(len(errors), 1)
                self.assertIn("reaches private header Source/Core/Log.h owned by core", errors[0])
        errors = []
        modules.check_includes([Path("Source/Engine/PrivateAlias.h")], {}, include_db(self.root),
                               loaded, [], errors, self.root)
        self.assertEqual(errors, [
            "Source/Engine/PrivateAlias.h: asset aliases private header Source/Core/Log.h owned by core"
        ])
        (self.root / "Source/Core/Public.h").write_text('#include "Core/Alias.h"\n')
        errors = []
        modules.check_includes([Path("Source/Core/Public.h"), Path("Source/Core/Alias.h")], {},
                               include_db(self.root), loaded, [], errors, self.root)
        self.assertEqual(errors, [])

    def test_private_visibility_tracks_case_aliases_on_insensitive_filesystems(self) -> None:
        if not (self.root / "Source/Core/log.h").is_file():
            self.skipTest("case aliases require a case-insensitive filesystem")
        contract = json.loads(json.dumps(INCLUDE_CONTRACT))
        contract["units"]["core"]["privateHeaders"] = ["Source/Core/Log.h"]
        (self.root / "Source/Core/Public.h").write_text('#include "Core/Log.h"\n')
        loaded = modules.load_contract(write_contract(self.root, contract), self.root)
        for spec in ("Core/log.h", "core/Log.h", "core/public.h"):
            with self.subTest(spec=spec):
                (self.root / "Source/Engine/Uses.h").write_text(f'#include "{spec}"\n')
                errors = []
                modules.check_includes([Path("Source/Engine/Uses.h")], {}, include_db(self.root),
                                       loaded, [], errors, self.root)
                self.assertEqual(len(errors), 1)
                self.assertIn("reaches private header Source/Core/Log.h owned by core", errors[0])
        (self.root / "Source/Core/Public.h").write_text('#include "core/log.h"\n')
        errors = []
        modules.check_includes([Path("Source/Core/Public.h")], {}, include_db(self.root), loaded,
                               [], errors, self.root)
        self.assertEqual(errors, [])

    def test_reach_through_two_project_headers_reports_the_chain(self) -> None:
        errors = self.check(self.root, ["Source/Engine/Mid.h"])
        self.assertEqual(
            errors,
            [
                "Source/Engine/Mid.h: asset reaches rhi-public via "
                "Source/Engine/Mid.h -> Source/Engine/Bad.h -> RHI/Include/RHI/Device.h"
            ],
        )

    def test_header_allowance_admits_the_named_header_but_not_its_transitive_include(self) -> None:
        self.assertEqual(
            self.check(self.root, ["Source/Engine/Allowed.h"]),
            [
                "Source/Engine/Allowed.h: asset reaches rhi-public via "
                "Source/Engine/Allowed.h -> RHI/Include/RHI/Format.h -> RHI/Include/RHI/Device.h"
            ],
        )
        self.assertEqual(len(self.check(self.root, ["Source/Engine/Bad.h"])), 1)

    def test_header_cycle_terminates_and_reports_the_reaching_chain(self) -> None:
        errors = self.check(self.root, ["Source/Engine/Cyc1.h"])
        self.assertEqual(
            errors,
            [
                "Source/Engine/Cyc1.h: asset reaches rhi-public via "
                "Source/Engine/Cyc1.h -> Source/Engine/Cyc2.h -> RHI/Include/RHI/Device.h"
            ],
        )

    def test_third_party_reached_through_a_project_header_is_not_a_direct_include(self) -> None:
        self.assertEqual(self.check(self.root, ["Source/Engine/Uses.h"]), [])

    def test_third_party_named_by_the_file_itself_is_a_direct_include(self) -> None:
        self.assertEqual(
            self.check(self.root, ["Source/Engine/Widget.h"]),
            ["Source/Engine/Widget.h: asset includes imgui directly"],
        )

    def test_direct_third_party_violation_is_reported_once_per_file(self) -> None:
        self.assertEqual(
            self.check(self.root, ["Source/Engine/DoubleWidget.h"]),
            ["Source/Engine/DoubleWidget.h: asset includes imgui directly"],
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

    def test_angle_project_include_in_objective_cpp_is_followed_transitively(self) -> None:
        file = "Source/Engine/Probe.mm"
        (self.root / file).write_text("#include <Engine/Mid.h>\n")
        errors = self.check(self.root, [file])
        self.assertEqual(len(errors), 1)
        self.assertIn("Probe.mm -> Source/Engine/Mid.h -> Source/Engine/Bad.h -> RHI/Include/RHI/Device.h", errors[0])


class AppModelBoundaryTests(unittest.TestCase):
    def test_model_rejects_shell_and_ui_or_backend_headers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = json.loads(json.dumps(CONTRACT))
            contract["units"]["app-model"]["paths"] = ["Source/App/Model"]
            contract["units"]["app-model"]["targets"] = ["AppModel"]
            contract["targets"]["AppModel"] = {"deps": ["Core"]}
            contract["thirdPartyPrefixes"] = {"SDL3/": "libsdl3"}
            file = "Source/App/Model/Options.cpp"
            write_tree(root, [
                "Source/Core/Log.h", "Source/App/Shell.h", file,
                "ThirdParty/imgui/imgui.h", "ThirdParty/metal-cpp/Metal/Metal.hpp",
            ])
            (root / file).write_text(
                '#include "App/Shell.h"\n#include <imgui.h>\n'
                '#include <SDL3/SDL.h>\n#include <Metal/Metal.hpp>\n'
            )
            contract = modules.load_contract(write_contract(root, contract), root)
            database = {
                file: include_entry(root, file),
                "Source/App/Shell.cpp": include_entry(root, "Source/App/Shell.cpp"),
            }
            errors: list[str] = []
            modules.check_includes([Path(file)], {}, database, contract, [], errors, root)
            self.assertEqual(sorted(errors), sorted([
                f"{file}: app-model includes imgui directly",
                f"{file}: app-model includes libsdl3 directly",
                f"{file}: app-model includes metal-cpp directly",
                f"{file}: app-model reaches app-shell via {file} -> Source/App/Shell.h",
            ]))

    def test_model_rejects_builder_directly_and_through_an_allowed_unit(self) -> None:
        for indirect in (False, True):
            with self.subTest(indirect=indirect), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                contract = json.loads(json.dumps(CONTRACT))
                model = contract["units"]["app-model"]
                model["paths"] = ["Source/App/Model"]
                model["units"].append("render")
                model["forbidHeaders"] = ["Source/Render/RenderGraph.h"]
                contract["units"]["render"] = {
                    "paths": ["Source/Render"], "targets": ["Render"],
                    "units": [], "thirdParty": [],
                }
                file = "Source/App/Model/GraphModel.cpp"
                write_tree(root, ["Source/Core/Log.h", file, "Source/Render/RenderGraph.h",
                                  "Source/Render/Observer.h"])
                middle = "Render/Observer.h" if indirect else "Render/RenderGraph.h"
                (root / file).write_text(f'#include "{middle}"\n#include <{middle}>\n')
                (root / "Source/Render/Observer.h").write_text('#include "Render/RenderGraph.h"\n')
                contract = modules.load_contract(write_contract(root, contract), root)
                database = {name: include_entry(root, name) for name in
                            (file, "Source/Render/Render.cpp")}
                errors: list[str] = []
                modules.check_includes([Path(file)], {}, database, contract, [], errors, root)
                chain = ("Source/Render/Observer.h -> " if indirect else "") + "Source/Render/RenderGraph.h"
                self.assertEqual(errors, [
                    f"{file}: app-model reaches forbidden header Source/Render/RenderGraph.h "
                    f"via {file} -> {chain}",
                ])
                # Replacing the builder edge with the record leaf repairs the same include path.
                leaf = root / "Source/Render/CompiledFrameRecord.h"
                leaf.write_text("")
                includer = root / "Source/Render/Observer.h" if indirect else root / file
                includer.write_text('#include "Render/CompiledFrameRecord.h"\n')
                errors.clear()
                modules.check_includes([Path(file)], {}, database, contract, [], errors, root)
                self.assertEqual(errors, [])

    def test_model_rejects_transitive_ui_target_dependencies(self) -> None:
        contract = {
            "thirdPartyTargets": ["ImGui"],
            "targets": {
                "Core": {"deps": []},
                "Scene": {"deps": ["Core", "ImGui"]},
                "AppModel": {"deps": ["Core", "Scene"]},
            },
        }
        targets = {
            "Core": {"deps": []},
            "Scene": {"deps": ["Core", "ImGui"]},
            "AppModel": {"deps": ["Core", "Scene"]},
            "ImGui": {"deps": []},
        }
        errors: list[str] = []
        modules.check_target_closure(targets, contract, [], errors)
        self.assertEqual(errors, ["AppModel: depends on ImGui outside its allowed set"])


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


class TargetClosureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = {"targets": {
            "TextureBake": {"deps": ["Core", "Asset"]}, "Core": {"deps": []},
            "RHI": {"deps": ["Core"]}, "Render": {"deps": ["Core", "RHI"]},
            "Engine": {"deps": ["Core", "RHI", "Render"]},
        }}
        self.targets = {
            "Core": {"deps": []},
            "RHI": {"deps": ["Core"]},
            "Render": {"deps": ["Core", "RHI"]},
            "Engine": {"deps": ["Core", "RHI", "Render"]},
            "TextureBake": {"deps": ["Core", "Engine"]},
        }

    def test_closure_is_transitive(self) -> None:
        self.assertEqual(
            modules.dependency_closure("TextureBake", self.targets), {"Core", "Engine", "RHI", "Render"}
        )

    def test_closure_dep_outside_the_allowed_set_is_an_error(self) -> None:
        errors: list[str] = []
        modules.check_target_closure(self.targets, self.contract, [], errors)
        self.assertEqual(
            sorted(errors),
            [
                "TextureBake: depends on Engine outside its allowed set",
                "TextureBake: depends on RHI outside its allowed set",
                "TextureBake: depends on Render outside its allowed set",
            ],
        )

    def test_allowed_name_that_is_not_a_target_is_tolerated(self) -> None:
        targets = {"Core": {"deps": []}, "TextureBake": {"deps": ["Core"]}}
        errors: list[str] = []
        modules.check_target_closure(targets, self.contract, [], errors)
        self.assertEqual(errors, [])

    def test_new_target_requires_a_contract_even_without_sources(self) -> None:
        targets = {"NewTool": {"deps": []}, "ImGui": {"deps": []}}
        errors = []
        modules.check_target_closure(targets, {"targets": {}, "thirdPartyTargets": ["ImGui"]}, [], errors)
        self.assertEqual(errors, ["NewTool: no target contract; declare its allowed dependencies"])

    def test_target_dep_allowlist_entry_suppresses_one_dependency(self) -> None:
        allowlist = [
            {"kind": "target-dep", "target": "TextureBake", "dep": "Render", "reason": "r", "until": "R1.2"}
        ]
        errors: list[str] = []
        modules.check_target_closure(self.targets, self.contract, allowlist, errors)
        self.assertEqual(
            sorted(errors),
            [
                "TextureBake: depends on Engine outside its allowed set",
                "TextureBake: depends on RHI outside its allowed set",
            ],
        )
        self.assertTrue(allowlist[0]["used"])


class FakeCompleted:
    """A canned `subprocess.run` result, injected so tests never shell out."""

    def __init__(self, returncode: int = 0, stdout: str = "", stderr: str = "") -> None:
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


class LinkedFrameworksAndSymbolsTests(unittest.TestCase):
    def test_linked_frameworks_reads_otool_output(self) -> None:
        stdout = (
            "build/TextureBake:\n"
            "\t/usr/lib/libz.1.dylib (compatibility version 1.0.0, current version 1.2.12)\n"
            "\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal (compatibility version 1.0.0, current version 1.0.0)\n"
            "\t/System/Library/Frameworks/MetalFX.framework/Versions/A/MetalFX (compatibility version 1.0.0, current version 1.0.0)\n"
        )

        def run(args, **kwargs):
            self.assertEqual(args[0], "otool")
            return FakeCompleted(stdout=stdout)

        self.assertEqual(modules.linked_frameworks(Path("build/TextureBake"), run=run), ["Metal", "MetalFX"])

    def test_linked_frameworks_failure_is_a_could_not_run_error(self) -> None:
        def run(args, **kwargs):
            return FakeCompleted(returncode=1, stderr="no such file")

        with self.assertRaises(modules.ModuleContractError):
            modules.linked_frameworks(Path("build/Missing"), run=run)

    def test_undefined_symbols_demangles_through_cxxfilt(self) -> None:
        calls: list[list[str]] = []

        def run(args, **kwargs):
            calls.append(args)
            if args[0] == "nm":
                return FakeCompleted(stdout="libEngine.a(Foo.cpp.o):\n__ZN3lmx3rhi6DeviceD1Ev\n__Znwm\n")
            self.assertEqual(args[0], "c++filt")
            self.assertEqual(kwargs.get("input"), "__ZN3lmx3rhi6DeviceD1Ev\n__Znwm")
            return FakeCompleted(stdout="lmx::rhi::Device::~Device()\n__Znwm\n")

        symbols = modules.undefined_symbols(Path("libEngine.a"), run=run)
        self.assertEqual(symbols, ["lmx::rhi::Device::~Device()", "__Znwm"])
        self.assertEqual([call[0] for call in calls], ["nm", "c++filt"])

    def test_undefined_symbols_with_no_symbols_skips_cxxfilt(self) -> None:
        def run(args, **kwargs):
            self.assertEqual(args[0], "nm")
            return FakeCompleted(stdout="libEngine.a(Foo.cpp.o):\n")

        self.assertEqual(modules.undefined_symbols(Path("libEngine.a"), run=run), [])


class CheckLinkTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = {
            "targets": {
                "TextureBake": {"deps": ["Core", "Asset"], "frameworks": []},
                "Asset": {"deps": ["Core"], "forbidUndefined": "lmx::rhi::"},
            }
        }

    def make_targets(self, root: Path) -> dict[str, dict]:
        texture_bake = root / "TextureBake"
        texture_bake.write_text("", encoding="utf-8")
        asset = root / "libAsset.a"
        asset.write_text("", encoding="utf-8")
        return {
            "TextureBake": {"deps": [], "packages": [], "frameworks": [], "targetfile": str(texture_bake)},
            "Asset": {"deps": [], "packages": [], "frameworks": [], "targetfile": str(asset)},
        }

    def test_framework_outside_the_allowed_set_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            targets = self.make_targets(Path(directory))

            def run(args, **kwargs):
                if args[0] == "otool":
                    return FakeCompleted(
                        stdout="TextureBake:\n"
                        "\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal (x)\n"
                    )
                return FakeCompleted(stdout="")

            errors: list[str] = []
            modules.check_link(targets, self.contract, [], errors, run=run)
            self.assertIn("TextureBake: links Metal outside its allowed set", errors)

    def test_framework_allowlist_entry_suppresses_one_framework(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            targets = self.make_targets(Path(directory))
            allowlist = [
                {"kind": "framework", "target": "TextureBake", "framework": "Metal", "reason": "r", "until": "R1.2"}
            ]

            def run(args, **kwargs):
                if args[0] == "otool":
                    return FakeCompleted(
                        stdout="TextureBake:\n"
                        "\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal (x)\n"
                    )
                return FakeCompleted(stdout="")

            errors: list[str] = []
            modules.check_link(targets, self.contract, allowlist, errors, run=run)
            self.assertEqual(errors, [])
            self.assertTrue(allowlist[0]["used"])

    def test_forbidden_undefined_symbol_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            targets = self.make_targets(Path(directory))

            def run(args, **kwargs):
                if args[0] == "otool":
                    return FakeCompleted(stdout="")
                if args[0] == "nm":
                    return FakeCompleted(stdout="libAsset.a(Foo.cpp.o):\n__ZN3lmx3rhi6DeviceD1Ev\n")
                self.assertEqual(args[0], "c++filt")
                return FakeCompleted(stdout="lmx::rhi::Device::~Device()\n")

            errors: list[str] = []
            modules.check_link(targets, self.contract, [], errors, run=run)
            self.assertEqual(
                errors, ["Asset: undefined symbol lmx::rhi::Device::~Device() references lmx::rhi::"]
            )

    def test_missing_target_file_is_a_could_not_run_error(self) -> None:
        targets = {"TextureBake": {"deps": [], "packages": [], "frameworks": [], "targetfile": ""}}
        contract = {"targets": {"TextureBake": {"deps": [], "frameworks": []}}}
        with self.assertRaisesRegex(modules.ModuleContractError, "xmake build"):
            modules.check_link(targets, contract, [], [], run=lambda *a, **k: FakeCompleted())

    def test_target_with_no_link_contract_is_skipped(self) -> None:
        contract = {"targets": {"Core": {"deps": []}}}
        targets = {"Core": {"deps": [], "packages": [], "frameworks": [], "targetfile": ""}}
        errors: list[str] = []
        modules.check_link(targets, contract, [], errors, run=lambda *a, **k: FakeCompleted())
        self.assertEqual(errors, [])


class AllowlistLinkOnlyTests(unittest.TestCase):
    def test_unused_framework_entry_is_tolerated_when_link_is_not_requested(self) -> None:
        allowlist = [
            {"kind": "framework", "target": "TextureBake", "framework": "Metal", "reason": "r", "until": "R1.2"}
        ]
        errors: list[str] = []
        modules.check_allowlist_use(Path("Tools/module_allowlist.json"), allowlist, errors, link=False)
        self.assertEqual(errors, [])

    def test_unused_framework_entry_is_an_error_when_link_is_requested(self) -> None:
        allowlist = [
            {"kind": "framework", "target": "TextureBake", "framework": "Metal", "reason": "r", "until": "R1.2"}
        ]
        errors: list[str] = []
        modules.check_allowlist_use(Path("Tools/module_allowlist.json"), allowlist, errors, link=True)
        self.assertEqual(errors, ["Tools/module_allowlist.json: unused entry framework TextureBake (R1.2)"])

    def test_unused_target_dep_entry_is_an_error_regardless_of_link(self) -> None:
        allowlist = [{"kind": "target-dep", "target": "TextureBake", "dep": "Render", "reason": "r", "until": "R1.2"}]
        errors: list[str] = []
        modules.check_allowlist_use(Path("Tools/module_allowlist.json"), allowlist, errors, link=False)
        self.assertEqual(errors, ["Tools/module_allowlist.json: unused entry target-dep TextureBake (R1.2)"])

    def test_unused_header_entry_is_tolerated_because_another_checker_owns_it(self) -> None:
        allowlist = [
            {"kind": "header", "file": "Source/App/Model/GraphLayout.h", "reason": "r", "until": "R1.2"},
            {"kind": "target-dep", "target": "TextureBake", "dep": "Render", "reason": "r", "until": "R1.2"},
        ]
        errors: list[str] = []
        modules.check_allowlist_use(Path("Tools/module_allowlist.json"), allowlist, errors, link=False)
        self.assertEqual(errors, ["Tools/module_allowlist.json: unused entry target-dep TextureBake (R1.2)"])


class BudgetTests(unittest.TestCase):
    def test_file_over_its_root_budget_is_a_review_candidate(self) -> None:
        contract = {"budgets": {"production": 2, "tests": 5}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Big.cpp", "Tests/Big.cpp"])
            (root / "Source/Big.cpp").write_text("a\nb\nc\n", encoding="utf-8")
            (root / "Tests/Big.cpp").write_text("a\nb\nc\n", encoding="utf-8")
            lines = modules.report_budgets([Path("Source/Big.cpp"), Path("Tests/Big.cpp")], contract, root)
            self.assertEqual(lines, ["review candidate: Source/Big.cpp (3 > 2 lines)"])

    def test_component_test_directory_uses_the_tests_budget(self) -> None:
        contract = {"budgets": {"production": 2, "tests": 5}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["RHI/Tests/Big.cpp", "RHI/Source/Big.cpp"])
            (root / "RHI/Tests/Big.cpp").write_text("a\nb\nc\n", encoding="utf-8")
            (root / "RHI/Source/Big.cpp").write_text("a\nb\nc\n", encoding="utf-8")
            lines = modules.report_budgets(
                [Path("RHI/Tests/Big.cpp"), Path("RHI/Source/Big.cpp")], contract, root
            )
            self.assertEqual(lines, ["review candidate: RHI/Source/Big.cpp (3 > 2 lines)"])

    def test_file_within_budget_is_not_reported(self) -> None:
        contract = {"budgets": {"production": 1000}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Small.cpp"])
            (root / "Source/Small.cpp").write_text("a\n", encoding="utf-8")
            self.assertEqual(modules.report_budgets([Path("Source/Small.cpp")], contract, root), [])


if __name__ == "__main__":
    unittest.main()
