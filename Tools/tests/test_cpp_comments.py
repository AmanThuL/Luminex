from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Tools import check_cpp_comments as comments


def source(name: str, brief: str = "Declares an example API.") -> str:
    return "\n".join(
        [
            comments.FILE_RULER,
            f"/// @file {name}",
            f"/// @brief {brief}",
            comments.FILE_RULER,
            "",
        ]
    )


class CppCommentTests(unittest.TestCase):
    def test_valid_file_header_envelope(self) -> None:
        text = source("Example.h") + "#pragma once\n"
        self.assertEqual(comments.check_file_header(Path("Source/Example.h"), text), [])
        self.assertEqual(len(comments.FILE_RULER), 120)

    def test_file_header_starts_on_first_line_and_matches_basename(self) -> None:
        text = "\n" + source("Other.h")
        errors = comments.check_file_header(Path("Benchmarks/Example.h"), text)
        self.assertTrue(any(":1:" in error for error in errors))
        self.assertTrue(any("basename" in error or "expected" in error for error in errors))

    def test_file_header_requires_nonempty_brief_and_closing_ruler(self) -> None:
        text = "\n".join(
            [comments.FILE_RULER, "/// @file Example.cpp", "/// @brief ", "//----"]
        )
        errors = comments.check_file_header(Path("Source/Example.cpp"), text)
        self.assertTrue(any("non-empty" in error for error in errors))
        self.assertTrue(any("closing" in error for error in errors))

    def test_owned_roots_include_source_and_benchmarks_but_not_tests(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in (
                "Source/App.cpp",
                "Benchmarks/Bench.cpp",
                "Tests/Test.cpp",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(
                [path.relative_to(root).as_posix() for path in comments.project_cpp_files(root)],
                ["Benchmarks/Bench.cpp", "Source/App.cpp"],
            )

    def test_public_header_roots_only_walk_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in (
                "Source/App/Public.h",
                "Benchmarks/Public.h",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(
                [path.relative_to(root).as_posix() for path in comments.public_header_files(root)],
                ["Source/App/Public.h"],
            )

    def test_private_source_headers_keep_envelopes_but_are_not_exported_api(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("Source/Core/Public.h", "Source/Core/Private.h"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            contract_path = root / "Tools/module_contract.json"
            contract_path.parent.mkdir()
            contract = {"schemaVersion": 1, "roots": ["Source"], "units": {
                "core": {"paths": ["Source/Core"], "privateHeaders": ["Source/Core/Private.h"]}
            }}
            contract_path.write_text(json.dumps(contract))
            self.assertEqual([p.name for p in comments.public_header_files(root)], ["Public.h"])
            self.assertEqual([p.name for p in comments.project_cpp_files(root)], ["Private.h", "Public.h"])
            (root / "Source/Core/Alias.h").symlink_to("Private.h")
            self.assertEqual([p.name for p in comments.public_header_files(root)], ["Public.h"])
            contract["units"]["core"]["privateHeaders"] = ["Source/Core/Missing.h"]
            contract_path.write_text(json.dumps(contract))
            with self.assertRaises(RuntimeError):
                comments.public_header_files(root)

    def test_ast_gate_observes_access_and_documentation(self) -> None:
        text = source("Example.h") + "class Example {};\n"
        ast = [{
            "kind": "NamespaceDecl",
            "loc": {"file": "Source/Example.h", "line": 5, "offset": 150},
            "inner": [{
                "kind": "CXXRecordDecl", "name": "Example", "tagUsed": "class",
                "completeDefinition": True, "loc": {"line": 5, "offset": 151},
                "inner": [
                    {"kind": "AccessSpecDecl", "access": "public"},
                    {"kind": "CXXMethodDecl", "name": "missing", "loc": {"line": 7}},
                    {"kind": "CXXMethodDecl", "name": "documented", "loc": {"line": 8},
                     "inner": [{"kind": "FullComment"}]},
                    {"kind": "CXXConstructorDecl", "name": "Example", "loc": {"line": 9},
                     "explicitlyDefaulted": "default"},
                    {"kind": "AccessSpecDecl", "access": "private"},
                    {"kind": "FieldDecl", "name": "implementation", "loc": {"line": 11}},
                ],
            }],
        }]
        findings = comments.public_api_findings_from_ast(Path("Source/Example.h"), text, ast)
        self.assertEqual(len(findings), 2)
        self.assertTrue(any(":5:" in finding for finding in findings))
        self.assertTrue(any(":7:" in finding for finding in findings))

    def test_ast_gate_ignores_forward_declarations_and_checks_enum_values(self) -> None:
        ast = [{
            "kind": "NamespaceDecl", "loc": {"file": "Source/Example.h", "line": 5},
            "inner": [
                {"kind": "CXXRecordDecl", "name": "Forward", "loc": {"line": 6}},
                {"kind": "EnumDecl", "name": "Mode", "loc": {"line": 8}, "inner": [
                    {"kind": "FullComment"},
                    {"kind": "EnumConstantDecl", "name": "Fast", "loc": {"line": 9},
                     "inner": [{"kind": "FullComment"}]},
                    {"kind": "EnumConstantDecl", "name": "Safe", "loc": {"line": 10}},
                ]},
            ],
        }]
        findings = comments.public_api_findings_from_ast(
            Path("Source/Example.h"), source("Example.h"), ast
        )
        self.assertEqual(
            findings,
            ["Source/Example.h:10: public API declaration has no Doxygen comment"],
        )


class EntryForHeaderTests(unittest.TestCase):
    """Headers borrow actual commands from their declared contract owner."""

    contract = {"units": {
        "core": {"paths": ["Source/Core"]},
        "engine": {"paths": ["Source/Engine"]},
        "asset": {"paths": ["Source/Engine/Asset"]},
        "app": {"paths": ["Source/App"]},
        "model": {"paths": ["Source/App/Model"]},
    }}

    def entry(self, header: str, entries: list[dict], root: Path = Path("/repo")) -> dict:
        return comments._entry_for_header(root / header, entries, root, self.contract)

    def test_a_component_unit_with_the_same_module_name_and_stem_is_not_chosen(self) -> None:
        root = Path("/repo")
        entries = [
            {"file": "RojoRHI/Source/Core/SlotAllocator.cpp"},
            {"file": "Source/Core/Containers/Handle.cpp"},
        ]
        entry = self.entry("Source/Core/Containers/SlotAllocator.h", entries, root)
        self.assertEqual(entry["file"], "Source/Core/Containers/Handle.cpp")

    def test_an_absolute_unit_path_inside_the_module_matches(self) -> None:
        root = Path("/repo")
        entries = [{"file": "/repo/RojoRHI/Source/Core/Probe.cpp"}, {"file": "/repo/Source/Core/Probe.cpp"}]
        entry = self.entry("Source/Core/Probe.h", entries, root)
        self.assertEqual(entry["file"], "/repo/Source/Core/Probe.cpp")

    def test_nested_asset_command_does_not_supply_engine_header_context(self) -> None:
        asset = {"file": "Source/Engine/Asset/RepositoryAsset.cpp", "arguments": ["clang++", "-DASSET"]}
        engine = {"file": "Source/Engine/Gpu/GpuBuffer.cpp", "arguments": ["clang++", "-DENGINE"]}
        for header in ("Gpu/GpuOwned.h", "Scene/DrawItem.h", "Scene/SceneTables.h"):
            with self.subTest(header=header):
                self.assertIs(self.entry("Source/Engine/" + header, [asset, engine]), engine)
        self.assertIs(self.entry("Source/Engine/Asset/RepositoryAsset.h", [engine, asset]), asset)

    def test_stem_match_is_considered_only_after_contract_ownership(self) -> None:
        entries = [{"file": "Source/Engine/Asset/SceneTables.cpp"},
                   {"file": "Source/Engine/Gpu/GpuBuffer.cpp"}]
        self.assertIs(self.entry("Source/Engine/Scene/SceneTables.h", entries), entries[1])

    def test_nested_app_model_borrows_model_command_not_app_command(self) -> None:
        entries = [{"file": "Source/App/Shell/GraphInspectorModel.cpp"},
                   {"file": "Source/App/Model/Graph/GraphNodeModel.cpp"}]
        self.assertIs(self.entry("Source/App/Model/Graph/GraphInspectorModel.h", entries), entries[1])
        self.assertIs(self.entry("Source/App/Shell/Tool.h", list(reversed(entries))), entries[0])

    def test_same_owner_counterpart_precedes_lexical_fallback(self) -> None:
        entries = [{"file": "Source/Engine/Alpha.cpp"}, {"file": "Source/Engine/Gpu/Owner.cpp"}]
        self.assertIs(self.entry("Source/Engine/Gpu/Owner.h", entries), entries[1])

    def test_fallback_and_duplicate_source_context_are_stable_under_db_order(self) -> None:
        entries = [{"file": "Source/Engine/Zulu.cpp", "arguments": ["clang++", "-DZ"]},
                   {"file": "Source/Engine/Alpha.cpp", "arguments": ["clang++", "-DB"]},
                   {"file": "Source/Engine/Alpha.cpp", "arguments": ["clang++", "-DA"]}]
        self.assertIs(self.entry("Source/Engine/Scene/Header.h", entries), entries[2])
        self.assertIs(self.entry("Source/Engine/Scene/Header.h", list(reversed(entries))), entries[2])

    def test_source_identity_uses_entry_directory_without_mutating_entry(self) -> None:
        entry = {"file": "../Source/Engine/Gpu/Owner.cpp", "directory": "/repo/build",
                 "arguments": ["clang++", "-g", "-O0", "-DLMX_PENDING_POST", "-I../Source"]}
        original = json.loads(json.dumps(entry))
        foreign = {"file": "../Source/Engine/Asset/Owner.cpp", "directory": "/repo/build"}
        self.assertIs(self.entry("Source/Engine/Gpu/Owner.h", [foreign, entry]), entry)
        self.assertEqual(entry, original)
        relative_directory = dict(entry, directory="build")
        self.assertIs(self.entry("Source/Engine/Gpu/Owner.h", [relative_directory]), relative_directory)

    def test_source_traversal_is_normalized_before_ownership(self) -> None:
        entries = [{"file": "Source/Engine/../../RojoRHI/Source/Engine/Probe.cpp"},
                   {"file": "Source/Engine/Gpu/Real.cpp"}]
        self.assertIs(self.entry("Source/Engine/Gpu/Probe.h", entries), entries[1])

    def test_missing_owner_and_missing_owner_context_are_explicit(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "no unit owns Source/Unknown/Header.h"):
            self.entry("Source/Unknown/Header.h", [{"file": "Source/Core/Probe.cpp"}])
        for entries in ([], [{"file": "Source/Engine/Asset/Header.cpp"}]):
            with self.subTest(entries=entries):
                with self.assertRaisesRegex(RuntimeError, "no compiled source found for unit engine"):
                    self.entry("Source/Engine/Gpu/Header.h", entries)

    def test_ast_uses_selected_directory_and_retains_flags_and_real_findings(self) -> None:
        root = Path("/repo")
        entry = {"file": "../Source/Engine/Gpu/Probe.cpp", "directory": "/repo/build",
                 "arguments": ["clang++", "-g", "-O0", "-DLMX_PENDING_POST", "-I../Source",
                               "-c", "../Source/Engine/Gpu/Probe.cpp", "-o", "probe.o"]}
        header = root / "Source/Engine/Gpu/Probe.h"
        ast = {"kind": "FunctionDecl", "name": "undocumented",
               "loc": {"file": str(header), "line": 5, "offset": 100}}
        result = subprocess.CompletedProcess([], 0, json.dumps(ast), "")
        original = json.loads(json.dumps(entry))
        with patch.object(comments.subprocess, "run", return_value=result) as run:
            objects, error = comments._ast_for_header(header, [entry], root, self.contract)
        self.assertIsNone(error)
        command = run.call_args.args[0]
        self.assertEqual(command[:5], entry["arguments"][:5])
        self.assertNotIn("../Source/Engine/Gpu/Probe.cpp", command)
        self.assertNotIn("probe.o", command)
        self.assertIn(str(header), command)
        self.assertEqual(run.call_args.kwargs["cwd"], Path("/repo/build"))
        self.assertEqual(entry, original)
        self.assertEqual(comments.public_api_findings_from_ast(
            Path("Source/Engine/Gpu/Probe.h"), source("Probe.h"), objects
        ), ["Source/Engine/Gpu/Probe.h:5: public API declaration has no Doxygen comment"])

    def test_root_cwd_keeps_root_relative_header_and_compiler_errors_visible(self) -> None:
        root = Path("/repo")
        entry = {"file": "Source/Engine/Gpu/Probe.cpp", "directory": "/repo",
                 "arguments": ["clang++", "-DRETAIN", "-c", "Source/Engine/Gpu/Probe.cpp", "-o", "probe.o"]}
        result = subprocess.CompletedProcess([], 1, "", "real source error")
        with patch.object(comments.subprocess, "run", return_value=result) as run:
            objects, error = comments._ast_for_header(root / "Source/Engine/Gpu/Probe.h", [entry], root, self.contract)
        self.assertEqual(objects, [])
        self.assertEqual(error, "real source error")
        self.assertIn("Source/Engine/Gpu/Probe.h", run.call_args.args[0])
        self.assertEqual(run.call_args.kwargs["cwd"], root)


if __name__ == "__main__":
    unittest.main()
