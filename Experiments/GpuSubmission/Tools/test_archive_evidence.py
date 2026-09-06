#!/usr/bin/env python3
"""CPU-only archive tests using synthetic captures; original artifacts stay untouched."""

import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import archive_evidence as archive


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="submission-archive-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.bundle = self.root / "evidence"
        self.capture = self.bundle / "native.gputrace"
        self.capture.mkdir(parents=True)
        self.payload = b"native buffer\x00\xff"
        self.source = self.capture / "MTLBuffer-Y"
        self.source.write_bytes(self.payload)
        self.alias = self.capture / "MTLBuffer-X"
        self.output = self.root / "index.json"

    def run_archive(self, output=None):
        with mock.patch.object(archive.subprocess, "check_output",
                               side_effect=["test-revision\n", ""]), \
                contextlib.redirect_stdout(io.StringIO()):
            archive.main(["--bundle", str(self.bundle), "--output", str(output or self.output)])

    def reject(self, reason):
        # Alias resolution must not reopen any target, especially an outside target.
        original_open = os.open

        def checked_open(path, flags, *args, **kwargs):
            if Path(path).is_absolute():
                self.assertEqual(Path(path), self.bundle)
            else:
                self.assertIn("dir_fd", kwargs)
                self.assertTrue(flags & os.O_NOFOLLOW)
            return original_open(path, flags, *args, **kwargs)

        with mock.patch.object(archive.os, "open", side_effect=checked_open), \
                self.assertRaisesRegex(ValueError, reason):
            archive.inventory(self.bundle)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.run_archive()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.source.read_bytes(), self.payload)

    def test_safe_alias_records_target_and_content_without_changing_originals(self):
        self.alias.symlink_to("MTLBuffer-Y")
        self.run_archive()
        data = json.loads(self.output.read_text())
        self.assertEqual(data["schemaVersion"], 2)
        self.assertEqual(data["artifactCount"], 2)
        self.assertEqual(data["totalBytes"], 2 * len(self.payload))
        self.assertEqual(data["files"], [
            {"path": "native.gputrace/MTLBuffer-X", "linkTarget": "MTLBuffer-Y",
             "contentSHA256": hashlib.sha256(self.payload).hexdigest(),
             "bytes": len(self.payload)},
            {"path": "native.gputrace/MTLBuffer-Y", "bytes": len(self.payload),
             "sha256": hashlib.sha256(self.payload).hexdigest()},
        ])
        self.assertTrue(self.alias.is_symlink())
        self.assertEqual(os.readlink(self.alias), "MTLBuffer-Y")
        self.assertEqual(self.source.read_bytes(), self.payload)

    def test_safe_parent_relative_alias_and_chain(self):
        nested = self.capture / "nested"
        nested.mkdir()
        (nested / "alias").symlink_to("../MTLBuffer-Y")
        self.alias.symlink_to("nested/alias")
        rows = archive.inventory(self.bundle)
        aliases = [row for row in rows if "linkTarget" in row]
        self.assertEqual(len(aliases), 2)
        self.assertTrue(all(row["contentSHA256"] == hashlib.sha256(self.payload).hexdigest()
                            for row in aliases))

    def test_absolute_target_even_inside_bundle_is_rejected(self):
        self.alias.symlink_to(self.source)
        self.reject("absolute symlink")

    def test_relative_escape_is_rejected_without_opening_target(self):
        outside = self.root / "secret"
        outside.write_bytes(b"outside")
        self.alias.symlink_to("../../secret")
        self.reject("out-of-bundle")
        self.assertEqual(outside.read_bytes(), b"outside")

    def test_escape_and_reentry_is_rejected(self):
        self.alias.symlink_to("../../evidence/native.gputrace/MTLBuffer-Y")
        self.reject("out-of-bundle")

    def test_chained_escape_is_rejected(self):
        self.alias.symlink_to("nested-alias")
        (self.capture / "nested-alias").symlink_to("../../secret")
        self.reject("out-of-bundle")

    def test_broken_alias_is_rejected(self):
        self.alias.symlink_to("missing")
        self.reject("broken symlink")

    def test_regular_file_cannot_be_used_as_directory(self):
        for target in ("MTLBuffer-Y/", "MTLBuffer-Y/.", "MTLBuffer-Y/../MTLBuffer-Y"):
            with self.subTest(target=target):
                self.alias.symlink_to(target)
                self.reject("non-directory")
                self.alias.unlink()

    def test_directory_alias_is_rejected(self):
        self.alias.symlink_to(".")
        self.reject("directory symlink")

    def test_directory_alias_cannot_be_used_as_intermediate_component(self):
        self.alias.symlink_to("directory/MTLBuffer-Y")
        (self.capture / "directory").symlink_to(".")
        self.reject("directory symlink")

    def test_external_directory_alias_is_not_traversed(self):
        (self.root / "secret-directory").mkdir()
        self.alias.symlink_to("../../secret-directory")
        self.reject("out-of-bundle")

    def test_cycle_is_rejected(self):
        self.alias.symlink_to("MTLBuffer-X")
        self.reject("cyclic symlink")

    def test_special_file_is_rejected_without_opening_it(self):
        os.mkfifo(self.capture / "pipe")
        self.reject("special file")

    def test_output_inside_bundle_is_not_self_indexed(self):
        self.run_archive(self.bundle / "index.json")
        data = json.loads((self.bundle / "index.json").read_text())
        self.assertEqual([row["path"] for row in data["files"]],
                         ["native.gputrace/MTLBuffer-Y"])

    def test_output_collision_preserves_existing_file_or_symlink(self):
        self.output.write_text("original")
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.run_archive()
        self.assertEqual(self.output.read_text(), "original")
        self.output.unlink()
        for target in (self.root / "absent", self.source):
            with self.subTest(target=target):
                self.output.symlink_to(target)
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    self.run_archive()
                self.assertTrue(self.output.is_symlink())
                self.assertEqual(os.readlink(self.output), str(target))
                self.output.unlink()
        self.assertFalse((self.root / "absent").exists())
        self.assertEqual(self.source.read_bytes(), self.payload)

    def test_output_creation_race_is_exclusive(self):
        original_inventory = archive.inventory

        def collide(bundle):
            records = original_inventory(bundle)
            self.output.write_text("other writer")
            return records

        with mock.patch.object(archive, "inventory", side_effect=collide), \
                self.assertRaises(FileExistsError):
            self.run_archive()
        self.assertEqual(self.output.read_text(), "other writer")


if __name__ == "__main__":
    unittest.main()
