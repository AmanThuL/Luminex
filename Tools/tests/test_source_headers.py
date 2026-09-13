from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Tools import check_source_headers as headers


CONTRACT = {
    "schemaVersion": 1,
    "roots": ["Source"],
    "budgets": {"production": 1000, "tests": 1500},
    "units": {
        "core": {"paths": ["Source/Core"], "targets": ["Core"], "units": [], "thirdParty": []},
        "app-shell": {
            "paths": ["Source/App"],
            "targets": ["App"],
            "units": ["core"],
            "thirdParty": ["imgui"],
        },
        "app-model": {
            "paths": ["Source/App/Model"],
            "targets": ["AppModel"],
            "units": ["core"],
            "thirdParty": [],
        },
    },
    "targets": {
        "Core": {"deps": []},
        "AppModel": {"deps": ["Core"]},
        "App": {"deps": ["Core", "AppModel"]},
    },
}

TARGETS = {
    "App": {
        "kind": "binary",
        "files": ["Source/App/Panels/InspectorPanel.cpp"],
        "deps": ["Core", "AppModel"],
        "packages": [],
        "frameworks": [],
        "targetfile": "",
    },
    "AppModel": {
        "kind": "static",
        "files": ["Source/App/Model/GraphLayout.cpp"],
        "deps": ["Core"],
        "packages": [],
        "frameworks": [],
        "targetfile": "",
    },
    "Core": {"kind": "static", "files": [], "deps": [], "packages": [], "frameworks": [], "targetfile": ""},
}

COMPILE_DB = {
    "Source/App/Model/GraphLayout.cpp": {
        "directory": "/repo",
        "arguments": [
            "clang++",
            "-c",
            "-target",
            "arm64-apple-macos26.5",
            "-isysroot",
            "/SDK",
            "-std=c++23",
            "-Wall",
            "-IRHI/Include",
            "-ISource",
            "-I",
            "/opt/glm/include",
            "-isystem",
            "/opt/glm/system",
            "-o",
            "build/GraphLayout.cpp.o",
            "Source/App/Model/GraphLayout.cpp",
        ],
    },
    "Source/App/Panels/InspectorPanel.cpp": {
        "directory": "/repo",
        "arguments": [
            "clang++",
            "-c",
            "-target",
            "arm64-apple-macos26.5",
            "-isysroot",
            "/SDK",
            "-std=c++23",
            "-IRHI/Include",
            "-ISource",
            "-isystem",
            "/opt/imgui/include",
            "-o",
            "build/InspectorPanel.cpp.o",
            "Source/App/Panels/InspectorPanel.cpp",
        ],
    },
}


def write_tree(root: Path, files: list[str]) -> None:
    for name in files:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("", encoding="utf-8")


def write_json(root: Path, name: str, document: dict | list) -> Path:
    path = root / name
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


def run_main(argv: list[str]) -> int:
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return headers.main(argv)


class SyntaxCommandTests(unittest.TestCase):
    def test_absolute_source_field_removes_the_relative_command_operand(self) -> None:
        entry = {
            "directory": "/repo/build", "file": "/repo/Source/Core/Log.cpp",
            "arguments": ["clang++", "-I../Source", "-c", "../Source/Core/Log.cpp", "-o", "Log.o"],
        }
        self.assertEqual(headers.syntax_command(entry), ["clang++", "-I../Source", "-fsyntax-only", "-x", "c++", "-"])

    def test_drops_compile_only_output_and_source_but_keeps_include_flags(self) -> None:
        entry = dict(COMPILE_DB["Source/App/Model/GraphLayout.cpp"])
        entry["file"] = "Source/App/Model/GraphLayout.cpp"
        command = headers.syntax_command(entry)

        self.assertNotIn("-c", command)
        self.assertNotIn("-o", command)
        self.assertNotIn("build/GraphLayout.cpp.o", command)
        self.assertNotIn("Source/App/Model/GraphLayout.cpp", command)
        self.assertIn("-std=c++23", command)
        self.assertIn("-isysroot", command)
        self.assertIn("-target", command)
        self.assertIn("-IRHI/Include", command)
        self.assertIn("-ISource", command)
        self.assertIn("-isystem", command)
        self.assertEqual(command[-4:], ["-fsyntax-only", "-x", "c++", "-"])


class CommandForTests(unittest.TestCase):
    def test_panel_header_resolves_to_the_app_targets_command(self) -> None:
        header = Path("Source/App/Panels/InspectorPanel.h")
        command = headers.command_for(header, CONTRACT, TARGETS, COMPILE_DB)
        self.assertIn("-isystem", command)
        self.assertIn("-ISource", command)

    def test_model_header_resolves_to_its_own_target_command(self) -> None:
        panel_command = headers.command_for(Path("Source/App/Panels/InspectorPanel.h"), CONTRACT, TARGETS, COMPILE_DB)
        model_command = headers.command_for(Path("Source/App/Model/GraphLayout.h"), CONTRACT, TARGETS, COMPILE_DB)
        self.assertNotEqual(panel_command, model_command)
        self.assertIn("/opt/imgui/include", panel_command)
        self.assertNotIn("/opt/imgui/include", model_command)
        self.assertIn("/opt/glm/include", model_command)

    def test_unowned_header_is_an_error(self) -> None:
        with self.assertRaises(headers.check_module_deps.ModuleContractError):
            headers.command_for(Path("Source/Other/Thing.h"), CONTRACT, TARGETS, COMPILE_DB)

    def test_choice_is_the_smallest_matching_path_regardless_of_database_order(self) -> None:
        header = Path("Source/App/Panels/InspectorPanel.h")
        forward = dict(COMPILE_DB)
        reversed_db = dict(reversed(list(COMPILE_DB.items())))

        self.assertEqual(list(forward), list(reversed(list(reversed_db))))
        self.assertEqual(
            headers.command_for(header, CONTRACT, TARGETS, forward),
            headers.command_for(header, CONTRACT, TARGETS, reversed_db),
        )
        # A model source cannot supply the shell target's compilation context.
        self.assertEqual(
            headers.command_for(header, CONTRACT, TARGETS, forward),
            headers.syntax_command(
                {**COMPILE_DB["Source/App/Panels/InspectorPanel.cpp"], "file": "Source/App/Panels/InspectorPanel.cpp"}
            ),
        )


class CheckHeaderTests(unittest.TestCase):
    def test_compile_uses_the_database_working_directory(self) -> None:
        with patch.object(headers.subprocess, "run") as run:
            run.return_value.returncode = 0
            headers.check_header(Path("Source/Core/Log.h"), ["clang++"], Path("/repo"),
                                 run=run, directory=Path("/repo/build"))
        self.assertEqual(run.call_args.kwargs["cwd"], Path("/repo/build"))

    def test_include_line_is_relative_to_source(self) -> None:
        calls: list[dict] = []

        def fake_run(command, **kwargs):
            calls.append({"command": command, **kwargs})

            class Result:
                returncode = 0
                stderr = ""

            return Result()

        header = Path("Source/App/Model/GraphLayout.h")
        diagnostic = headers.check_header(header, ["clang++"], Path("/repo"), run=fake_run)

        self.assertIsNone(diagnostic)
        self.assertEqual(calls[0]["input"], '#include "App/Model/GraphLayout.h"\n')
        self.assertEqual(calls[0]["cwd"], Path("/repo"))

    def test_nonzero_exit_reports_the_diagnostic(self) -> None:
        def fake_run(command, **kwargs):
            class Result:
                returncode = 1
                stderr = "boom\n"

            return Result()

        header = Path("Source/App/Model/GraphLayout.h")
        diagnostic = headers.check_header(header, ["clang++"], Path("/repo"), run=fake_run)
        self.assertEqual(diagnostic, "boom")


class HeaderAllowlistTests(unittest.TestCase):
    def test_allowlisted_header_is_suppressed_and_marked_used(self) -> None:
        entry = {"kind": "header", "file": "Source/App/Model/GraphLayout.h", "reason": "r", "until": "R1.2"}
        allowlist = [entry]
        self.assertTrue(headers.header_allowed(Path("Source/App/Model/GraphLayout.h"), allowlist))
        self.assertTrue(entry["used"])

    def test_unused_header_entry_is_reported(self) -> None:
        entry = {"kind": "header", "file": "Source/App/Model/GraphLayout.h", "reason": "r", "until": "R1.2"}
        errors: list[str] = []
        headers.check_header_allowlist_use(Path("Tools/module_allowlist.json"), [entry], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("Source/App/Model/GraphLayout.h", errors[0])

    def test_used_header_entry_is_not_reported(self) -> None:
        entry = {"kind": "header", "file": "Source/App/Model/GraphLayout.h", "reason": "r", "until": "R1.2", "used": True}
        errors: list[str] = []
        headers.check_header_allowlist_use(Path("Tools/module_allowlist.json"), [entry], errors)
        self.assertEqual(errors, [])


class SourceHeadersTests(unittest.TestCase):
    def test_lists_only_headers_under_source_sorted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(
                root,
                [
                    "Source/App/Model/GraphLayout.h",
                    "Source/App/Model/GraphLayout.cpp",
                    "Source/Core/Log.h",
                    "RHI/Include/RHI/Device.h",
                ],
            )
            result = headers.source_headers(root)
            self.assertEqual(
                result,
                [Path("Source/App/Model/GraphLayout.h"), Path("Source/Core/Log.h")],
            )


class MainTests(unittest.TestCase):
    def run_fixture(self, diagnostic: str | Exception | None, allowed: bool) -> tuple[int, int]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h", "Source/Core/Log.cpp"])
            contract = write_json(root, "contract.json", {
                "schemaVersion": 1, "roots": ["Source"], "units": {
                    "core": {"paths": ["Source/Core"], "targets": ["Core"]},
                },
            })
            entries = [{"kind": "header", "file": "Source/Core/Log.h", "reason": "r", "until": "R1.2"}] if allowed else []
            allowlist = write_json(root, "allowlist.json", {"schemaVersion": 1, "entries": entries})
            targets = write_json(root, "targets.json", {"Core": {"files": ["Source/Core/Log.cpp"]}})
            write_json(root, "compile_commands.json", [{
                "directory": str(root), "file": "Source/Core/Log.cpp",
                "arguments": ["clang++", "-ISource", "-c", "Source/Core/Log.cpp"],
            }])
            with patch.object(headers, "check_header") as check:
                if isinstance(diagnostic, Exception):
                    check.side_effect = diagnostic
                else:
                    check.return_value = diagnostic
                code = run_main(["--root", str(root), "--contract", str(contract),
                                 "--allowlist", str(allowlist), "--targets", str(targets)])
                return code, check.call_count

    def test_clean_header_passes(self) -> None:
        self.assertEqual(self.run_fixture(None, False), (0, 1))

    def test_failed_header_returns_one(self) -> None:
        self.assertEqual(self.run_fixture("unknown type", False), (1, 1))

    def test_allowance_suppresses_only_a_failed_compilation(self) -> None:
        self.assertEqual(self.run_fixture("unknown type", True), (0, 1))

    def test_allowance_for_a_repaired_header_is_stale(self) -> None:
        self.assertEqual(self.run_fixture(None, True), (1, 1))

    def test_missing_compiler_returns_two_even_with_an_allowance(self) -> None:
        self.assertEqual(self.run_fixture(FileNotFoundError("clang++"), True), (2, 1))

    def test_missing_compile_database_is_exit_2(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_tree(root, ["Source/Core/Log.h"])
            contract_path = write_json(
                root,
                "contract.json",
                {
                    "schemaVersion": 1,
                    "roots": ["Source"],
                    "budgets": {},
                    "units": {"core": {"paths": ["Source/Core"], "targets": ["Core"], "units": [], "thirdParty": []}},
                },
            )
            allowlist_path = write_json(root, "allowlist.json", {"schemaVersion": 1, "entries": []})
            missing_compile_db = root / "compile_commands.json"

            with contextlib.redirect_stderr(io.StringIO()) as captured:
                code = headers.main(
                    [
                        "--root",
                        str(root),
                        "--contract",
                        str(contract_path),
                        "--allowlist",
                        str(allowlist_path),
                        "--compile-commands",
                        str(missing_compile_db),
                    ]
                )

            self.assertEqual(code, 2)
            self.assertIn("run xmake project -k compile_commands first", captured.getvalue())


if __name__ == "__main__":
    unittest.main()
