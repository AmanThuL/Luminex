from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from Tools import check_project_policy as policy


class ProjectPolicyTests(unittest.TestCase):
    def test_session_actor_allowlist_applies_across_app_code_and_tests(self) -> None:
        code = (
            "enum class Actor { Operator, System, Agent };\n"
            "Actor::Agent; Provenance::AgentApplied; ThemeRole::AccentAgentText;\n"
            'ThemeRole::ActorAgent; const char* label = "Agent";\n'
        )
        for path in ("Source/App/Model/Session/SessionLog.cpp",
                     "Tests/App/Model/Session/AppSessionLogTests.cpp"):
            with self.subTest(path=path):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([Path(path)], errors)
                self.assertEqual(errors, [])

    def test_session_actor_allowlist_does_not_hide_comments_or_other_units(self) -> None:
        for path, code in (
            ("Source/App/Model/Session/SessionLog.cpp", "// agent action\nActor::Agent;"),
            ("Tests/App/Model/Session/AppSessionLogTests.cpp", "/* Agent */ Actor::Agent;"),
            ("Source/Render/Renderer/Renderer.cpp", 'Actor::Agent; "Agent";'),
        ):
            with self.subTest(path=path, code=code):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([Path(path)], errors)
                self.assertTrue(errors)

    def test_session_actor_allowlist_does_not_apply_to_commit_subjects(self) -> None:
        errors: list[str] = []
        with (
            mock.patch.object(policy, "git", return_value="0123456789abcdef\0tool: allow agent names\0"),
            mock.patch.object(policy, "author_identity_tokens", return_value=set()),
        ):
            policy.check_commit_messages("base..head", errors)
        self.assertTrue(any("implementation-history" in error for error in errors))

    def test_read_text_accepts_utf8_without_extension_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scene.json").write_text('{"scene": "test"}', encoding="utf-8")
            with mock.patch.object(policy, "ROOT", root):
                self.assertEqual(policy.read_text(Path("scene.json")), '{"scene": "test"}')

    def test_read_text_skips_binary_data(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "capture.bin").write_bytes(b"capture\0payload")
            with mock.patch.object(policy, "ROOT", root):
                self.assertIsNone(policy.read_text(Path("capture.bin")))

    def test_identity_check_covers_repository_relative_paths(self) -> None:
        errors: list[str] = []
        path = Path("docs/privatehandle-notes.md")
        with (
            mock.patch.object(policy, "author_identity_tokens", return_value={"privatehandle"}),
            mock.patch.object(policy, "read_text", return_value="safe content"),
        ):
            policy.check_identity([path], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("repository-relative path", errors[0])

    def test_identity_check_covers_text_regardless_of_suffix(self) -> None:
        errors: list[str] = []
        with (
            mock.patch.object(policy, "author_identity_tokens", return_value={"privatehandle"}),
            mock.patch.object(policy, "read_text", return_value='owner = "privatehandle"'),
        ):
            policy.check_identity([Path("settings.toml")], errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("outside metadata", errors[0])

    def test_markdown_check_rejects_unknown_status(self) -> None:
        path = Path("docs/milestones/r/example.md")
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value="# Example\n\n**Status**: Maybe\n"):
            policy.check_markdown([path], errors)
        self.assertTrue(any("unsupported document status" in error for error in errors))

    def test_markdown_check_rejects_record_outside_series_folder(self) -> None:
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value="# X\n\n**Status**: Proposed\n"):
            policy.check_markdown([Path("docs/milestones/r9.md")], errors)
        self.assertTrue(any("series folder" in error for error in errors))

    def test_status_tables_drop_removed_folders(self) -> None:
        self.assertNotIn("specs", policy.STATUS_DIRS)
        self.assertNotIn("postmortems", policy.STATUS_DIRS)
        self.assertNotIn("docs/postmortems/", policy.LINE_BUDGETS)

    def test_line_budget_exempts_retained_designs(self) -> None:
        errors: list[str] = []
        paths = [Path("docs/milestones/m6/m6.2-design.md"), Path("docs/milestones/m6/m6.2.md")]
        with mock.patch.object(policy, "read_text", return_value="line\n" * 301):
            policy.check_line_budgets(paths, errors)
        self.assertEqual(len(errors), 1)
        self.assertIn("m6.2.md", errors[0])

    def test_commit_policy_rejects_process_transcript_and_past_tense_subject(self) -> None:
        records = (
            "0123456789abcdef\0scene: added Sponza\n\nClose the backlog after subagent review.\0"
        )
        errors: list[str] = []
        with (
            mock.patch.object(policy, "git", return_value=records),
            mock.patch.object(policy, "author_identity_tokens", return_value=set()),
        ):
            policy.check_commit_messages("base..head", errors)
        self.assertTrue(any("imperative mood" in error for error in errors))
        self.assertTrue(any("implementation-history" in error for error in errors))

    def test_commit_policy_accepts_an_engineering_outcome(self) -> None:
        records = "0123456789abcdef\0render: establish the Metal baseline\0"
        errors: list[str] = []
        with (
            mock.patch.object(policy, "git", return_value=records),
            mock.patch.object(policy, "author_identity_tokens", return_value=set()),
        ):
            policy.check_commit_messages("base..head", errors)
        self.assertEqual(errors, [])

    def test_commit_policy_accepts_a_squash_merged_spike(self) -> None:
        records = "0123456789abcdef\0spike: decide the RHI execution model (#11)\0"
        errors: list[str] = []
        with (
            mock.patch.object(policy, "git", return_value=records),
            mock.patch.object(policy, "author_identity_tokens", return_value=set()),
        ):
            policy.check_commit_messages("base..head", errors)
        self.assertEqual(errors, [])

    def test_public_copy_rejects_internal_milestones_and_unshipped_backends(self) -> None:
        errors: list[str] = []
        with mock.patch.object(
            policy,
            "read_text",
            return_value="M4 adds a Vulkan backend after the current renderer.",
        ):
            policy.check_public_copy([Path("README.md")], errors)
        self.assertEqual(len(errors), 2)

    def test_public_copy_accepts_shipped_features_and_plain_future_themes(self) -> None:
        errors: list[str] = []
        with mock.patch.object(
            policy,
            "read_text",
            return_value="Metal 4 renderer. Next: HDR and physically based materials.",
        ):
            policy.check_public_copy([Path("README.md")], errors)
        self.assertEqual(errors, [])

    def test_process_policy_covers_extracted_build_scripts(self) -> None:
        for path in ("xmake/setup.lua", "Source/Core/xmake.lua"):
            with self.subTest(path=path):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value="-- Close the backlog."):
                    policy.check_process_narration([Path(path)], errors)
                self.assertEqual(len(errors), 1)

    def test_session_labels_are_allowed_without_site_exemptions(self) -> None:
        for label in ('"Agent"', '"Agent · working"'):
            for path in ("Source/App/Panels/Gallery/StyleGalleryPanel.cpp",
                         "Source/App/Shell/EditorShell.cpp",
                         "Tests/App/Model/Session/AppSessionLogTests.cpp"):
                with self.subTest(label=label, path=path):
                    errors: list[str] = []
                    with mock.patch.object(policy, "read_text", return_value=f"label({label});"):
                        policy.check_process_narration([Path(path)], errors)
                    self.assertEqual(errors, [])

    def test_session_actor_identifiers_are_exact(self) -> None:
        path = Path("Source/App/Model/Session/SessionLog.cpp")
        for code in ("Actor::Agent;", "Actor /* actor */ :: Agent;",
                     "enum class Actor { Operator, System, Agent };",
                     "Provenance::AgentApplied;", "ThemeRole::AccentAgentText;",
                     "ThemeRole::ActorAgent;"):
            with self.subTest(code=code):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertEqual(errors, [])
        for code in ("OtherActor::Agent;", "enum class Other { Operator, System, Agent };",
                     "enum class Actor { Agent };", '"Agent implementation";'):
            with self.subTest(code=code):
                errors = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertTrue(errors)

    def test_session_actor_copy_rejects_noncode_and_adjacent_narration(self) -> None:
        path = Path("Source/App/Panels/Shared/EditorStyle.cpp")
        for code in ("// Actor::Agent", "/* Actor::Agent */", '"Actor::Agent"',
                     '"Actor::Agent', '"Actor::Agent\\',
                     'R"(Actor::Agent)"', "#define ACTOR Actor::Agent",
                     "%:define ACTOR Actor::Agent", '#define LABEL "Agent"',
                     '%:define LABEL "Agent"', 'R"(Agent)"',
                     "// continued \\\nActor::Agent", "/\\\n/ Actor::Agent",
                     "/\\\r\n/ Actor::Agent", "/\\ \t\n/ Actor::Agent",
                     "/* prefix */ #define ACTOR Actor::Agent",
                     "#define ACTOR /* continued\n*/ Actor::Agent",
                     "%:define ACTOR \\\nActor::Agent",
                     'u8R"copy(Actor::Agent)copy"',
                     'R"copy()copy\\\n" Actor::Agent)copy";',
                     'R"copy("quoted"\nActor::Agent',
                     'Actor::Agent; // agent narration',
                     'label("Agent"); // agent narration'):
            with self.subTest(code=code):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertTrue(errors)

    def test_session_actor_declaration_rejects_lexical_lookalikes(self) -> None:
        path = Path("Source/App/Model/Workspace/Provenance.h")
        declaration = "enum class Actor { Operator, System, Agent };"
        for code in (
            "// " + declaration,
            "/*\n" + declaration + "\n*/",
            "/*\n" + declaration,
            'const char* text = "' + declaration + '";',
            'const char* text = "' + declaration,
            'const char* text = "' + declaration + "\\",
            'const char* text = R"(' + declaration + ')";',
            'const char* text = u8R"copy(\n' + declaration + '\n)copy";',
            'const char* text = R"copy("quoted"\n' + declaration,
            'R"copy()copy\\\n" ' + declaration + ')copy";',
            "const auto text = '" + declaration + "';",
            "const auto text = '" + declaration,
            "const auto text = '" + declaration + "\\",
            "// continued comment \\\n" + declaration,
            "#define DECLARATION " + declaration,
            "%:define DECLARATION " + declaration,
            "/* prefix */ #define DECLARATION " + declaration,
            "#define DECLARATION /* continued\n*/ " + declaration,
            "enum class Actor { Operator, System, Agent, Extra };",
        ):
            with self.subTest(code=code):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertTrue(errors)

    def test_session_label_rejects_lexical_lookalikes(self) -> None:
        path = Path("Tests/App/Model/Session/AppSessionLogTests.cpp")
        for code in (
            '// label("Agent");',
            '/* label("Agent"); */',
            'R"(label("Agent");)"',
            'u8R"copy(label("Agent");)copy"',
            '#define LABEL "Agent"',
            '%:define LABEL "Agent"',
            '#define LABEL /* continued\n*/ "Agent"',
            '// continued \\\nlabel("Agent");',
            'label("Agent implementation");',
        ):
            with self.subTest(code=code):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertTrue(errors)

    def test_session_actor_allowlist_keeps_preprocessor_and_literal_barriers(self) -> None:
        path = Path("Source/App/Model/Workspace/Provenance.h")
        fixtures = (
            "enum class Actor { Operator, System, Agent };",
            "Actor::Agent;",
            '"Agent"',
            '"Agent · working"',
        )
        prefixes = (
            "/* prefix */ #define DECLARATION ",
            "/* prefix\n*/ #define DECLARATION ",
            "%:define DECLARATION ",
            "/* prefix */ %:define DECLARATION ",
            "/\\\n/ ",
            "/\\\r\n/ ",
            "/\\ \t\n/ ",
            "/\\\n* ",
            "#define DECLARATION /* continued\n*/ ",
            "%:define DECLARATION \\\n",
            "#define DECLARATION /\\\n* continued\n*/ ",
        )
        for fixture in fixtures:
            variants = [prefix + fixture for prefix in prefixes]
            variants.extend((
                'R"copy()copy\\\n" ' + fixture + ')copy";',
                'u8R"copy(\n' + fixture + '\n)copy";',
                "constexpr auto count = 1'000;\n"
                'const char* text = "don\'t ' + fixture.replace('"', '\\"') + '";',
            ))
            for code in variants:
                with self.subTest(fixture=fixture, code=code):
                    errors: list[str] = []
                    with mock.patch.object(policy, "read_text", return_value=code):
                        policy.check_process_narration([path], errors)
                    self.assertEqual(
                        errors,
                        [f"{path}:{code.count(chr(10), 0, code.index('Agent')) + 1}: "
                         "implementation-history narration"],
                    )

    def test_session_actor_allowlist_accepts_logical_code_after_splicing(self) -> None:
        path = Path("Tests/App/Model/Session/AppSessionLogTests.cpp")
        for newline in ("\n", "\r\n"):
            code = newline.join((
                "#pragma once",
                "enum /* attribution */ class Actor { Operator, \\",
                "System, /* software source */ Agent, };",
                "auto actor = Actor /* source */ :: Agent;",
                'const char* label = "Agent";',
            ))
            with self.subTest(newline=newline):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=code):
                    policy.check_process_narration([path], errors)
                self.assertEqual(errors, [])

    def test_session_actor_copy_keeps_original_line_numbers(self) -> None:
        path = Path("Tests/App/Model/Session/AppSessionLogTests.cpp")
        code = 'Actor::Agent;\n// agent narration\nlabel("Agent");\n'
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value=code):
            policy.check_process_narration([path], errors)
        self.assertEqual(errors, [f"{path}:2: implementation-history narration"])

    def test_component_paths_are_not_process_roots(self) -> None:
        self.assertFalse(any(root.startswith("RojoRHI") for root in policy.PROCESS_ROOTS))


if __name__ == "__main__":
    unittest.main()
