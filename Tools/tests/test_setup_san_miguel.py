from __future__ import annotations

import hashlib
import importlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


with patch.object(sys, "path", [str(Path(__file__).resolve().parents[1]), *sys.path]):
    setup = importlib.import_module("setup_san_miguel")


class SanMiguelSetupTests(unittest.TestCase):
    def test_corrupt_cached_range_is_discarded_and_retry_downloads_again(self):
        archive = b"the verified archive fixture"
        expected = hashlib.sha256(archive).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "scene.zip"
            chunks = destination.with_suffix(f".parts-{expected}")
            chunks.mkdir()
            (chunks / "000000000000").write_bytes(b"x" * len(archive))
            other_chunks = destination.with_suffix(".parts-another-version")
            other_chunks.mkdir()
            other_piece = other_chunks / "000000000000"
            other_piece.write_bytes(b"keep unrelated archive chunks")

            def fetch(command, **kwargs):
                self.assertEqual(command[command.index("--range") + 1],
                                 f"0-{len(archive) - 1}")
                Path(command[command.index("--output") + 1]).write_bytes(archive)

            with patch.object(setup.subprocess, "run", side_effect=fetch) as curl:
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    setup.fetch_archive("https://example.invalid/scene.zip", destination,
                                        len(archive), expected)
                curl.assert_not_called()
                self.assertFalse(chunks.exists())
                self.assertFalse(destination.with_suffix(f".part-{expected}").exists())
                self.assertFalse(destination.exists())
                self.assertTrue(other_piece.exists())

                setup.fetch_archive("https://example.invalid/scene.zip", destination,
                                    len(archive), expected)
                curl.assert_called_once()
                self.assertEqual(destination.read_bytes(), archive)
                self.assertFalse(chunks.exists())
                self.assertTrue(other_piece.exists())
                setup.fetch_archive("https://example.invalid/scene.zip", destination,
                                    len(archive), expected)
                curl.assert_called_once()


if __name__ == "__main__":
    unittest.main()
