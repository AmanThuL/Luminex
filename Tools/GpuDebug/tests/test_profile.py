"""Tests for profile.py components that do not need a real xctrace recording.

These pin the multi-pass encoder note and subprocess timeouts. The record/export happy path needs
real Xcode and a display and is covered by manual profiling verification.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import io
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

# Shadows the stdlib "profile" module (the pure-Python profiler) within this process. Harmless:
# sys.path.insert(0, ...) above makes `import profile` resolve to Tools/GpuDebug/profile.py first,
# and nothing else in this test suite needs the real stdlib one.
import profile as profile_cli  # noqa: E402

_ONE_ENCODER = {"lmx.pass.scene": {"count": 1, "totalMs": 1.0, "meanMs": 1.0, "p50Ms": 1.0,
                                    "p95Ms": 1.0, "maxMs": 1.0}}


# --- _build_timings ---------------------------------------------------------------------------

class BuildTimingsTests(unittest.TestCase):
    def test_encoders_note_names_distinct_render_passes(self):
        timings = profile_cli._build_timings(pathlib.Path("/build/App"), "sponza", 120,
                                             _ONE_ENCODER, [])

        self.assertIn("encodersNote", timings)
        note = timings["encodersNote"].lower()
        self.assertIn("lmx.pass.shadow", note)
        self.assertIn("lmx.pass.scene", note)
        self.assertIn("lmx.pass.ui", note)

    def test_scene_key_records_the_profiled_scene(self):
        timings = profile_cli._build_timings(pathlib.Path("/build/App"), "sponza", 120,
                                             _ONE_ENCODER, [])

        self.assertEqual(timings["scene"], "sponza")
        self.assertNotIn("sceneRequested", timings)
        self.assertNotIn("sceneRequestedNote", timings)

    def test_frame_total_is_none_when_no_lmx_encoders_matched(self):
        timings = profile_cli._build_timings(pathlib.Path("/build/App"), "sponza", 120, {},
                                             [])

        self.assertIsNone(timings["frameTotalMsApprox"])

    def test_frame_total_divides_by_the_requested_frame_count(self):
        timings = profile_cli._build_timings(pathlib.Path("/build/App"), "sponza", 4,
                                             _ONE_ENCODER, [])

        self.assertEqual(timings["frameTotalMsApprox"], 0.25)


# --- subprocess timeouts ----------------------------------------------------------------------

class TimeoutTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        # main() only checks App.is_file(); it never has to be executable for these tests, since
        # subprocess.run itself is mocked below and never actually runs it.
        self.app_path = pathlib.Path(self._tmp.name) / "App"
        self.app_path.write_bytes(b"")
        self.out_dir = pathlib.Path(self._tmp.name) / "out"

    def _argv(self):
        return ["--app", str(self.app_path), "--frames", "5", "--out", str(self.out_dir)]

    def test_record_timeout_exits_3_and_names_the_step(self):
        timeout = subprocess.TimeoutExpired(cmd=["xcrun", "xctrace", "record"], timeout=600)
        with mock.patch.object(profile_cli.shutil, "which", return_value="/usr/bin/xcrun"), \
             mock.patch.object(profile_cli.subprocess, "run", side_effect=timeout), \
             mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            code = profile_cli.main(self._argv())

        self.assertEqual(code, 3)
        self.assertIn("xctrace record", stderr.getvalue())
        self.assertIn("timed out", stderr.getvalue())

    def test_export_toc_timeout_exits_3_and_names_the_step(self):
        # First subprocess.run call (inside _record) succeeds; the second (inside _toc_schemas,
        # the "export --toc" step) times out -- pins that the message names the RIGHT step, not
        # just "some xctrace call failed".
        record_ok = subprocess.CompletedProcess(args=["xcrun"], returncode=0, stdout="", stderr="")
        timeout = subprocess.TimeoutExpired(cmd=["xcrun", "xctrace", "export", "--toc"],
                                            timeout=120)
        with mock.patch.object(profile_cli.shutil, "which", return_value="/usr/bin/xcrun"), \
             mock.patch.object(profile_cli.subprocess, "run",
                               side_effect=[record_ok, timeout]), \
             mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            code = profile_cli.main(self._argv())

        self.assertEqual(code, 3)
        self.assertIn("xctrace export --toc", stderr.getvalue())
        self.assertIn("timed out", stderr.getvalue())

    def test_run_wraps_timeout_expired_in_xctrace_timeout_error(self):
        with mock.patch.object(profile_cli.subprocess, "run",
                               side_effect=subprocess.TimeoutExpired(cmd=["x"], timeout=1)):
            with self.assertRaises(profile_cli.XctraceTimeoutError) as ctx:
                profile_cli._run(["x"], step="a probe step", timeout=1)
        self.assertIn("a probe step", str(ctx.exception))
        self.assertIn("timed out", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
