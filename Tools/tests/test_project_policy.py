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
        path = Path("docs/specs/example.md")
        errors: list[str] = []
        with mock.patch.object(policy, "read_text", return_value="# Example\n\n**Status**: Maybe\n"):
            policy.check_markdown([path], errors)
        self.assertTrue(any("unsupported document status" in error for error in errors))

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


if __name__ == "__main__":
    unittest.main()
