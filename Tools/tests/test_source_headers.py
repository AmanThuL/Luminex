from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

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
            "paths": ["Source/App/GraphLayout.h", "Source/App/GraphLayout.cpp"],
            "targets": ["App"],
            "units": ["core"],
            "thirdParty": [],
        },
    },
    "targets": {"Core": {"deps": []}, "App": {"deps": ["Core"]}},
}

TARGETS = {
    "App": {
        "kind": "binary",
        "files": [
            "Source/App/GraphLayout.cpp",
            "Source/App/Panels/InspectorPanel.cpp",
        ],
        "deps": ["Core"],
        "packages": [],
        "frameworks": [],
        "targetfile": "",
    },
    "Core": {"kind": "static", "files": [], "deps": [], "packages": [], "frameworks": [], "targetfile": ""},
}

COMPILE_DB = {
    "Source/App/GraphLayout.cpp": {
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
            "/opt/imgui/include",
            "-o",
            "build/GraphLayout.cpp.o",
            "Source/App/GraphLayout.cpp",
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
    def test_drops_compile_only_output_and_source_but_keeps_include_flags(self) -> None:
        entry = dict(COMPILE_DB["Source/App/GraphLayout.cpp"])
        entry["file"] = "Source/App/GraphLayout.cpp"
        command = headers.syntax_command(entry)

        self.assertNotIn("-c", command)
        self.assertNotIn("-o", command)
        self.assertNotIn("build/GraphLayout.cpp.o", command)
        self.assertNotIn("Source/App/GraphLayout.cpp", command)
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

    def test_model_header_resolves_to_the_same_app_command(self) -> None:
        panel_command = headers.command_for(Path("Source/App/Panels/InspectorPanel.h"), CONTRACT, TARGETS, COMPILE_DB)
        model_command = headers.command_for(Path("Source/App/GraphLayout.h"), CONTRACT, TARGETS, COMPILE_DB)
        self.assertEqual(panel_command, model_command)

    def test_unowned_header_is_an_error(self) -> None:
        with self.assertRaises(headers.check_module_deps.ModuleContractError):
            headers.command_for(Path("Source/Other/Thing.h"), CONTRACT, TARGETS, COMPILE_DB)


class CheckHeaderTests(unittest.TestCase):
    def test_include_line_is_relative_to_source(self) -> None:
        calls: list[dict] = []

        def fake_run(command, **kwargs):
            calls.append({"command": command, **kwargs})

            class Result:
                returncode = 0
                stderr = ""

            return Result()

        header = Path("Source/App/GraphLayout.h")
        diagnostic = headers.check_header(header, ["clang++"], Path("/repo"), run=fake_run)

        self.assertIsNone(diagnostic)
        self.assertEqual(calls[0]["input"], '#include "App/GraphLayout.h"\n')
        self.assertEqual(calls[0]["cwd"], Path("/repo"))

    def test_nonzero_exit_reports_the_diagnostic(self) -> None:
        def fake_run(command, **kwargs):
            class Result:
                returncode = 1
                stderr = "boom\n"

            return Result()

        header = Path("Source/App/GraphLayout.h")
        diagnostic = headers.check_header(header, ["clang++"], Path("/repo"), run=fake_run)
        self.assertEqual(diagnostic, "boom")


class HeaderAllowlistTests(unittest.TestCase):
    def test_allowlisted_header_is_suppressed_and_marked_used(self) -> None:
        entry = {"kind": "header", "file": "Source/App/GraphLayout.h", "reason": "r", "until": "R1.2"}
        allowlist = [entry]
        self.assertTrue(headers.header_allowed(Path("Source/App/GraphLayout.h"), allowlist))
        self.assertTrue(entry["used"])

    def test_unused_header_entry_is_reported(self) -> None:
        entry = {"kind": "header", "file": "Source/App/GraphLayout.h", "reason": "r", "until": "R1.2"}
        errors: list[str] = []
        headers.check_header_allowlist_use(Path("Tools/module_allowlist.json"), [entry], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("Source/App/GraphLayout.h", errors[0])

    def test_used_header_entry_is_not_reported(self) -> None:
        entry = {"kind": "header", "file": "Source/App/GraphLayout.h", "reason": "r", "until": "R1.2", "used": True}
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
                    "Source/App/GraphLayout.h",
                    "Source/App/GraphLayout.cpp",
                    "Source/Core/Log.h",
                    "RHI/Include/RHI/Device.h",
                ],
            )
            result = headers.source_headers(root)
            self.assertEqual(
                result,
                [Path("Source/App/GraphLayout.h"), Path("Source/Core/Log.h")],
            )


class MainTests(unittest.TestCase):
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
