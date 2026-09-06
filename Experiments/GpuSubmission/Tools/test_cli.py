#!/usr/bin/env python3
"""Exercise the built CLI's non-GPU contract and failure-path safety from another CWD."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

BENCH = None


class CliTests(unittest.TestCase):
    def invoke(self, *args, env=None):
        return subprocess.run([str(BENCH), *args], cwd="/tmp", env=env,
                              text=True, capture_output=True, timeout=60)

    def test_model_selftest(self):
        result = self.invoke("--selftest")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("selftest passed", result.stdout)

    def test_matrix_has_only_twenty_scored_cases(self):
        result = self.invoke("--list-cases")
        self.assertEqual(result.returncode, 0, result.stderr)
        cases = json.loads(result.stdout)
        self.assertEqual(len(cases), 20)
        self.assertEqual(len({case["id"] for case in cases}), 20)
        self.assertEqual(sum(case["bins"] == 1 for case in cases), 18)
        self.assertTrue(all(case["count"] > 0 for case in cases))
        self.assertEqual(result.stdout, self.invoke("--list-cases").stdout)

    def test_diagnostic_shader_validation_environment(self):
        values = {
            "MTL_SHADER_VALIDATION_DEFAULT_STATE": "none",
            "MTL_SHADER_VALIDATION_ENABLE_PIPELINES": 'raster, prepare "quoted"\\label\n\t',
            "MTL_SHADER_VALIDATION_DISABLE_PIPELINES": " prepare,uid-123 ",
            "MTL_SHADER_VALIDATION_REPORT_TO_STDERR": "1",
            "MTL_SHADER_VALIDATION_FAIL_MODE": "allow",
            "MTL_SHADER_VALIDATION_DUMP_PIPELINES": "0",
        }
        env = {key: value for key, value in os.environ.items() if key not in values}
        env["MTL_SHADER_VALIDATION_UNLISTED_TEST"] = "must-not-leak-metal"
        env["LMX_PRIVATE_TEST"] = "must-not-leak-private"
        scored_environment = None
        for label, overrides in [("unset", {}), ("empty", dict.fromkeys(values, "")),
                                 ("selected", values)]:
            with self.subTest(label=label):
                result = self.invoke("--selftest", env=dict(env, **overrides))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("selftest passed", result.stdout)
                metadata = json.loads(result.stdout.splitlines()[0])
                self.assertEqual(metadata["diagnosticShaderValidationEnvironment"],
                                 {key: overrides.get(key) for key in values})
                if scored_environment is None:
                    scored_environment = metadata["environment"]
                self.assertEqual(metadata["environment"], scored_environment)
                self.assertTrue(values.keys().isdisjoint(metadata["environment"]))
                for secret in ["must-not-leak-metal", "must-not-leak-private",
                               "MTL_SHADER_VALIDATION_UNLISTED_TEST", "LMX_PRIVATE_TEST"]:
                    self.assertNotIn(secret, result.stdout + result.stderr)

    def test_invalid_arguments_fail(self):
        for args in [[], ["--selftest", "--verify"], ["--measure", "--lane", "bogus"],
                     ["--verify", "--frames", "0"], ["--verify", "--frames", "-1"],
                     ["--verify", "--frames", "4294967296"], ["--verify", "--frames"],
                     ["--measure", "--order", "AA"], ["--selftest", "--bogus", "x"]]:
            with self.subTest(args=args):
                self.assertNotEqual(self.invoke(*args).returncode, 0)

    def test_existing_output_is_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            sentinel = Path(directory) / "keep.txt"
            sentinel.write_text("user data")
            result = self.invoke("--verify", "--output", directory)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("already exists", result.stderr)
            self.assertEqual(sentinel.read_text(), "user data")
            self.assertEqual(list(Path(directory).iterdir()), [sentinel])

    def test_measure_refuses_instrumentation_before_gpu_or_output(self):
        for variable in ["MTL_DEBUG_LAYER", "MTL_CAPTURE_ENABLED", "MTL_SHADER_VALIDATION"]:
            with self.subTest(variable=variable), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "new"
                env = dict(os.environ, **{variable: "1"})
                result = self.invoke("--measure", "--output", str(target), env=env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refuses validation/capture", result.stderr)
                self.assertFalse(target.exists())

    def test_capture_requires_single_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "new"
            result = self.invoke("--verify", "--output", str(target), "--capture",
                                 str(Path(directory) / "frame.gputrace"))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("one explicit case", result.stderr)
            self.assertFalse(target.exists())

    def test_diagnosis_rejects_unsafe_scope_before_gpu(self):
        selection = ["--case", "n16384-t32-v50-b1", "--suite", "E", "--variant", "gpu-args"]
        for extra in [[], selection + ["--frames", "901"],
                      selection + ["--warmup", "33"], selection + ["--lane", "gpu-span"],
                      selection + ["--capture", "/tmp/unused-diagnostic.gputrace"],
                      ["--case", "empty", "--suite", "E", "--variant", "gpu-icb"],
                      ["--case", "bogus", "--suite", "E", "--variant", "direct"]]:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "new"
                result = self.invoke("--diagnose", "--output", str(target), *extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(target.exists())

    def test_diagnosis_cannot_be_combined_with_measurement(self):
        result = self.invoke("--measure", "--diagnose")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one action", result.stderr)

    def test_diagnostic_dependency_cannot_enter_measurement(self):
        cases = [
            ("--measure", "gpu-args", "all", "diagnostic dependency requires --diagnose"),
            ("--verify", "gpu-args", "all", "diagnostic dependency requires --diagnose"),
            ("--measure", "gpu-args", "declared", "diagnostic dependency requires --diagnose"),
            ("--diagnose", "gpu-args", "bogus", "diagnostic dependency must be declared or all"),
            ("--diagnose", "gpu-args", "", "diagnostic dependency must be declared or all"),
            ("--diagnose", "direct", "all", "requires gpu-args"),
        ]
        for action, variant, dependency, message in cases:
            with self.subTest(action=action, dependency=dependency), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "new"
                result = self.invoke(action, "--case", "n16384-t32-v50-b1", "--suite", "S",
                                     "--variant", variant, "--frames", "1", "--output", str(target),
                                     "--diagnostic-dependency", dependency)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse(target.exists())


    def test_diagnostic_argument_source_is_isolated(self):
        cases = [
            ("--measure", "S", "gpu-args", "cpu", "declared", "requires --diagnose"),
            ("--verify", "S", "gpu-args", "gpu", "declared", "requires --diagnose"),
            ("--diagnose", "E", "gpu-args", "cpu", "declared", "requires suite S"),
            ("--diagnose", "S", "direct", "gpu", "declared", "requires gpu-args"),
            ("--diagnose", "S", "gpu-args", "cpu", "all", "requires suite S"),
            ("--diagnose", "S", "gpu-args", "", "declared", "must be gpu or cpu"),
            ("--diagnose", "S", "gpu-args", "bogus", "declared", "must be gpu or cpu"),
        ]
        for action, suite, variant, source, dependency, message in cases:
            with self.subTest(source=source, action=action), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "new"
                result = self.invoke(action, "--case", "n16384-t32-v50-b1", "--suite", suite,
                                     "--variant", variant, "--frames", "1", "--output", str(target),
                                     "--diagnostic-arguments", source,
                                     "--diagnostic-dependency", dependency)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse(target.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench", required=True, type=Path)
    args, rest = parser.parse_known_args()
    BENCH = args.bench.resolve(strict=True)
    unittest.main(argv=[__file__, *rest])
