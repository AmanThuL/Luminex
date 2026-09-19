from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

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
        errors = comments.check_file_header(Path("RojoRHI/Include/rojoRHI/Example.h"), text)
        self.assertTrue(any(":1:" in error for error in errors))
        self.assertTrue(any("basename" in error or "expected" in error for error in errors))

    def test_file_header_requires_nonempty_brief_and_closing_ruler(self) -> None:
        text = "\n".join(
            [comments.FILE_RULER, "/// @file Example.cpp", "/// @brief ", "//----"]
        )
        errors = comments.check_file_header(Path("Source/Example.cpp"), text)
        self.assertTrue(any("non-empty" in error for error in errors))
        self.assertTrue(any("closing" in error for error in errors))

    def test_owned_roots_include_source_and_root_rhi_but_not_tests(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in (
                "Source/App.cpp",
                "RojoRHI/Include/rojoRHI/RHI.h",
                "Tests/Test.cpp",
                # Test sources are exempt wherever they live, including inside the RHI component.
                "RojoRHI/Tests/RHITest.cpp",
                "RojoRHI/Tests/RHITestSupport.h",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(
                [path.relative_to(root).as_posix() for path in comments.project_cpp_files(root)],
                ["RojoRHI/Include/rojoRHI/RHI.h", "Source/App.cpp"],
            )

    def test_public_header_roots_exclude_rhi_backend_headers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in (
                "Source/App/Public.h",
                "RojoRHI/Include/rojoRHI/RHI.h",
                "RojoRHI/Backends/Metal4/ImGui/Include/rojoRHI/Metal4/Metal4ImGui.h",
                "RojoRHI/Backends/Metal4/Source/Private.h",
                "RojoRHI/Source/Validate.h",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(
                [path.relative_to(root).as_posix() for path in comments.public_header_files(root)],
                [
                    "RojoRHI/Backends/Metal4/ImGui/Include/rojoRHI/Metal4/Metal4ImGui.h",
                    "RojoRHI/Include/rojoRHI/RHI.h",
                    "Source/App/Public.h",
                ],
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


if __name__ == "__main__":
    unittest.main()
