#!/usr/bin/env python3
"""Contract, statistics, adoption and subprocess failure tests; no GPU or third-party packages."""

import contextlib
import copy
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import paired as p


def capabilities(span='verified', icb=False):
    return dict(schemaVersion=1, modes=list(p.MODES if icb else p.MODES[:-1]), gpuSpan=span,
                icb=dict(status='verified' if icb else 'unsupported', reason='test fixture'))


def fixture(job=None):
    job = job or p.schedule(p.frozen_cases())[12]
    order = job['pair'].split(',')
    if job['order'] == 'BA':
        order.reverse()
    data = {key: job[key] for key in ('case', 'suite', 'lane', 'pair', 'order')}
    data.update(schemaVersion=1, warmup=p.WARMUP, frameCount=p.FRAMES,
                environment=dict(capture=False, shaderValidation=False, validation=False),
                manifestHash='f' * 64)
    data.update({field: 'a' * 64 for field in p.HASH_FIELDS})
    data['environmentHash'] = p.native_environment_hash(data['environment'])
    data['runs'] = [dict(
        variant=mode, throughput=1000 if mode == 'direct' else 1200,
        requestedBytes=1000, allocatedBytes=4096, residentBytes=None, device='test-device',
        setupMs=10.0, drainMs=0.4,
        gpuSpanStatus='verified' if job['lane'] == 'gpu-span' else 'unavailable: no markers',
        frames=[dict(frame=i, cpuWorkNs=100 if mode == 'direct' else 80, waitNs=10,
                     drawCalls=1024, copiedBytes=4096,
                     gpuSpanMs=1.0 if job['lane'] == 'gpu-span' else None,
                     preparationMs=None, rasterMs=None) for i in range(p.FRAMES)]) for mode in order]
    return job, data


def stat(median=20, low=18, high=22):
    return dict(median=median, low=low, high=high, pairs=12)


def decision_cells():
    cases = [case for case in p.frozen_cases() if case['triangles'] == 2
             and case['visibleFraction'] == 0.5 and case['bins'] == 1]
    rows = []
    for case in cases:
        for control in ('direct', 'batched'):
            for lane in ('headline', 'gpu-span'):
                metrics = (dict(cpuWorkNs=stat(), throughput=stat(2, -2, 6), gpuSpanMs=None)
                           if lane == 'headline' else
                           dict(cpuWorkNs=None, throughput=None, gpuSpanMs=stat(1, -1, 3)))
                rows.append(dict(case=case, suite='E', pair='gpu-args,' + control,
                                 lane=lane, status='valid', metrics=metrics, icbMemoryKnown=True))
    gates = {key: {'status': 'verified'} for key in p.GATES}
    return cases, rows, gates


class SchemaTests(unittest.TestCase):
    def assert_invalid(self, change, match=None, job=None):
        job, data = fixture(job)
        change(data)
        with self.assertRaisesRegex(p.Invalid, match or '.'):
            p.validate_pair(data, job)

    def test_frozen_matrix_and_schedule(self):
        cases = p.validate_cases(p.frozen_cases())
        self.assertEqual(len(cases), 20)
        jobs = p.schedule(cases)
        self.assertEqual(len(jobs), 20 * 2 * 2 * 6 * 12)
        self.assertEqual(len({j['id'] for j in jobs}), len(jobs))
        self.assertEqual([j['order'] for j in jobs[:4]], ['AB', 'BA', 'AB', 'BA'])
        for pair in ('gpu-args,batched', 'gpu-icb,batched'):
            self.assertEqual(sum(j['pair'] == pair for j in jobs), 20 * 2 * 2 * 12)
        with self.assertRaises(p.Invalid):
            p.validate_cases(cases[:-1])
        with self.assertRaises(p.Invalid):
            p.validate_cases({'cases': cases})

    def test_capabilities_and_unavailable(self):
        caps = p.validate_caps(capabilities('unavailable'))
        jobs = p.schedule(p.frozen_cases())
        span = next(j for j in jobs if j['lane'] == 'gpu-span')
        self.assertIn('unverified', p.unavailable(span, caps))
        icb = next(j for j in jobs if j['pair'].startswith('gpu-icb'))
        self.assertIn('gpu-icb', p.unavailable(icb, caps))
        caps['modes'].append('gpu-icb')
        with self.assertRaises(p.Invalid):
            p.validate_caps(caps)

    def test_ab_ba_identical_candidate_deltas(self):
        job, data = fixture()
        ab, device = p.validate_pair(data, job)
        job['order'] = data['order'] = 'BA'
        data['runs'].reverse()
        ba, _ = p.validate_pair(data, job)
        self.assertEqual(ab, ba)
        self.assertEqual(device, 'test-device')
        self.assertEqual(p.improvement(ba['gpu-args']['cpuWorkNs'],
                                       ba['direct']['cpuWorkNs'], 'cpuWorkNs'), 20)

    def test_missing_ids_frames_duplicates_and_tail(self):
        for change in (
            lambda d: d.pop('case'),
            lambda d: d['runs'][0]['frames'][0].pop('frame'),
            lambda d: d['runs'][0]['frames'].pop(),
            lambda d: d['runs'][0]['frames'][-1].update(frame=0),
            lambda d: d['runs'][0]['frames'][-1].update(frame=256),
            lambda d: d['runs'][0]['frames'][0].update(frame=False),
            lambda d: d['runs'][0]['frames'].__setitem__(0, None),
            lambda d: d['runs'].__setitem__(0, None),
        ):
            self.assert_invalid(change)

    def test_mismatched_case_lane_pair_suite(self):
        for field, value in (('case', p.frozen_cases()[1]), ('suite', 'E'),
                             ('lane', 'stages'), ('pair', 'batched,direct')):
            self.assert_invalid(lambda d: d.update({field: value}), 'mismatched')

    def test_hashes_memory_and_incomplete_pairs(self):
        for field in (*p.HASH_FIELDS, 'manifestHash'):
            self.assert_invalid(lambda d: d.pop(field), 'invalid')
            self.assert_invalid(lambda d: d.update({field: 'not-a-hash'}), 'invalid')
        self.assert_invalid(lambda d: d['runs'].pop(), 'incomplete pair')
        self.assert_invalid(lambda d: d['runs'][1].update(variant='gpu-args'), 'variants')
        self.assert_invalid(lambda d: d['runs'][1].update(device='other'), 'different devices')
        self.assert_invalid(lambda d: d['runs'][0].update(requestedBytes=p.MEMORY_LIMIT + 1), '256 MiB')
        self.assert_invalid(lambda d: d['runs'][0].pop('residentBytes'), 'memory')
        self.assert_invalid(lambda d: d['runs'][0].update(allocatedBytes=-1), 'allocatedBytes')

    def test_missing_nullable_metrics_and_marker_contamination(self):
        self.assert_invalid(lambda d: d['runs'][0]['frames'][0].pop('gpuSpanMs'), 'missing nullable')
        self.assert_invalid(lambda d: d['runs'][0]['frames'][0].update(gpuSpanMs=1), 'headline')
        span = next(j for j in p.schedule(p.frozen_cases()) if j['lane'] == 'gpu-span')
        self.assert_invalid(lambda d: d['runs'][0]['frames'][0].update(gpuSpanMs=None),
                            'missing or unverified', span)
        self.assert_invalid(lambda d: d['runs'][0]['frames'][0].update(preparationMs=1),
                            'stage markers', span)
        self.assert_invalid(lambda d: d['runs'][0].update(gpuSpanStatus='unavailable'),
                            'unverified', span)

    def test_nonfinite_negative_zero_and_bool(self):
        for value in (float('nan'), float('inf'), -1, 0, True, 10 ** 400):
            self.assert_invalid(lambda d: d['runs'][0].update(throughput=value))
        self.assert_invalid(lambda d: d['runs'][0]['frames'][0].update(cpuWorkNs=0))
        self.assert_invalid(lambda d: d['environment'].update(validation=True))
        for text in ('{"x":NaN}', '{"x":1e999}', '{"x":0,"x":1}'):
            with self.assertRaises(p.Invalid):
                p.loads(text)

    def test_environment_identity_is_checked(self):
        self.assert_invalid(lambda d: d['environment'].update(powerSource='AC'), 'content hash')
        self.assert_invalid(lambda d: d['environment'].pop('shaderValidation'), 'shader validation')
        self.assert_invalid(lambda d: d.update(environmentHash='e' * 64), 'content hash')
        environment = {'os': 'quote" slash\\ newline\n tab\t', 'validation': False}
        native = b'{"os":"quote\\" slash\\\\ newline\\u000a tab\\u0009","validation":false}'
        self.assertEqual(p.native_environment_hash(environment), p.hashlib.sha256(native).hexdigest())

    def test_setup_drain_contract(self):
        for field in ('setupMs', 'drainMs'):
            self.assert_invalid(lambda d: d['runs'][0].pop(field))
            self.assert_invalid(lambda d: d['runs'][0].update({field: -1}))

    def test_manifest_actual_bytes(self):
        job, data = fixture()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            p.save(root / 'manifest.json', {'schemaVersion': 1, 'valid': True, **job['case']})
            data['manifestHash'] = p.digest(root / 'manifest.json')
            p.validate_pair(data, job, root)
            data['manifestHash'] = '0' * 64
            with self.assertRaisesRegex(p.Invalid, 'content hash'):
                p.validate_pair(data, job, root)
            data['manifestHash'] = p.digest(root / 'manifest.json')
            manifest = {'schemaVersion': 1, 'valid': True, **p.frozen_cases()[1]}
            original_load = p.load
            with patch('paired.load', side_effect=lambda path: manifest if Path(path).name ==
                       'manifest.json' else original_load(path)):
                with self.assertRaisesRegex(p.Invalid, 'another case'):
                    p.validate_pair(data, job, root)


class StatisticsTests(unittest.TestCase):
    def test_known_duration_throughput_and_seed(self):
        self.assertEqual(p.improvement(80, 100, 'cpuWorkNs'), 20)
        self.assertEqual(p.improvement(120, 100, 'throughput'), 20)
        self.assertEqual(p.bootstrap((20,) * 12), dict(median=20, low=20, high=20, pairs=12))
        data = (-80, -20, -9, -5, 0, 2, 5, 10, 15, 20, 30, 100)
        a = p.bootstrap(data)
        p.bootstrap.cache_clear()
        self.assertEqual(p.bootstrap(data), a)
        self.assertLess(a['low'], 0)
        self.assertGreater(a['high'], 0)
        self.assertEqual(p.RESAMPLES, 10000)
        self.assertEqual(p.SEED, 0x4C4D5836)

    def test_no_zero_denominators_or_incomplete_bootstrap(self):
        with self.assertRaises(p.Invalid):
            p.improvement(1, 0, 'cpuWorkNs')
        with self.assertRaises(p.Invalid):
            p.bootstrap((20,) * 11)

    def test_run_median_before_pair_delta(self):
        job, data = fixture()
        # One extreme frame remains raw, but frames are not independent bootstrap repetitions.
        data['runs'][0]['frames'][0]['cpuWorkNs'] = 100000000
        reduced, _ = p.validate_pair(data, job)
        self.assertEqual(reduced['gpu-args']['cpuWorkNs'], 80)
        self.assertEqual(data['runs'][0]['frames'][0]['cpuWorkNs'], 100000000)


class DecisionTests(unittest.TestCase):
    def decision(self, mutate=None):
        cases, cells, gates = decision_cells()
        if mutate:
            mutate(cases, cells, gates)
        return p.decide(cells, cases, capabilities(), gates)[0]

    def test_adjacent_both_controls_same_metric_adopts(self):
        result = self.decision()
        self.assertEqual(result['decision'], 'adopt')
        self.assertEqual(len(result['regions']), 2)
        self.assertEqual(result['regions'][0]['winningMetrics'], ['cpuWorkNs'])

    def test_nonadjacent_wins_do_not_adopt(self):
        def mutate(cases, rows, gates):
            for row in rows:
                if row['case']['count'] == 16384 and row['lane'] == 'headline':
                    row['metrics']['cpuWorkNs'] = stat(0, -1, 1)
        self.assertEqual(self.decision(mutate)['decision'], 'retain')

    def test_both_controls_required(self):
        def mutate(cases, rows, gates):
            for row in rows:
                if row['pair'].endswith('batched') and row['lane'] == 'headline':
                    row['metrics']['cpuWorkNs'] = stat(0, -1, 1)
        self.assertEqual(self.decision(mutate)['decision'], 'retain')

    def test_same_winning_metric_across_controls_and_scales(self):
        for discriminator in ('control', 'scale'):
            def mutate(cases, rows, gates):
                for row in rows:
                    selected = (row['pair'].endswith('batched') if discriminator == 'control'
                                else row['case']['count'] == 16384)
                    if selected and row['lane'] == 'headline':
                        row['metrics']['cpuWorkNs'] = stat(0, -1, 1)
                        row['metrics']['throughput'] = stat()
            self.assertEqual(self.decision(mutate)['decision'], 'retain')

    def test_uncertain_regression_and_exact_loss_margin_retain(self):
        for low in (-15, -30):
            for lane, metric in (('headline', 'throughput'), ('gpu-span', 'gpuSpanMs')):
                def mutate(cases, rows, gates):
                    for row in rows:
                        if row['lane'] == lane:
                            row['metrics'][metric] = stat(-16, low, 10)
                self.assertEqual(self.decision(mutate)['decision'], 'retain')

    def test_missing_guards_or_gates_defer(self):
        def missing(cases, rows, gates):
            for row in rows:
                if row['lane'] == 'gpu-span':
                    row['metrics']['gpuSpanMs'] = None
        self.assertEqual(self.decision(missing)['decision'], 'defer')
        self.assertEqual(self.decision(lambda c, r, g: g.pop('capture'))['decision'], 'defer')
        cases, rows, gates = decision_cells()
        self.assertEqual(p.decide(rows, cases, capabilities(), gates, False)[0]['decision'], 'defer')

    def test_suite_s_is_not_adoption_evidence(self):
        def mutate(cases, rows, gates):
            for row in rows:
                row['suite'] = 'S'
        self.assertNotEqual(self.decision(mutate)['decision'], 'adopt')

    def test_unavailable_icb_never_adopts_and_opaque_memory_gates_supported_icb(self):
        cases, cells, gates = decision_cells()
        self.assertEqual(p.decide(cells, cases, capabilities(), gates)[1]['decision'], 'defer')
        for row in cells:
            row['pair'] = row['pair'].replace('gpu-args', 'gpu-icb')
            row['icbMemoryKnown'] = False
        self.assertEqual(p.decide(cells, cases, capabilities(icb=True), gates)[1]['decision'], 'defer')
        for row in cells:
            row['icbMemoryKnown'] = True
        self.assertEqual(p.decide(cells, cases, capabilities(icb=True), gates)[1]['decision'], 'adopt')


class ProcessTests(unittest.TestCase):
    def test_failed_process_logs_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'attempt'
            receipt = p.run_command([sys.executable, '-c',
                                     "import sys; print('raw failure'); sys.exit(7)"], directory)
            self.assertEqual(receipt['exitCode'], 7)
            self.assertIn('raw failure', (directory / 'stdout.log').read_text())
            self.assertEqual(p.load(directory / 'receipt.json')['exitCode'], 7)
            with self.assertRaises(FileExistsError):
                p.run_command([sys.executable, '-c', 'pass'], directory)

    def test_timeout_is_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch('paired.subprocess.run', side_effect=subprocess.TimeoutExpired('test', 1)):
                receipt = p.run_command(['unused'], directory)
            self.assertIsNone(receipt['exitCode'])
            self.assertIn('timed out', receipt['error'])
            self.assertTrue((directory / 'receipt.json').exists())

    def test_telemetry_actual_values_and_unavailable(self):
        response = subprocess.CompletedProcess([], 0, 'Now drawing from AC Power\n', '')
        with patch('paired.subprocess.run', return_value=response) as run:
            observed = p.telemetry()
        self.assertEqual(run.call_args_list[0].args[0], ['pmset', '-g', 'batt'])
        self.assertEqual(run.call_args_list[1].args[0], ['pmset', '-g', 'therm'])
        self.assertEqual(observed['power']['value'], response.stdout)
        self.assertIsNone(observed['power']['error'])
        with patch('paired.subprocess.run', side_effect=FileNotFoundError('no pmset')):
            observed = p.telemetry()
        self.assertIsNone(observed['thermal']['value'])
        self.assertIn('no pmset', observed['thermal']['error'])
        with patch('paired.subprocess.run', return_value=subprocess.CompletedProcess([], 1, '', 'denied')):
            observed = p.telemetry()
        self.assertIsNone(observed['power']['value'])
        self.assertEqual(observed['power']['error'], 'denied')

    def test_output_collision_and_source_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            p.reserve(root)
            p.save(root / 'retained.json', {})
            with self.assertRaisesRegex(p.Invalid, 'nonempty'):
                p.reserve(root)
        with self.assertRaisesRegex(p.Invalid, 'outside the source tree'):
            p.reserve(Path(p.__file__).parent / 'test-must-not-exist')

    def test_resume_never_retries_failure_and_keeps_session_telemetry(self):
        calls = []

        def command(args, logs, timeout):
            self.assertEqual(args[0], str(Path(sys.executable).resolve()))
            self.assertIn(args[1], ('--list-cases', '--capabilities', '--measure'))
            logs.mkdir(parents=True)
            if '--list-cases' in args:
                p.save(logs / 'stdout.log', p.frozen_cases())
            elif '--capabilities' in args:
                p.save(logs / 'stdout.log', capabilities('unavailable'))
            else:
                calls.append(args)
            receipt = dict(exitCode=7 if '--measure' in args else 0, error=None)
            p.save(logs / 'receipt.json', receipt)
            return receipt

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'bundle'
            args = p.argparse.Namespace(bench=sys.executable, output=str(root), validation=None,
                                       resume=False, max_pairs=1, timeout=10)
            with patch('paired.run_command', side_effect=command), \
                    patch('paired.telemetry', side_effect=[{'sample': i} for i in range(4)]), \
                    patch('paired.flags', return_value={k: None for k in p.FLAGS}), \
                    patch('paired.summarize', return_value=2), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(p.collect(args), 2)
                receipt_path = root / 'pairs' / 'p00000' / 'process' / 'receipt.json'
                retained = receipt_path.read_bytes()
                args.resume = True
                self.assertEqual(p.collect(args), 2)
            self.assertEqual(len(calls), 2)
            self.assertIn('p00000', calls[0][-1])
            self.assertIn('p00001', calls[1][-1])
            self.assertEqual(receipt_path.read_bytes(), retained)
            initial = p.load(root / 'environment.json')
            resumed = p.load(root / 'sessions' / 's0001' / 'environment.json')
            self.assertEqual(initial['pre'], {'sample': 0})
            self.assertEqual(initial['post'], {'sample': 1})
            self.assertEqual(resumed['pre'], {'sample': 2})
            self.assertEqual(resumed['post'], {'sample': 3})
            self.assertFalse(initial['telemetryAffectsEnvironmentHash'])


class ReportTests(unittest.TestCase):
    def make_bundle(self, root, cases=None, bad=None):
        cases = cases or p.frozen_cases()[:1]
        caps = capabilities('unavailable')
        p.save(root / 'cases.json', cases)
        p.save(root / 'capabilities.json', caps)
        config = dict(schemaVersion=1, benchHash='a' * 64, seed=p.SEED, pairs=12,
                      warmup=32, frames=256, resamples=10000,
                      driverHash=p.digest(p.__file__), schemaHash=p.digest(Path(p.__file__).with_name('schema.md')),
                      casesHash=p.digest(root / 'cases.json'), capabilitiesHash=p.digest(root / 'capabilities.json'))
        p.save(root / 'collection.json', config)
        jobs = p.schedule(cases)
        for job in jobs:
            directory = root / 'pairs' / job['id']
            directory.mkdir(parents=True)
            p.save(directory / 'job.json', job)
            if p.unavailable(job, caps):
                p.save(directory / 'skip.json', {'status': 'unavailable', 'reason': p.unavailable(job, caps)})
                continue
            process = directory / 'process'
            process.mkdir()
            p.save(process / 'receipt.json', dict(exitCode=0, error=None))
            output = directory / 'output'
            output.mkdir()
            _, data = fixture(job)
            p.save(output / 'manifest.json', {'schemaVersion': 1, 'valid': True, **job['case']})
            data['manifestHash'] = p.digest(output / 'manifest.json')
            if bad:
                bad(job, data)
            p.save(output / 'result.json', data)
        return cases, caps

    def report(self, root, name):
        destination = root.parent / name
        with patch('paired.validate_cases', side_effect=lambda cases: cases), contextlib.redirect_stdout(io.StringIO()):
            code = p.summarize(root, p.reserve(destination))
        return code, destination, p.load(destination / 'summary.json')

    def test_reproducible_report_and_explicit_nulls(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'bundle'
            root.mkdir()
            self.make_bundle(root)
            code, a, result = self.report(root, 'report-a')
            code_b, b, _ = self.report(root, 'report-b')
            self.assertEqual(code, 0)
            self.assertEqual(code_b, 0)
            for name in ('summary.json', 'summary.jsonl', 'summary.md', 'summary.csv',
                         'pairs.jsonl', 'frames.jsonl', 'runs.csv'):
                self.assertEqual((a / name).read_bytes(), (b / name).read_bytes(), name)
            headline = next(c for c in result['cells'] if c['lane'] == 'headline' and c['pair'] == 'gpu-args,direct')
            self.assertEqual(headline['metrics']['cpuWorkNs']['median'], 20)
            self.assertIsNone(headline['metrics']['gpuSpanMs'])
            self.assertTrue(all(c['metrics']['gpuSpanMs'] is None for c in result['cells']))
            self.assertEqual(result['recommendations'][0]['decision'], 'defer')
            self.assertTrue(all(row['throughputLossPct'] is None for row in result['observerOverhead']))
            self.assertEqual(headline['runStats']['gpu-args']['requestedBytes'], 1000)
            row = p.loads((a / 'frames.jsonl').read_text().splitlines()[0])
            self.assertIn('runId', row)
            self.assertIn('pairId', row)
            self.assertEqual(row['frame'], 0)

    def test_missing_pair_invalidates_entire_cell_without_dropping_repetition(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'bundle'
            root.mkdir()
            self.make_bundle(root)
            # Simulate interrupted process by moving its receipt, retaining the original evidence.
            receipt = root / 'pairs' / 'p00000' / 'process' / 'receipt.json'
            receipt.rename(receipt.with_name('interrupted-receipt.json'))
            code, _, result = self.report(root, 'report')
            self.assertEqual(code, 2)
            cell = result['cells'][0]
            self.assertEqual(cell['validPairs'], 11)
            self.assertEqual(cell['status'], 'invalid')
            self.assertIsNone(cell['metrics']['cpuWorkNs'])

    def test_changed_hash_poisons_all_comparisons(self):
        for field in ('shaderHash', 'protocolHash', 'environmentHash'):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / 'bundle'
                root.mkdir()
                self.make_bundle(root, bad=lambda job, d: d.update({field: 'b' * 64})
                                 if job['id'] == 'p00001' else None)
                code, _, result = self.report(root, 'report')
                self.assertEqual(code, 2)
                self.assertTrue(result['auditErrors'])
                self.assertTrue(all(c['metrics']['cpuWorkNs'] is None for c in result['cells']))

    def test_validation_rows_and_additional_gates(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = p.frozen_cases()[:1]
            ids = {field: 'a' * 64 for field in p.HASH_FIELDS}
            manifests = {cases[0]['id']: 'f' * 64}
            rows = [dict(case=cases[0]['id'], suite=suite, variant=mode, frames=256,
                         manifestHash='f' * 64, reference=True, scoredReplay=True, retirement=True)
                    for suite in ('S', 'E') for mode in capabilities()['modes']]
            data = dict(schemaVersion=1, **ids, results=rows, environment={'validation': True})
            p.save(root / 'validation.json', data)
            gates = p.gates_for(root, ids, manifests, cases, capabilities())
            self.assertEqual(gates['validation']['status'], 'verified')
            self.assertEqual(gates['capture']['status'], 'unavailable')
            data['results'].append(copy.deepcopy(rows[0]))
            with patch('paired.load', return_value=data):
                gates = p.gates_for(root, ids, manifests, cases, capabilities())
            self.assertEqual(gates['validation']['status'], 'failed')
            self.assertIn('duplicate', gates['validation']['reason'])

    def test_gate_environment_checkpoint_and_local_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = p.frozen_cases()[:1]
            ids = {field: 'a' * 64 for field in p.HASH_FIELDS}
            manifests = {cases[0]['id']: 'f' * 64}
            rows = [dict(case=cases[0]['id'], suite=suite, variant=mode, frames=256,
                         manifestHash='f' * 64, reference=True, scoredReplay=True, retirement=True)
                    for suite in ('S', 'E') for mode in capabilities()['modes']]
            data = dict(schemaVersion=1, **ids, results=rows, environment={'validation': True},
                        gates={key: dict(status='verified', reason='test evidence',
                                         references=['proof.json'], frames=900) for key in p.GATES[1:]})
            p.save(root / 'validation.json', data)
            p.save(root / 'proof.json', {'verified': True})
            self.assertTrue(all(gate['status'] == 'verified' for gate in
                                p.gates_for(root, ids, manifests, cases, capabilities()).values()))
            mutations = (
                lambda d: d['environment'].update(validation=False),
                lambda d: d.update(shaderHash='b' * 64),
                lambda d: d['results'][0].update(scoredReplay=False),
                lambda d: d['results'][0].update(frames=255),
                lambda d: d['results'][0].update(manifestHash='b' * 64),
                lambda d: d['results'].pop(),
                lambda d: d['gates']['capture'].update(references=['../escape']),
                lambda d: d['gates']['capture'].update(references=[str(root / 'proof.json')]),
                lambda d: d['gates']['measurementFreeze'].update(references=['missing.json']),
                lambda d: d['gates']['lifetimeStress'].update(frames=899),
            )
            for mutate in mutations:
                altered = copy.deepcopy(data)
                mutate(altered)
                with patch('paired.load', return_value=altered):
                    gates = p.gates_for(root, ids, manifests, cases, capabilities())
                self.assertEqual(gates['validation']['status'], 'failed')
            altered = copy.deepcopy(data)
            altered['gates'].pop('checkpointA')
            with patch('paired.load', return_value=altered):
                gates = p.gates_for(root, ids, manifests, cases, capabilities())
            self.assertEqual(gates['checkpointA']['status'], 'unavailable')

    def test_validation_import_and_frozen_artifact_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, target = root / 'source', root / 'bundle'
            source.mkdir()
            target.mkdir()
            proof = source / 'proof'
            proof.mkdir()
            p.save(proof / 'checkpoint.json', {'passed': True})
            p.save(source / 'validation.json', {'gates': {
                'checkpointA': {'status': 'verified', 'reason': 'passed',
                                'references': ['proof', 'proof/checkpoint.json']}}})
            p.import_validation(source / 'validation.json', target)
            self.assertEqual((proof / 'checkpoint.json').read_bytes(),
                             (target / 'proof' / 'checkpoint.json').read_bytes())
            hash_value, artifacts = p.validation_inventory(target)
            config = {'validationHash': hash_value, 'validationArtifacts': artifacts}
            p.check_validation_freeze(target, config)
            p.save(target / 'proof' / 'new-file.json', {'changed': True})
            with self.assertRaisesRegex(p.Invalid, 'evidence changed'):
                p.check_validation_freeze(target, config)
            (target / 'escape').symlink_to(source)
            with self.assertRaisesRegex(p.Invalid, 'escapes bundle'):
                p.evidence_path(target, 'escape/validation.json')


if __name__ == '__main__':
    unittest.main()
