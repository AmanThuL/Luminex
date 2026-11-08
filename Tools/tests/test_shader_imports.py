from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from Tools.check_shader_imports import check_shaders


class ShaderImportTests(unittest.TestCase):
    def check(self, sources: dict[str, str], external: str | None = None) -> tuple[list[str], int, int]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in sources.items():
                path = root / "Shaders" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            if external is not None:
                path = root / external
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
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
                   "Tests/Oracle.slang": '[shader("compute")] void m(){}'}
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

    def test_public_rhi_module_is_available_to_every_role(self) -> None:
        errors, files, imports = self.check({
            "Common/Roots.slang": "import RojoRHI;",
            "Passes/Scene/Scene.slang": '[shader("compute")] void m(){} import RojoRHI;',
            "Tests/OracleRoots.slang": "import RojoRHI;",
            "Layout/LuminexLayout.slang": "import RojoRHI;",
            "Layout/TestLayout.slang": "import RojoRHI;",
        }, "RojoRHI/Shaders/RojoRHI/RojoRHI.slang")
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (5, 5))

    def test_public_rhi_requires_the_exact_mounted_file(self) -> None:
        for external in (None, "RojoRHI/Shaders/Private/RojoRHI.slang",
                         "RojoRHI/Shaders/RojoRHI/rojorhi.slang",
                         "rojorhi/Shaders/RojoRHI/RojoRHI.slang"):
            with self.subTest(external=external):
                errors, _, _ = self.check({"Common/Roots.slang": "import RojoRHI;"}, external)
                self.assertTrue(any("is missing at RojoRHI/Shaders/RojoRHI/RojoRHI.slang" in e
                                    for e in errors))

    def test_external_private_alias_and_wrong_case_imports_fail(self) -> None:
        for name in ("rojorhi", "RojoRhi", "RojoRHI.Private", "RojoRHI::Private",
                     "Shaders.RojoRHI", "Alias = RojoRHI", "RojoRHI as Alias",
                     "../RojoRHI", "RojoRHI/Private"):
            with self.subTest(name=name):
                errors, _, _ = self.check({"Common/Roots.slang": f"import {name};"},
                                         "RojoRHI/Shaders/RojoRHI/RojoRHI.slang")
                self.assertTrue(errors)

    def test_existing_component_private_and_other_external_files_stay_unresolved(self) -> None:
        for name, external in (("Private", "RojoRHI/Shaders/Private.slang"),
                               ("OtherExternal", "OtherExternal/Shaders/OtherExternal.slang")):
            with self.subTest(name=name):
                errors, _, _ = self.check({"Common/Roots.slang": f"import {name};"}, external)
                self.assertTrue(any("unresolved module import" in e for e in errors))

    def test_local_sources_cannot_shadow_public_rhi(self) -> None:
        for local in ("Common/RojoRHI.slang", "Tests/rojorhi.slang",
                      "Passes/Scene/RojoRHI.slang", "Common/rojorhi/Private.slang"):
            with self.subTest(local=local):
                errors, _, _ = self.check({local: "", "Common/Roots.slang": "import RojoRHI;"},
                                         "RojoRHI/Shaders/RojoRHI/RojoRHI.slang")
                self.assertTrue(any("cannot shadow the public RojoRHI" in e for e in errors))

    def test_tests_and_test_probe_import_test_owned_root_modules(self) -> None:
        errors, files, imports = self.check({
            "Tests/OracleRoots.slang": "import Nested.Helper; import Visibility;",
            "Tests/Nested/Helper.slang": "import Color;",
            "Tests/Oracle.slang": '[shader("compute")] void m(){} import OracleRoots;',
            "Passes/Visibility/Visibility.slang": "import Color;",
            "Common/Color.slang": "",
            "Layout/TestLayout.slang": "import OracleRoots; import Visibility; import Color;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (6, 8))

    def test_production_and_production_probe_cannot_import_test_modules(self) -> None:
        for importer in ("Common/Shared.slang", "Passes/Scene/Scene.slang",
                         "Layout/LuminexLayout.slang"):
            for name, target in (("OracleRoots", "Tests/OracleRoots.slang"),
                                 ("Nested.Helper", "Tests/Nested/Helper.slang")):
                with self.subTest(importer=importer, name=name):
                    errors, _, _ = self.check({importer: f"import {name};", target: ""})
                    self.assertTrue(any("is test-only" in e for e in errors))

    def test_both_probes_can_reflect_common_and_private_production_roots(self) -> None:
        errors, files, imports = self.check({
            "Common/Shared.slang": "",
            "Passes/Scene/SceneRoots.slang": "import Shared;",
            "Passes/Display/DisplayRoots.slang": "import Shared;",
            "Layout/LuminexLayout.slang": "import SceneRoots; import DisplayRoots; import Shared;",
            "Layout/TestLayout.slang": "import SceneRoots; import DisplayRoots; import Shared;",
        })
        self.assertEqual(errors, [])
        self.assertEqual((files, imports), (5, 8))

    def test_probes_are_leaf_files_for_all_importers(self) -> None:
        for importer in ("Common/Shared.slang", "Passes/Scene/Scene.slang", "Tests/Oracle.slang",
                         "Layout/LuminexLayout.slang", "Layout/TestLayout.slang"):
            for probe in ("LuminexLayout", "TestLayout"):
                with self.subTest(importer=importer, probe=probe):
                    sources = {f"Layout/{probe}.slang": "", importer: f"import {probe};"}
                    errors, _, _ = self.check(sources)
                    self.assertTrue(any("probes are not importable" in e for e in errors))

    def test_probes_cannot_declare_entries_or_import_entries(self) -> None:
        for probe in ("LuminexLayout", "TestLayout"):
            with self.subTest(probe=probe):
                errors, _, _ = self.check({
                    f"Layout/{probe}.slang": '[shader("compute")] void m(){} import Entry;',
                    "Passes/Scene/Entry.slang": '[shader("compute")] void m(){}',
                })
                self.assertTrue(any("reflection probes cannot declare" in e for e in errors))
                self.assertTrue(any("reaches entry point" in e for e in errors))

    def test_unknown_nested_and_wrong_case_layout_roles_fail(self) -> None:
        for stray in ("Layout/Other.slang", "Layout/Deep/TestLayout.slang",
                      "Layout/testlayout.slang", "layout/TestLayout.slang"):
            with self.subTest(stray=stray):
                errors, _, _ = self.check({stray: ""})
                self.assertTrue(any("explicit Layout/ reflection probe" in e for e in errors))

    def test_probes_and_test_modules_share_the_flat_artifact_namespace(self) -> None:
        for first, second in (("Layout/TestLayout.slang", "Tests/testlayout.slang"),
                              ("Tests/OracleRoots.slang", "Common/oracleroots.slang")):
            with self.subTest(first=first, second=second):
                errors, _, _ = self.check({first: "", second: ""})
                self.assertTrue(any("duplicate shader output basename" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
