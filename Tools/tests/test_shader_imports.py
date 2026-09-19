from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from Tools.check_shader_imports import check_shaders


class ShaderImportTests(unittest.TestCase):
    def check(self, sources: dict[str, str]) -> tuple[list[str], int, int]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in sources.items():
                path = root / "Shaders" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            return check_shaders(root)

    def test_entries_oracles_and_modules_import_shared_modules(self) -> None:
        errors, files, imports = self.check({
            "Scene.slang": "import Lighting; import Nested.Color;",
            "Tests/Oracle.slang": "import Lighting;",
            "Modules/Lighting.slang": "import Nested::Color;",
            "Modules/Nested/Color.slang": "float value;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (4, 4))

    def test_comments_and_strings_are_not_imports(self) -> None:
        errors, _, imports = self.check({
            "Scene.slang": '// import Bad;\n/* import Bad; */\n"import Bad;";\nimport Color;',
            "Modules/Color.slang": "float value;",
        })
        self.assertEqual(errors, [])
        self.assertEqual(imports, 1)

    def test_multiline_imports_and_comments_preserve_diagnostic_lines(self) -> None:
        errors, _, imports = self.check({
            "Scene.slang": "/* comment\ncomment */\nimport /* shared */\n Color\n;\nimport Missing;",
            "Modules/Color.slang": "",
        })
        self.assertEqual(imports, 2)
        self.assertEqual(len(errors), 1)
        self.assertIn("Shaders/Scene.slang:6: unresolved", errors[0])

    def test_comment_and_string_entry_examples_do_not_make_module_an_entry(self) -> None:
        errors, _, _ = self.check({
            "Modules/Color.slang": '// [shader("compute")]\n"[shader( ";',
        })
        self.assertEqual(errors, [])

    def test_imports_never_reach_production_or_test_entries(self) -> None:
        for source in ("Scene.slang", "Modules/Shared.slang", "Tests/Other.slang"):
            for target in ("Pass.slang", "Tests/Oracle.slang"):
                with self.subTest(source=source, target=target):
                    errors, _, _ = self.check({source: f"import {Path(target).stem};", target: ""})
                    self.assertTrue(any("imports may target Shaders/Modules only" in error for error in errors))

    def test_missing_and_wrong_case_modules_fail(self) -> None:
        for name in ("Missing", "color"):
            errors, _, _ = self.check({"Scene.slang": f"import {name};", "Modules/Color.slang": ""})
            self.assertTrue(any("unresolved module" in error for error in errors))

    def test_duplicate_output_basenames_fail_including_case_collisions(self) -> None:
        for duplicate in ("Color", "color"):
            errors, _, _ = self.check({"Modules/Color.slang": "", f"Tests/{duplicate}.slang": ""})
            self.assertTrue(any("duplicate shader output basename" in error for error in errors))

    def test_modules_cannot_own_entry_points(self) -> None:
        errors, _, _ = self.check({"Modules/Color.slang": '[shader("compute")] void main() {}'})
        self.assertTrue(any("cannot declare shader entry points" in error for error in errors))

    def test_unsupported_and_incomplete_imports_fail(self) -> None:
        for declaration in ('import "../Tests/Oracle";', "import Color", "import ;"):
            errors, _, _ = self.check({"Scene.slang": declaration})
            self.assertTrue(any("expected a named module import" in error for error in errors))

    def test_textual_include_cannot_bypass_module_boundary(self) -> None:
        errors, _, _ = self.check({"Scene.slang": '#include "Tests/Oracle.slang"'})
        self.assertTrue(any("textual includes" in error for error in errors))


class ComponentTreeTests(unittest.TestCase):
    """The RHI component owns a second tree whose modules sit beside its oracles."""

    def check(self, sources: dict[str, str]) -> tuple[list[str], int, int]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in sources.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            return check_shaders(root)

    def test_component_oracle_imports_the_component_module(self) -> None:
        errors, files, imports = self.check({
            "Shaders/Scene.slang": "",
            "RHI/Shaders/Tests/ShadowSmoke.slang": "import Shadow;",
            "RHI/Shaders/Tests/Modules/Shadow.slang": "float value;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (3, 1))

    def test_component_oracle_cannot_reach_the_repository_modules(self) -> None:
        errors, _, _ = self.check({
            "Shaders/Modules/Lighting.slang": "float value;",
            "RHI/Shaders/Tests/Smoke.slang": "import Lighting;",
        })
        self.assertTrue(any("unresolved module import 'Lighting'" in error for error in errors))

    def test_the_two_trees_have_independent_basename_namespaces(self) -> None:
        errors, files, _ = self.check({
            "Shaders/Tests/Triangle.slang": "",
            "RHI/Shaders/Tests/Triangle.slang": "",
        })
        self.assertEqual(errors, [])
        self.assertEqual(files, 2)

    def test_component_modules_cannot_own_entry_points(self) -> None:
        errors, _, _ = self.check({
            "Shaders/Scene.slang": "",
            "RHI/Shaders/Tests/Modules/Shadow.slang": '[shader("compute")] void main() {}',
        })
        self.assertTrue(any("cannot declare shader entry points" in error for error in errors))

    def test_a_duplicate_inside_the_component_tree_still_fails(self) -> None:
        errors, _, _ = self.check({
            "Shaders/Scene.slang": "",
            "RHI/Shaders/Tests/Triangle.slang": "",
            "RHI/Shaders/Tests/Modules/triangle.slang": "",
        })
        self.assertTrue(any("duplicate shader output basename" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
