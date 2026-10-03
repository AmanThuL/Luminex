"""Regression tests for the alternating exact-hash parity driver."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Screenshots"))
import parity_rounds


class ParityRoundsTests(unittest.TestCase):
    def exercise_side_references(self, mutate=None, change_during_capture=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            parent, candidate = root / "parent", root / "candidate"
            parent.write_bytes(b"parent")
            candidate.write_bytes(b"candidate")
            (root / "Shaders").mkdir()
            (root / "Shaders/test.metallib").write_bytes(b"shader")
            anchor = Path(parity_rounds.__file__).with_name("reference.json")
            base = json.loads(anchor.read_text())
            references = {}
            for side, digest in (("parent", "a"), ("candidate", "b")):
                payload = json.loads(json.dumps(base))
                payload["documents"] = dict.fromkeys(base["documents"], digest * 64)
                if side == "candidate" and mutate:
                    mutate(payload)
                references[side] = root / (side + "-reference.json")
                references[side].write_text(json.dumps(payload))
            seen = []

            def fake_run(command, **_):
                app = Path(command[command.index("--app") + 1])
                side = "parent" if app == parent else "candidate"
                reference = Path(command[command.index("--reference") + 1])
                self.assertEqual(reference, references[side])
                seen.append(side)
                output = Path(command[command.index("--output") + 1])
                output.mkdir()
                rows = [{"name": row["name"], "actualSha256": row["sha256"],
                         "expectedSha256": row["sha256"], "match": True, "returnCode": 0}
                        for row in base["images"]]
                report = {"complete": True, "allMatched": True, "images": rows,
                          "referenceSha256": parity_rounds.parity.sha256(reference),
                          "appSha256": parity_rounds.parity.sha256(app),
                          "shaderSha256": parity_rounds.parity.shader_hashes(app)}
                (output / "parity.json").write_text(json.dumps(report))
                if change_during_capture:
                    references["candidate"].write_text(references["candidate"].read_text() + " ")
                return mock.Mock(returncode=0, stdout="", stderr="")

            with mock.patch.object(parity_rounds.subprocess, "run", side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                result = parity_rounds.run(parent, candidate, 1, root / "output", anchor,
                                          parent_reference=references["parent"],
                                          candidate_reference=references["candidate"])
            return result, json.loads((root / "output/summary.json").read_text()), seen

    def test_separate_document_pins_keep_identical_image_baselines(self):
        result, summary, seen = self.exercise_side_references()
        self.assertTrue(result)
        self.assertEqual(seen, ["parent", "candidate"])
        self.assertNotEqual(summary["sideReferenceSha256"]["parent"],
                            summary["sideReferenceSha256"]["candidate"])

    def test_side_reference_cannot_change_image_hash_or_capture_controls(self):
        for mutation in (lambda data: data["images"][0].update(sha256="c" * 64),
                         lambda data: data["provenance"].update(device="another device")):
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "document pins"):
                self.exercise_side_references(mutate=mutation)

    def test_changed_side_reference_refuses_capture(self):
        result, summary, _ = self.exercise_side_references(change_during_capture=True)
        self.assertFalse(result)
        self.assertFalse(summary["complete"])
        self.assertEqual(summary["attempts"][0]["failure"], "reference changed during capture")

    def exercise_child_status(self, returncode, historical_match=False,
                              report_match=None, row_match=None):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            parent, candidate = root / "parent", root / "candidate"
            parent.write_bytes(b"parent")
            candidate.write_bytes(b"candidate")
            (root / "Shaders").mkdir()
            (root / "Shaders/test.metallib").write_bytes(b"shader")
            reference = Path(__file__).resolve().parents[1] / "Screenshots/reference.json"
            reference_rows = json.loads(reference.read_text())["images"]

            def fake_run(command, **_):
                app = Path(command[command.index("--app") + 1])
                output = Path(command[command.index("--output") + 1])
                output.mkdir(parents=True)
                rows = []
                for image in reference_rows:
                    actual = image["sha256"] if historical_match else "a" * 64
                    rows.append({"name": image["name"], "actualSha256": actual,
                                 "expectedSha256": image["sha256"], "returnCode": 0,
                                 "match": actual == image["sha256"] if row_match is None else row_match})
                (output / "parity.json").write_text(json.dumps({
                    "complete": True, "allMatched": historical_match if report_match is None else report_match,
                    "images": rows, "referenceSha256": parity_rounds.parity.sha256(reference),
                    "appSha256": parity_rounds.parity.sha256(app),
                    "shaderSha256": parity_rounds.parity.shader_hashes(app)}))
                return mock.Mock(returncode=returncode, stdout="child output", stderr="")

            with mock.patch.object(parity_rounds.subprocess, "run", side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                result = parity_rounds.run(parent, candidate, 1, root / "output", reference)
            summary = json.loads((root / "output/summary.json").read_text())
            return result, summary

    def test_parent_union_accepts_drift_but_rejects_unseen_candidate(self):
        rows = [{"name": "case", "actualSha256": "a" * 64, "returnCode": 0}]
        parent = [rows, [{**rows[0], "actualSha256": "b" * 64}]]
        candidate = [[{**rows[0], "actualSha256": "b" * 64}] for _ in range(2)]
        self.assertTrue(parity_rounds.compare_rounds(parent, candidate)["allMatched"])
        candidate[0][0]["actualSha256"] = "c" * 64
        result = parity_rounds.compare_rounds(parent, candidate)
        self.assertFalse(result["allMatched"])
        self.assertEqual(result["cases"]["case"]["unseenCandidateHashes"], ["c" * 64])

    def test_failed_or_incomplete_capture_is_never_parity(self):
        good = [{"name": "case", "actualSha256": "a" * 64, "returnCode": 0}]
        for bad in ([{**good[0], "returnCode": 1}], [{"name": "case", "returnCode": 0}], []):
            with self.subTest(bad=bad):
                self.assertFalse(parity_rounds.compare_rounds([good], [bad])["allMatched"])

    def test_run_alternates_binary_order_and_retains_every_report(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            parent, candidate = root / "parent", root / "candidate"
            parent.write_bytes(b"parent")
            candidate.write_bytes(b"candidate")
            shaders = root / "Shaders"
            shaders.mkdir()
            (shaders / "test.metallib").write_bytes(b"shader")
            reference = Path(__file__).resolve().parents[1] / "Screenshots/reference.json"
            reference_rows = json.loads(reference.read_text())["images"]
            calls = []

            def fake_run(command, **_):
                side = "parent" if command[command.index("--app") + 1] == str(parent.resolve()) else "candidate"
                output = Path(command[command.index("--output") + 1])
                output.mkdir(parents=True)
                rows = [{"name": row["name"], "actualSha256": "a" * 64,
                         "expectedSha256": row["sha256"], "returnCode": 0,
                         "match": False} for row in reference_rows]
                (output / "parity.json").write_text(json.dumps({
                    "complete": True, "allMatched": False, "images": rows,
                    "referenceSha256": parity_rounds.parity.sha256(reference),
                    "appSha256": parity_rounds.parity.sha256(parent if side == "parent" else candidate),
                    "shaderSha256": parity_rounds.parity.shader_hashes(parent if side == "parent" else candidate)}))
                calls.append(side)
                return mock.Mock(returncode=1, stdout="reference mismatches", stderr="")

            with mock.patch.object(parity_rounds.subprocess, "run", side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(parity_rounds.run(parent, candidate, 2, root / "output", reference))
            self.assertEqual(calls, ["parent", "candidate", "candidate", "parent"])
            summary = json.loads((root / "output/summary.json").read_text())
            self.assertTrue(summary["complete"])
            self.assertTrue(summary["allMatched"])
            self.assertEqual(len(summary["attempts"]), 4)

    def test_only_consistent_success_or_historical_mismatch_status_is_accepted(self):
        for status, historical_match in ((0, True), (1, False)):
            with self.subTest(status=status):
                result, summary = self.exercise_child_status(status, historical_match)
                self.assertTrue(result)
                self.assertTrue(summary["complete"])
                self.assertEqual(summary["attempts"][0]["referenceAllMatched"], historical_match)

    def test_crash_or_unexpected_status_fails_even_with_complete_report(self):
        for status in (-9, 2):
            with self.subTest(status=status):
                result, summary = self.exercise_child_status(status)
                self.assertFalse(result)
                self.assertFalse(summary["complete"])
                self.assertFalse(summary["allMatched"])

    def test_exit_report_or_row_disagreement_fails_closed(self):
        for status, historical_match, report_match, row_match in (
            (0, False, False, None), (1, True, True, None),
            (1, False, True, None), (1, False, False, True)):
            with self.subTest(status=status, historical_match=historical_match,
                              report_match=report_match, row_match=row_match):
                result, summary = self.exercise_child_status(status, historical_match,
                                                             report_match, row_match)
                self.assertFalse(result)
                self.assertFalse(summary["complete"])


if __name__ == "__main__":
    unittest.main()
