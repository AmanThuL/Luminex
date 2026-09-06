#!/usr/bin/env python3
"""Synthetic assembler audits; no Metal device or real gate attestations are created."""

import copy
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import assemble_validation as assembly
import paired


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='submission-assembly-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ids = {field: hashlib.sha256(field.encode()).hexdigest()
                    for field in assembly.IDENTITIES}
        self.verify = ['evidence/main/validation.json']
        self.stress = ['evidence/stress/validation.json']
        self.checkpoint = 'evidence/checkpoint-a.log'
        self.capture_index = 'evidence/captures.json'
        self.freeze = 'evidence/measurement-freeze.md'
        self.case = 'n1024-t2-v50-b1'
        self.write_run(self.verify[0], paired.frozen_cases(), 256)
        self.write_run(self.stress[0], [assembly.cases()[self.case]], 900, suites=('E',))
        self.write_text(self.checkpoint,
                        'Metal API Validation Enabled\nFilters: "[checkpoint-a]"\n'
                        'All tests passed (87 assertions in 12 test cases)\n')
        entries = []
        for kind, case in (('empty', 'empty'), ('sparse', self.case),
                           ('dense', 'n1024-t2-v100-b1')):
            trace = f'evidence/{kind}.gputrace'
            notes = f'evidence/{kind}-review.md'
            self.write_text(trace + '/capture.bin', 'synthetic capture package')
            self.write_text(notes, 'Synthetic explicit review fixture.\n')
            entries.append(dict(kind=kind, case=case, suite='E', variant='gpu-args',
                                reviewer='Synthetic test fixture', reviewedAt='2026-09-06T10:00:00Z',
                                references=[trace, notes],
                                **{field: True for field in assembly.CAPTURE_FACTS}))
        self.write_json(self.capture_index,
                        dict(schemaVersion=1, **self.ids, environment={'validation': True},
                             captures=entries))
        self.freeze_record = dict(schemaVersion=1, **self.ids, status='frozen',
                                  reviewer='Synthetic test fixture',
                                  reviewedAt='2026-09-06T10:00:00+08:00',
                                  references=['evidence/protocol.txt'],
                                  **{field: True for field in assembly.FREEZE_FACTS})
        self.write_text('evidence/protocol.txt', 'Synthetic freeze-support artifact.\n')
        self.write_freeze()

    def write_text(self, ref, text):
        path = self.root / ref
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def write_json(self, ref, data):
        self.write_text(ref, json.dumps(data))

    def read(self, ref):
        return json.loads((self.root / ref).read_text(encoding='utf-8'))

    def write_freeze(self):
        self.write_text(self.freeze, '# Measurement freeze\n\n```json\n' +
                        json.dumps(self.freeze_record) + '\n```\n')

    def write_run(self, ref, cases, frames, suites=('S', 'E')):
        rows = []
        for case in cases:
            manifest_ref = (Path(ref).parent / (case['id'] + '-manifest.json')).as_posix()
            self.write_json(manifest_ref, dict(schemaVersion=1, valid=True, **case))
            digest = paired.digest(self.root / manifest_ref)
            for suite in suites:
                for mode in assembly.ORDINARY:
                    rows.append(dict(case=case['id'], suite=suite, variant=mode, frames=frames,
                                     manifestHash=digest, reference=True, scoredReplay=True,
                                     retirement=True))
        self.write_json(ref, dict(schemaVersion=1, **self.ids,
                                 environment={'validation': True, 'capture': False}, results=rows))

    def assemble(self):
        return assembly.assemble(self.root, self.verify, self.stress, self.checkpoint,
                                 self.capture_index, self.freeze)

    def reject(self, message=None):
        if message:
            with self.assertRaisesRegex((ValueError, OSError), message):
                self.assemble()
        else:
            with self.assertRaises((ValueError, OSError)):
                self.assemble()
        self.assertFalse((self.root / 'validation.json').exists())

    def test_success_is_accepted_by_current_paired_gates(self):
        destination = self.assemble()
        self.assertEqual(destination, self.root / 'validation.json')
        result = paired.load(destination)
        self.assertEqual(len(result['results']), 160)
        self.assertTrue(result['environment']['validation'])
        self.assertEqual(result['gates']['lifetimeStress']['frames'], 900)
        self.assertEqual(result['gates']['checkpointA']['testCases'], 12)
        for source in result['sources']:
            self.assertFalse(Path(source['path']).is_absolute())
            if source['kind'] == 'file':
                self.assertEqual(source['sha256'], paired.digest(self.root / source['path']))
        manifests = {row['case']: row['manifestHash'] for row in result['results']}
        gates = paired.gates_for(self.root, self.ids, manifests, paired.frozen_cases(),
                                 dict(modes=list(assembly.ORDINARY)))
        self.assertTrue(gates)
        self.assertTrue(all(gate['status'] == 'verified' for gate in gates.values()), gates)

    def test_disjoint_scoped_main_and_stress_bundles(self):
        self.verify = ['evidence/scoped-a/validation.json', 'evidence/scoped-b/validation.json']
        matrix = paired.frozen_cases()
        self.write_run(self.verify[0], matrix[:10], 256)
        self.write_run(self.verify[1], matrix[10:], 256)
        self.stress = []
        for mode in assembly.ORDINARY:
            ref = f'evidence/stress-{mode}/validation.json'
            self.write_run(ref, [assembly.cases()[self.case]], 901, suites=('E',))
            data = self.read(ref)
            data['results'] = [row for row in data['results'] if row['variant'] == mode]
            self.write_json(ref, data)
            self.stress.append(ref)
        self.assemble()

    def test_driver_import_retains_all_sources_and_freezes_their_contents(self):
        source = self.assemble()
        destination = self.root / 'fresh-collection'
        destination.mkdir()
        paired.import_validation(source, destination)
        data = paired.load(destination / 'validation.json')
        for item in data['sources']:
            copied = destination / item['path']
            self.assertTrue(copied.exists(), item['path'])
            if item['kind'] == 'file':
                self.assertEqual(paired.digest(copied), item['sha256'])
        digest, artifacts = paired.validation_inventory(destination)
        frozen = dict(validationHash=digest, validationArtifacts=artifacts)
        paired.check_validation_freeze(destination, frozen)
        manifest = next(destination.glob('evidence/main/*-manifest.json'))
        manifest.write_text(manifest.read_text() + '\n')
        with self.assertRaisesRegex(ValueError, 'evidence changed'):
            paired.check_validation_freeze(destination, frozen)

    def test_tail_is_a_valid_changing_visibility_stress_fixture(self):
        self.write_run(self.stress[0], [assembly.cases()['tail']], 900, suites=('E',))
        self.assemble()

    def test_exclusive_output_preserves_existing_content(self):
        self.write_text('validation.json', 'original content')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.assemble()
        self.assertEqual((self.root / 'validation.json').read_text(), 'original content')

    def test_output_symlink_is_not_followed(self):
        target = self.root / 'absent-output'
        (self.root / 'validation.json').symlink_to(target)
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.assemble()
        self.assertFalse(target.exists())

    def test_complete_main_scope_is_required(self):
        original = self.read(self.verify[0])
        for mutation in ('missing', 'duplicate', 'unknown-mode', 'unknown-case', 'unknown-suite'):
            with self.subTest(mutation=mutation):
                data = copy.deepcopy(original)
                if mutation == 'missing':
                    data['results'].pop()
                elif mutation == 'duplicate':
                    data['results'].append(data['results'][0].copy())
                else:
                    field = {'unknown-mode': 'variant', 'unknown-case': 'case',
                             'unknown-suite': 'suite'}[mutation]
                    data['results'][0][field] = 'unknown'
                self.write_json(self.verify[0], data)
                self.reject()

    def test_frames_must_be_integral_and_complete(self):
        for ref, minimum in ((self.verify[0], 256), (self.stress[0], 900)):
            original = self.read(ref)
            for value in (None, True, False, minimum - 1, 0, -1, float(minimum), '900', 2**32):
                with self.subTest(source=ref, frames=value):
                    data = copy.deepcopy(original)
                    data['results'][0]['frames'] = value
                    self.write_json(ref, data)
                    self.reject('frames')
            self.write_json(ref, original)

    def test_each_verification_fact_must_be_true(self):
        original = self.read(self.verify[0])
        for field in ('reference', 'scoredReplay', 'retirement'):
            for value in (None, False, 1, 'true'):
                with self.subTest(field=field, value=value):
                    data = copy.deepcopy(original)
                    data['results'][0][field] = value
                    self.write_json(self.verify[0], data)
                    self.reject('failed verification')

    def test_all_bundles_need_a_true_validation_environment(self):
        for ref in (self.verify[0], self.stress[0], self.capture_index):
            original = self.read(ref)
            for value in (False, 1, 'true', None):
                with self.subTest(ref=ref, value=value):
                    data = copy.deepcopy(original)
                    data['environment']['validation'] = value
                    self.write_json(ref, data)
                    self.reject('environment.validation')
            self.write_json(ref, original)

    def test_mismatched_shader_executable_and_protocol_hashes(self):
        for ref in (self.stress[0], self.capture_index):
            original = self.read(ref)
            for field in assembly.IDENTITIES:
                with self.subTest(ref=ref, field=field):
                    data = copy.deepcopy(original)
                    data[field] = 'a' * 64
                    self.write_json(ref, data)
                    self.reject('identity mismatch')
            self.write_json(ref, original)
        original = self.freeze_record.copy()
        for field in assembly.IDENTITIES:
            self.freeze_record = dict(original, **{field: 'b' * 64})
            self.write_freeze()
            self.reject('identity mismatch')

    def test_scoped_verification_identities_must_match(self):
        extra = 'evidence/second/validation.json'
        self.write_run(extra, paired.frozen_cases()[10:], 256)
        first = self.read(self.verify[0])
        first['results'] = first['results'][:80]
        self.write_json(self.verify[0], first)
        second = self.read(extra)
        second['shaderHash'] = 'c' * 64
        self.write_json(extra, second)
        self.verify.append(extra)
        self.reject('identity mismatch')

    def test_invalid_hash_and_json_are_rejected(self):
        original = self.read(self.verify[0])
        for value in ('', 'ab', 'A' * 64, None, 12):
            data = dict(original, shaderHash=value)
            self.write_json(self.verify[0], data)
            self.reject('shaderHash')
        self.write_text(self.verify[0], '{"schemaVersion":1,"schemaVersion":1}')
        self.reject('duplicate JSON key')
        self.write_text(self.verify[0], '{"schemaVersion":1,"bad":NaN}')
        self.reject('non-finite')

    def test_schema_boolean_is_rejected(self):
        data = self.read(self.verify[0])
        data['schemaVersion'] = True
        self.write_json(self.verify[0], data)
        self.reject('schema')

    def test_manifest_content_hash_and_parameters_are_checked(self):
        ref = f'evidence/main/{self.case}-manifest.json'
        manifest = self.read(ref)
        manifest['triangles'] = 32
        self.write_json(ref, manifest)
        self.reject('manifest content hash')
        run = self.read(self.verify[0])
        for row in run['results']:
            if row['case'] == self.case:
                row['manifestHash'] = paired.digest(self.root / ref)
        self.write_json(self.verify[0], run)
        self.reject('manifest differs')

    def test_stress_cannot_silently_use_another_manifest(self):
        ref = f'evidence/stress/{self.case}-manifest.json'
        manifest = self.read(ref)
        manifest['generatorVersion'] = 'different'
        self.write_json(ref, manifest)
        run = self.read(self.stress[0])
        for row in run['results']:
            row['manifestHash'] = paired.digest(self.root / ref)
        self.write_json(self.stress[0], run)
        self.reject('stress and main manifest')

    def test_stress_requires_all_modes_in_E_on_one_changing_case(self):
        original = self.read(self.stress[0])
        data = copy.deepcopy(original)
        data['results'].pop()
        self.write_json(self.stress[0], data)
        self.reject('every ordinary mode')
        data = copy.deepcopy(original)
        for row in data['results']:
            row['suite'] = 'S'
        self.write_json(self.stress[0], data)
        self.reject('every ordinary mode')
        self.write_run(self.stress[0], [assembly.cases()[self.case], assembly.cases()['tail']], 900)
        self.reject('one representative case')
        self.write_run(self.stress[0], [assembly.cases()['single']], 900)
        self.reject('changing visibility')

    def test_files_are_not_capture_review(self):
        original = self.read(self.capture_index)
        for explicit in (False, True):
            for field in assembly.CAPTURE_FACTS:
                for missing in (False, True):
                    with self.subTest(explicit=explicit, field=field, missing=missing):
                        data = copy.deepcopy(original)
                        if explicit:
                            data['status'] = 'verified'
                        if missing:
                            del data['captures'][0][field]
                        else:
                            data['captures'][0][field] = False
                        self.write_json(self.capture_index, data)
                        self.reject('review facts')

    def nonverified_capture(self, status='unavailable'):
        return dict(schemaVersion=1, **self.ids, environment={'validation': True},
                    status=status, reason='Capture exposes warmup input, not generated output.',
                    references=['evidence/sparse-review.md'])

    def test_nonverified_capture_preserves_other_gates_and_defers(self):
        for status in ('unavailable', 'unresolved'):
            with self.subTest(status=status):
                index = self.nonverified_capture(status)
                self.write_json(self.capture_index, index)
                destination = self.assemble()
                result = paired.load(destination)
                self.assertEqual(len(result['results']), 160)
                self.assertEqual(result['gates']['capture'], dict(
                    status=status, reason=index['reason'],
                    references=sorted([self.capture_index, *index['references']])))
                self.assertTrue(all(gate['status'] == 'verified'
                                    for name, gate in result['gates'].items()
                                    if name != 'capture'))
                sources = {item['path'] for item in result['sources']}
                self.assertTrue({self.capture_index, *index['references']} <= sources)
                manifests = {row['case']: row['manifestHash'] for row in result['results']}
                caps = dict(modes=list(assembly.ORDINARY), icb={'reason': 'Unresolved probe'})
                gates = paired.gates_for(self.root, self.ids, manifests,
                                         paired.frozen_cases(), caps)
                self.assertEqual(gates['capture']['status'], status)
                self.assertEqual(gates['validation']['status'], 'verified')
                decisions = paired.decide([], paired.frozen_cases(), caps, gates)
                self.assertTrue(all(item['decision'] == 'defer' for item in decisions))
                self.assertIn('required correctness/capture/lifetime/freeze gates or collection '
                              'incomplete', decisions[0]['reasons'])
                destination.unlink()

    def test_nonverified_capture_requires_reason_and_references(self):
        for field, values in (('reason', (None, '', '  ', False, 1)),
                              ('references', (None, [], 'evidence/sparse-review.md',
                                              ['evidence/sparse-review.md'] * 2))):
            for value in values:
                with self.subTest(field=field, value=value):
                    data = self.nonverified_capture()
                    data[field] = value
                    self.write_json(self.capture_index, data)
                    self.reject()
            data = self.nonverified_capture()
            del data[field]
            self.write_json(self.capture_index, data)
            self.reject()

    def test_nonverified_capture_requires_matching_identity_schema_and_validation(self):
        mutations = [('schemaVersion', True), ('environment', {'validation': False})]
        mutations += [(field, 'a' * 64) for field in assembly.IDENTITIES]
        for field, value in mutations:
            with self.subTest(field=field):
                data = self.nonverified_capture()
                data[field] = value
                self.write_json(self.capture_index, data)
                self.reject()

    def test_unknown_capture_status_never_falls_back_to_positive_review(self):
        original = self.read(self.capture_index)
        for status in ('failed', 'unknown', '', None, True, 1, [], {}):
            with self.subTest(status=status):
                self.write_json(self.capture_index, dict(original, status=status))
                self.reject('capture status')

    def test_nonverified_capture_keeps_artifact_path_security(self):
        trace = self.root / 'evidence/sparse.gputrace'
        (trace / 'linked.bin').symlink_to(self.root / 'evidence/protocol.txt')
        (self.root / 'evidence/alias.md').symlink_to(self.root / 'evidence/protocol.txt')
        self.write_text('evidence/empty.txt', '')
        self.write_text('reports/review.md', 'Synthetic review')
        for ref in ('../outside', '/etc/passwd', 'evidence/../protocol.txt',
                    'evidence\\protocol.txt', 'file://capture', 'evidence//protocol.txt',
                    './evidence/protocol.txt', '', 'evidence/missing', 'evidence/empty.txt',
                    'evidence/alias.md', 'evidence/sparse.gputrace', 'reports/review.md'):
            with self.subTest(ref=ref):
                data = self.nonverified_capture()
                data['references'] = [ref]
                self.write_json(self.capture_index, data)
                self.reject()

    def test_nonverified_capture_does_not_bypass_freeze_gate(self):
        self.write_json(self.capture_index, self.nonverified_capture())
        self.freeze_record['reviewed'] = False
        self.write_freeze()
        self.reject('review facts')

    def test_capture_scope_and_reviewer_are_explicit(self):
        original = self.read(self.capture_index)
        for field, value in (('kind', 'dense'), ('case', self.case), ('reviewer', ''),
                             ('reviewedAt', '2026-09-06T10:00:00'), ('variant', 'gpu-icb')):
            with self.subTest(field=field):
                data = copy.deepcopy(original)
                data['captures'][0][field] = value
                self.write_json(self.capture_index, data)
                self.reject()

    def test_capture_requires_three_distinct_traces_and_notes(self):
        original = self.read(self.capture_index)
        for refs in (['evidence/empty-review.md'], ['evidence/empty.gputrace'],
                     ['evidence/sparse.gputrace', 'evidence/empty-review.md']):
            data = copy.deepcopy(original)
            data['captures'][0]['references'] = refs
            self.write_json(self.capture_index, data)
            self.reject()

    def test_review_references_must_exist_and_stay_inside_bundle(self):
        original = self.read(self.capture_index)
        for ref in ('../outside', '/etc/passwd', 'evidence/../checkpoint-a.log',
                    'evidence\\checkpoint-a.log', 'file://capture', 'evidence//empty.gputrace',
                    './evidence/empty.gputrace', '', 'evidence/missing.gputrace'):
            with self.subTest(ref=ref):
                data = copy.deepcopy(original)
                data['captures'][0]['references'][0] = ref
                self.write_json(self.capture_index, data)
                self.reject()

    def test_symlinks_and_special_files_are_rejected(self):
        trace = self.root / 'evidence/empty.gputrace'
        (trace / 'linked.bin').symlink_to(self.root / 'evidence/protocol.txt')
        self.reject('linked or special')
        (trace / 'linked.bin').unlink()
        alias = self.root / 'evidence/alias'
        alias.symlink_to(trace, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            assembly.artifact(self.root, 'evidence/alias/capture.bin')
        import os
        os.mkfifo(trace / 'pipe')
        self.reject('linked or special')

    def test_empty_artifacts_are_rejected(self):
        trace = self.root / 'evidence/empty.gputrace'
        (trace / 'capture.bin').unlink()
        self.reject('empty artifact directory')

    def test_checkpoint_log_requires_filter_validation_and_positive_success(self):
        original = (self.root / self.checkpoint).read_text()
        for text in ('passed.log', original.replace('[checkpoint-a]', '[unit]'),
                     original.replace('All tests passed', 'Some tests failed'),
                     original.replace('87 assertions', '0 assertions'),
                     original.replace('12 test cases', '0 test cases'),
                     original.replace('Metal API Validation Enabled', ''),
                     original + 'Validation Error: invalid binding\n',
                     original + 'SIGABRT\n', original + 'Filters: [unit]\n'):
            with self.subTest(text=text):
                self.write_text(self.checkpoint, text)
                self.reject('checkpoint')

    def test_freeze_record_must_be_explicit_complete_and_reviewed(self):
        original = self.freeze_record.copy()
        for field in assembly.FREEZE_FACTS:
            self.freeze_record = dict(original, **{field: False})
            self.write_freeze()
            self.reject('review facts')
        self.freeze_record = dict(original, status='planned')
        self.write_freeze()
        self.reject('not frozen')
        self.write_text(self.freeze, '# We intend to freeze later.\n')
        self.reject('fenced json')

    def test_collection_executable_must_match_if_collection_exists(self):
        self.write_json('collection.json', {'benchHash': 'a' * 64})
        self.reject('collection executable')
        self.write_json('collection.json', {'benchHash': self.ids['executableHash']})
        self.assemble()

    def test_already_frozen_collection_cannot_gain_posthoc_evidence(self):
        self.write_json('collection.json', dict(benchHash=self.ids['executableHash'],
                                                validationHash=None, validationArtifacts={}))
        self.reject('already froze validation')

    def test_import_references_cannot_collide_with_driver_outputs(self):
        self.write_text('reports/review.md', 'Synthetic review')
        self.freeze_record['references'] = ['reports/review.md']
        self.write_freeze()
        self.reject('collides with paired driver')

    def test_failure_marker_and_partial_progress_are_not_success(self):
        self.write_json('evidence/main/failure.json', {'error': 'synthetic failure'})
        self.reject('failure record')
        (self.root / 'evidence/main/failure.json').unlink()
        self.write_json('evidence/main/progress.json', self.read(self.verify[0]))
        self.verify = ['evidence/main/progress.json']
        self.reject('final CLI validation')

    def test_missing_inputs_never_emit_validation(self):
        for attribute in ('checkpoint', 'capture_index', 'freeze'):
            original = getattr(self, attribute)
            setattr(self, attribute, 'evidence/absent')
            self.reject()
            setattr(self, attribute, original)

    def test_cli_success_and_failure_exit_codes(self):
        argv = ['--bundle', str(self.root), '--verify', self.verify[0],
                '--stress', self.stress[0], '--checkpoint', self.checkpoint,
                '--capture-index', self.capture_index, '--freeze', self.freeze]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(assembly.main(argv), 0)
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(assembly.main(argv), 1)
        self.assertIn('already exists', errors.getvalue())

    def test_invalid_unicode_cannot_leave_a_partial_validation_file(self):
        self.freeze_record['reviewer'] = '\ud800'
        self.write_freeze()
        self.reject()


if __name__ == '__main__':
    unittest.main()
