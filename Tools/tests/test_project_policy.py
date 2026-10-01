from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from Tools import check_project_policy as policy


class ProjectPolicyTests(unittest.TestCase):
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

    def test_gallery_actor_label_is_semantic_copy(self) -> None:
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value='kind == "Actor" ? "Agent · working" : "WARN";'):
            policy.check_process_narration(
                [Path("Source/App/Panels/Gallery/StyleGalleryPanel.cpp")], errors
            )
        self.assertEqual(errors, [])

    def test_gallery_semantic_label_accepts_formatter_alignment(self) -> None:
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value='kind == "Actor"      ? "Agent · working" : "WARN";'):
            policy.check_process_narration(
                [Path("Source/App/Panels/Gallery/StyleGalleryPanel.cpp")], errors
            )
        self.assertEqual(errors, [])

    def test_gallery_semantic_label_does_not_hide_process_narration(self) -> None:
        for text in (
            '// The agent completed this.\nkind == "Actor" ? "Agent · working" : "WARN";',
            '/*\nkind == "Actor" ? "Agent · working" : "WARN";\n*/',
            '// kind == "Actor" ? "Agent · working" : "WARN";',
            'const char* text = R"(kind == "Actor" ? "Agent · working" : "WARN";)";',

            'kind == "Actor" ? "Agent · working" : "WARN"; // agent implementation',
            'kind == "Actor" ? "Agent · working" : "WARN"; // Task 11',
        ):
            with self.subTest(text=text):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Panels/Gallery/StyleGalleryPanel.cpp")], errors
                    )
                self.assertTrue(errors)

    def test_actor_label_allowance_stays_in_gallery(self) -> None:
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value='kind == "Actor" ? "Agent · working" : "WARN";'):
            policy.check_process_narration([Path("Source/App/Shell/EditorShell.cpp")], errors)
        self.assertTrue(errors)

    def test_gallery_actor_copy_rejects_spliced_noncode_and_directives(self) -> None:
        fixture = 'kind == "Actor" ? "Agent · working" : "WARN";'
        for text in (
            "// continued \\\n" + fixture, "/\\\n/ " + fixture,
            "#define LABEL " + fixture, "%:define LABEL " + fixture,
            "/* prefix */ #define LABEL " + fixture,
            "#define LABEL /* continued\n*/ " + fixture,
            'u8R"copy(' + fixture + ')copy"',
            'R"copy()copy\\\n" ' + fixture + ')copy";',
            'void proposalSpecimenExtra() { ImGui::TextUnformatted("Agent"); }',
            'void proposalSpecimen() { #define LABEL ImGui::TextUnformatted("Agent"); }',
            'void proposalSpecimen() {\n#define LABEL ImGui::TextUnformatted("Agent");\n}',
        ):
            with self.subTest(text=text):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Panels/Gallery/StyleGalleryPanel.cpp")], errors)
                self.assertTrue(errors)

    def test_actor_references_and_proposal_fixture_are_semantic_code(self) -> None:
        path = Path("Source/App/Panels/Shared/EditorStyle.cpp")
        for text in ("case Actor::Agent:", "actorMark(Actor /* actor */ :: Agent);",
                     "Actor\\\n::Agent;"):
            with self.subTest(text=text):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration([path], errors)
                self.assertEqual(errors, [])
        fixture = 'void proposalSpecimen() { ImGui::TextUnformatted("Agent"); }'
        errors = []
        with mock.patch.object(policy, "read_text", return_value=fixture):
            policy.check_process_narration(
                [Path("Source/App/Panels/Gallery/StyleGalleryPanel.cpp")], errors)
        self.assertEqual(errors, [])

    def test_actor_reference_allowance_rejects_noncode_and_adjacent_narration(self) -> None:
        reference = "Actor::Agent"
        for text in (
            "// " + reference, "/* " + reference + " */", '"' + reference + '"',
            'R"(' + reference + ')"', 'u8R"copy(' + reference + ')copy"',
            "// continued \\\n" + reference, "/\\\n/ " + reference,
            "#define ACTOR " + reference, "%:define ACTOR " + reference,
            "/* prefix */ #define ACTOR " + reference,
            "#define ACTOR /* continued\n*/ " + reference,
            "OtherActor::Agent", "Actor::AgentExtra; // agent narration",
            reference + "; // agent narration", reference + '; "agent narration"',
            'void proposalSpecimen() { "Agent"; }',
            'void otherSpecimen() { ImGui::TextUnformatted("Agent"); }',
            '// void proposalSpecimen() { ImGui::TextUnformatted("Agent"); }',
            'R"(void proposalSpecimen() { ImGui::TextUnformatted("Agent"); })"',
            'void proposalSpecimen() {} ImGui::TextUnformatted("Agent");',
            'void proposalSpecimen() { ImGui::TextUnformatted("Agent"); /* agent narration */ }',
            'void proposalSpecimen() { ImGui::TextUnformatted("Agent implementation"); }',
        ):
            for path in ("Source/App/Panels/Shared/EditorStyle.cpp",
                         "Source/App/Panels/Gallery/StyleGalleryPanel.cpp"):
                with self.subTest(text=text, path=path):
                    errors: list[str] = []
                    with mock.patch.object(policy, "read_text", return_value=text):
                        policy.check_process_narration([Path(path)], errors)
                    self.assertTrue(errors)
        errors = []
        with mock.patch.object(policy, "read_text", return_value=reference):
            policy.check_process_narration([Path("Source/Core/Other.cpp")], errors)
        self.assertTrue(errors)

    def test_provenance_actor_enum_accepts_only_required_code_token(self) -> None:
        for text in (
            "enum class Actor { Operator, System, Agent };",
            "enum\tclass Actor {\r\n"
            "    Operator, ///< Human input.\r\n"
            "    System,   ///< Editor policy.\r\n"
            "    Agent,    ///< Software-attributed input.\r\n"
            "};\r\n",
        ):
            with self.subTest(text=text):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Model/Workspace/Provenance.h")], errors
                    )
                self.assertEqual(errors, [])

    def test_provenance_actor_enum_rejects_noncode_and_other_declarations(self) -> None:
        declaration = "enum class Actor { Operator, System, Agent };"
        for text in (
            "// " + declaration,
            "/*\n" + declaration + "\n*/",
            "/*\n" + declaration,
            'const char* text = "' + declaration + '";',
            'const char* text = "' + declaration,
            'const char* text = "' + declaration + "\\",
            'const char* text = "escaped \\\"' + declaration + '\\\"";',
            'const char* text = R"(' + declaration + ')";',
            'const char* text = u8R"copy(\n' + declaration + '\n)copy";',
            'const char* text = R"copy("quoted"\n' + declaration,
            'const char* text = R"copy()copy\\\n" ' + declaration + ')copy";',
            "constexpr auto count = 1'000;\n"
            'const char* text = "don\'t ' + declaration + '";',
            "const auto text = '" + declaration + "';",
            "const auto text = '" + declaration,
            "const auto text = '" + declaration + "\\",
            "// continued comment \\\n" + declaration,
            "#define DECLARATION " + declaration,
            "#define DECLARATION \\\n" + declaration,
            "enum class Other { Operator, System, Agent };",
            "enum class ActorExtra { Operator, System, Agent };",
            "enum Actor { Operator, System, Agent };",
            "enum class Actor { Agent };",
            "enum class Actor { Operator, System, Agent, Extra };",
            "enum class Actor { Operator, System, agent };",
            'enum class Actor { Operator, System, "Agent" };',
            "Actor::Agent;",
            'const char* label = "Agent";',
        ):
            with self.subTest(text=text):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Model/Workspace/Provenance.h")], errors
                    )
                self.assertTrue(errors)

    def test_provenance_actor_enum_preserves_adjacent_process_rejections(self) -> None:
        declaration = "enum class Actor { Operator, System, Agent };"
        for suffix in (
            " // agent implementation",
            " // Task 13",
            " // reviewer finding",
            " // Close the backlog.",
            '\nconst char* label = "Agent";',
            '\nconst char* text = R"(agent implementation)";',
        ):
            with self.subTest(suffix=suffix):
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=declaration + suffix):
                    policy.check_process_narration(
                        [Path("Source/App/Model/Workspace/Provenance.h")], errors
                    )
                self.assertEqual(len(errors), 1)
                self.assertIn(":2:" if suffix.startswith("\n") else ":1:", errors[0])

    def test_provenance_actor_enum_rejects_logical_noncode(self) -> None:
        declaration = "enum class Actor { Operator, System, Agent };"
        for prefix in (
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
        ):
            with self.subTest(prefix=prefix):
                errors: list[str] = []
                text = prefix + declaration
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Model/Workspace/Provenance.h")], errors
                    )
                line = text.count("\n") + 1
                self.assertEqual(
                    errors,
                    [f"Source/App/Model/Workspace/Provenance.h:{line}: "
                     "implementation-history narration"],
                )

    def test_provenance_actor_enum_accepts_logical_code_in_header(self) -> None:
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=newline):
                text = newline.join((
                    "/* Header. */ #pragma once",
                    '#include "Other.h"',
                    "#include <string>",
                    "namespace lmx::app {",
                    "enum /* attribution */ class Actor { Operator, \\",
                    "System, /* software source */ Agent, };",
                    "}",
                ))
                errors: list[str] = []
                with mock.patch.object(policy, "read_text", return_value=text):
                    policy.check_process_narration(
                        [Path("Source/App/Model/Workspace/Provenance.h")], errors
                    )
                self.assertEqual(errors, [])

    def test_provenance_actor_enum_preserves_original_text_after_splicing(self) -> None:
        text = (
            "#pragma once\n"
            "enum class Actor { Operator, \\\n"
            "System, /* agent implementation */ Agent }; // Task 13\n"
            'const char* label = "Agent";\n'
            "// reviewer finding\n"
        )
        masked = policy.mask_provenance_actor_enumerator(text)
        self.assertEqual(masked, text.replace("Agent };", "      };"))
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value=text):
            policy.check_process_narration(
                [Path("Source/App/Model/Workspace/Provenance.h")], errors
            )
        self.assertEqual(
            sorted(errors),
            [f"Source/App/Model/Workspace/Provenance.h:{line}: implementation-history narration"
             for line in (3, 4, 5)],
        )

    def test_provenance_actor_enum_allowance_stays_in_exact_header(self) -> None:
        for path in (
            "Source/App/Model/Workspace/Other.h",
            "Source/App/Model/Workspace/Provenance.cpp",
            "Source/App/Other/Provenance.h",
            "Tests/App/Model/Workspace/AppProvenanceTests.cpp",
            "Source/App/Panels/Gallery/StyleGalleryPanel.cpp",
        ):
            with self.subTest(path=path):
                errors: list[str] = []
                with mock.patch.object(
                    policy, "read_text", return_value="enum class Actor { Operator, System, Agent };"
                ):
                    policy.check_process_narration([Path(path)], errors)
                self.assertEqual(len(errors), 1)

    def test_component_paths_are_not_process_roots(self):
        self.assertFalse(any(root.startswith("RojoRHI") for root in policy.PROCESS_ROOTS))


if __name__ == "__main__":
    unittest.main()
