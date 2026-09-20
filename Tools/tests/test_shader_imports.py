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
            "Passes/Scene/Scene.slang": '[shader("fragment")] void m(){}\n'
                                        "import Lighting; import Nested.Color;",
            "Tests/Oracle.slang": "import Lighting;",
            "Common/Lighting.slang": "import Nested::Color;",
            "Common/Nested/Color.slang": "float value;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (4, 4))

    def test_comments_and_strings_are_not_imports(self) -> None:
        errors, _, imports = self.check({
            "Passes/Scene/Scene.slang":
                '// import Bad;\n/* import Bad; */\n"import Bad;";\nimport Color;',
            "Common/Color.slang": "float value;",
        })
        self.assertEqual(errors, [])
        self.assertEqual(imports, 1)

    def test_multiline_imports_and_comments_preserve_diagnostic_lines(self) -> None:
        errors, _, imports = self.check({
            "Passes/Scene/Scene.slang":
                "/* comment\ncomment */\nimport /* shared */\n Color\n;\nimport Missing;",
            "Common/Color.slang": "",
        })
        self.assertEqual(imports, 2)
        self.assertEqual(len(errors), 1)
        self.assertIn("Shaders/Passes/Scene/Scene.slang:6: unresolved", errors[0])

    def test_comment_and_string_entry_examples_do_not_make_module_an_entry(self) -> None:
        errors, _, _ = self.check({
            "Common/Color.slang": '// [shader("compute")]\n"[shader( ";',
        })
        self.assertEqual(errors, [])

    def test_imports_never_reach_production_or_test_entries(self) -> None:
        targets = {"Passes/Other/Pass.slang": '[shader("compute")] void m(){}',
                   "Tests/Oracle.slang": ""}
        for source in ("Passes/Scene/Scene.slang", "Common/Shared.slang", "Tests/Other.slang"):
            for target, entry in targets.items():
                with self.subTest(source=source, target=target):
                    errors, _, _ = self.check(
                        {source: f"import {Path(target).stem};", target: entry})
                    self.assertTrue(any("reaches entry point" in error for error in errors))

    def test_missing_and_wrong_case_modules_fail(self) -> None:
        for name in ("Missing", "color"):
            errors, _, _ = self.check({
                "Passes/Scene/Scene.slang": f"import {name};",
                "Common/Color.slang": "",
            })
            self.assertTrue(any("unresolved module" in error for error in errors))

    def test_duplicate_output_basenames_fail_including_case_collisions(self) -> None:
        for duplicate in ("Color", "color"):
            errors, _, _ = self.check({"Common/Color.slang": "", f"Tests/{duplicate}.slang": ""})
            self.assertTrue(any("duplicate shader output basename" in error for error in errors))

    def test_modules_cannot_own_entry_points(self) -> None:
        errors, _, _ = self.check({"Common/Color.slang": '[shader("compute")] void main() {}'})
        self.assertTrue(any("cannot declare shader entry points" in error for error in errors))

    def test_unsupported_and_incomplete_imports_fail(self) -> None:
        for declaration in ('import "../Tests/Oracle";', "import Color", "import ;"):
            errors, _, _ = self.check({"Passes/Scene/Scene.slang": declaration})
            self.assertTrue(any("expected a named module import" in error for error in errors))

    def test_textual_include_cannot_bypass_module_boundary(self) -> None:
        errors, _, _ = self.check({"Passes/Scene/Scene.slang": '#include "Tests/Oracle.slang"'})
        self.assertTrue(any("textual includes" in error for error in errors))

    def test_family_entries_import_siblings_and_common(self) -> None:
        errors, files, imports = self.check({
            "Passes/Visibility/Classify.slang": '[shader("compute")] void m(){}\n'
                                                "import Visibility; import SceneTables;",
            "Passes/Visibility/Visibility.slang": "import SceneTables;",
            "Common/SceneTables.slang": "float value;",
            "Tests/Probe.slang": "import Visibility;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (4, 4))

    def test_family_local_module_is_private_to_its_folder(self) -> None:
        for importer in ("Passes/Scene/Scene.slang", "Common/Shared.slang"):
            with self.subTest(importer=importer):
                errors, _, _ = self.check({
                    importer: "import Visibility;",
                    "Passes/Visibility/Visibility.slang": "float value;",
                })
                self.assertTrue(any("is local to Shaders/Passes/Visibility" in e for e in errors))

    def test_imports_never_reach_family_entries(self) -> None:
        errors, _, _ = self.check({
            "Passes/Scene/A.slang": "import B;",
            "Passes/Scene/B.slang": '[shader("fragment")] void m(){}',
        })
        self.assertTrue(any("reaches entry point" in e for e in errors))

    def test_files_outside_the_three_folders_fail(self) -> None:
        for stray in ("Passes/Loose.slang", "Passes/Scene/Deep/X.slang", "Other/X.slang"):
            with self.subTest(stray=stray):
                errors, _, _ = self.check({stray: "float value;"})
                self.assertTrue(any("must live in Common/, Tests/ or Passes/<family>/" in e
                                    for e in errors))

    def test_legacy_root_and_modules_folders_fail(self) -> None:
        for stray in ("Scene.slang", "Modules/Color.slang"):
            with self.subTest(stray=stray):
                errors, _, _ = self.check({stray: "float value;"})
                self.assertTrue(any("must live in Common/, Tests/ or Passes/<family>/" in e
                                    for e in errors))

    def test_collisions_are_rejected_across_family_folders(self) -> None:
        errors, _, _ = self.check({"Passes/Scene/Dup.slang": "", "Passes/Bloom/dup.slang": ""})
        self.assertTrue(any("duplicate shader output basename" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
