#!/usr/bin/env python3
"""Small CPU-only regression fixtures for diagnostic record auditing."""

import copy
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import audit_diagnostics as audit


def context(version=3):
    data = dict(schemaVersion=1, format='lmx.submission.diagnostic', scored=False,
                protocol=audit.PROTOCOLS[version - 1], suite='S', variant='gpu-args',
                warmup=32, frameCount=900,
                case=dict(id='n16384-t32-v50-b1', count=16384, triangles=32,
                          bins=1, visibleFraction=0.5),
                environment=dict(osRelease='25.5.0', osVersion='Darwin test', machine='arm64',
                                 hostModel='Mac15,9', compiler='clang test', validation=False,
                                 capture=False, shaderValidation=False))
    data.update({key: 'a' * 64 for key in
                 ('manifestHash', 'shaderHash', 'executableHash', 'protocolHash')})
    if version >= 2:
        data['diagnosticDependency'] = 'declared'
    if version >= 3:
        data['diagnosticArguments'] = 'gpu'
        data['diagnosticShaderValidationEnvironment'] = {'MTL_SHADER_VALIDATION_DEFAULT_STATE': None}
    return data


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def bundle(self, start=None, status='retired'):
        start = context() if start is None else start
        outcome = copy.deepcopy(start)
        outcome['status'] = status
        if status == 'retired':
            outcome.update(verified=False, retiredFrames=start['frameCount'])
        else:
            outcome['error'] = 'command queue timeout'
        self.write('diagnostic-start.json', start)
        self.write('diagnostic.json', outcome)
        return outcome

    def write(self, name, data):
        (self.root / name).write_text(json.dumps(data))

    def cli(self, *dirs):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = audit.main([str(d) for d in dirs])
        return code, out.getvalue()

    def test_all_supported_protocols(self):
        for version in (1, 2, 3):
            with self.subTest(version=version):
                self.bundle(context(version))
                self.assertEqual(audit.audit_directory(self.root), 'retired')

    def test_context_mismatches(self):
        changes = [('shaderHash',), ('executableHash',), ('manifestHash',), ('protocolHash',),
                   ('environment', 'validation'), ('environment', 'shaderValidation'),
                   ('diagnosticShaderValidationEnvironment', 'MTL_SHADER_VALIDATION_DEFAULT_STATE'),
                   ('diagnosticArguments',), ('diagnosticDependency',), ('case', 'count'),
                   ('case', 'triangles'), ('case', 'bins'), ('case', 'visibleFraction'),
                   ('warmup',), ('frameCount',), ('suite',), ('variant',)]
        for keys in changes:
            with self.subTest(keys=keys):
                outcome = self.bundle()
                target = outcome
                for key in keys[:-1]:
                    target = target[key]
                target[keys[-1]] = 'changed'
                self.write('diagnostic.json', outcome)
                with self.assertRaisesRegex(ValueError, 'mismatch'):
                    audit.audit_directory(self.root)

    def test_missing_outcome(self):
        self.bundle()
        (self.root / 'diagnostic.json').unlink()
        code, text = self.cli(self.root)
        self.assertEqual(code, 1)
        self.assertIn('diagnostic.json', text)

    def test_verified_claim_rejected_for_both_statuses(self):
        for status in ('retired', 'failed'):
            for value in (True, 0, None, 'false'):
                with self.subTest(status=status, value=value):
                    outcome = self.bundle(status=status)
                    outcome['verified'] = value
                    self.write('diagnostic.json', outcome)
                    with self.assertRaisesRegex(ValueError, 'verified'):
                        audit.audit_directory(self.root)

    def test_retirement_contract(self):
        for change in ({'retiredFrames': 899}, {'retiredFrames': 900.0},
                       {'retiredFrames': True}, {'error': 'timeout'}):
            outcome = self.bundle()
            outcome.update(change)
            self.write('diagnostic.json', outcome)
            with self.assertRaises(ValueError):
                audit.audit_directory(self.root)
        outcome = self.bundle()
        del outcome['verified']
        self.write('diagnostic.json', outcome)
        with self.assertRaises(ValueError):
            audit.audit_directory(self.root)

    def test_failed_is_valid_diagnosis_not_gate_pass(self):
        self.bundle(status='failed')
        code, text = self.cli(self.root)
        self.assertEqual(code, 0)
        self.assertIn('0 retired, 1 failed, 0 invalid', text)
        self.assertIn('not gate passes', text)
        self.assertIn('no parity, performance, reliability', text)
        for error in ('', '  ', None, 12):
            outcome = self.bundle(status='failed')
            outcome['error'] = error
            self.write('diagnostic.json', outcome)
            with self.assertRaisesRegex(ValueError, 'nonempty error'):
                audit.audit_directory(self.root)

    def test_format_protocol_and_bounds(self):
        for change in ({'protocol': 'pipelined-feedback-v4'}, {'schemaVersion': True},
                       {'schemaVersion': 2}, {'format': 'measurement'}, {'scored': True},
                       {'scored': 0}, {'unscored': False}, {'frameCount': 901},
                       {'frameCount': 0}, {'warmup': 33}, {'shaderHash': 'missing'},
                       {'diagnosticArguments': 'unknown'}):
            with self.subTest(change=change):
                start = context()
                start.update(change)
                self.bundle(start)
                with self.assertRaises(ValueError):
                    audit.audit_directory(self.root)

    def test_result_file_forbidden(self):
        self.bundle()
        self.write('result.json', {})
        with self.assertRaisesRegex(ValueError, 'result.json'):
            audit.audit_directory(self.root)

    def test_full_context_including_future_flags(self):
        start = context()
        start['extraSettings'] = {'pacing': [False, 2]}
        outcome = self.bundle(start)
        self.assertEqual(audit.audit_directory(self.root), 'retired')
        outcome['extraSettings']['pacing'][0] = 0
        self.write('diagnostic.json', outcome)
        with self.assertRaisesRegex(ValueError, 'type mismatch'):
            audit.audit_directory(self.root)

    def test_malformed_duplicate_and_nonfinite_json(self):
        for raw in ('{', '[]', '{"status":1,"status":2}', '{"x":NaN}'):
            self.bundle()
            (self.root / 'diagnostic.json').write_text(raw)
            self.assertEqual(self.cli(self.root)[0], 1)

    def test_multiple_directories_and_duplicates(self):
        self.bundle()
        code, text = self.cli(self.root / 'missing', self.root)
        self.assertEqual(code, 1)
        self.assertIn('1 retired, 0 failed, 1 invalid', text)
        self.assertEqual(self.cli(self.root, self.root)[0], 1)

    def test_cpu_source_constraints(self):
        start = context()
        start['diagnosticArguments'] = 'cpu'
        self.bundle(start)
        self.assertEqual(audit.audit_directory(self.root), 'retired')
        start['suite'] = 'E'
        self.bundle(start)
        with self.assertRaisesRegex(ValueError, 'CPU argument source'):
            audit.audit_directory(self.root)


if __name__ == '__main__':
    unittest.main()
