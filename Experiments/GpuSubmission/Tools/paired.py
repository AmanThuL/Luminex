#!/usr/bin/env python3
"""Collect and independently audit the frozen GPU-submission paired protocol (stdlib only)."""

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from functools import lru_cache

VERSION = 1
SEED = 0x4C4D5836
PAIRS = 12
FRAMES = 256
WARMUP = 32
RESAMPLES = 10000
MEMORY_LIMIT = 256 * 1024 * 1024
MODES = ('direct', 'cpu-indirect', 'gpu-args', 'batched', 'gpu-icb')
COMPARISONS = tuple((m, 'direct') for m in MODES if m != 'direct') + (
    ('gpu-args', 'batched'), ('gpu-icb', 'batched'))
HASH_FIELDS = ('shaderHash', 'executableHash', 'protocolHash', 'environmentHash')
METRICS = ('cpuWorkNs', 'throughput', 'gpuSpanMs')
FLAGS = ('MTL_DEBUG_LAYER', 'MTL_CAPTURE_ENABLED', 'MTL_SHADER_VALIDATION')
GATES = ('validation', 'capture', 'lifetimeStress', 'measurementFreeze', 'checkpointA')


class Invalid(ValueError):
    """An evidence contract violation; never silently omit its statistical unit."""


def require(condition, message):
    if not condition:
        raise Invalid(message)


def finite_tree(value):
    if isinstance(value, float):
        require(math.isfinite(value), 'non-finite JSON number')
    elif isinstance(value, dict):
        for item in value.values():
            finite_tree(item)
    elif isinstance(value, list):
        for item in value:
            finite_tree(item)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def loads(text):
    value = json.loads(text, object_pairs_hook=unique_object)
    finite_tree(value)
    return value


def load(path):
    return loads(Path(path).read_text(encoding='utf-8'))


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False,
                      allow_nan=False)


def save(path, value):
    # Exclusive creation makes accidental evidence replacement a hard failure.
    with Path(path).open('x', encoding='utf-8') as stream:
        stream.write(encoded(value) + '\n')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sha(value, name):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value),
            'invalid ' + name)
    return value


def native_environment_hash(environment):
    """Match the CLI's compact, insertion-ordered JSON and control-character escaping."""
    def native(value):
        if isinstance(value, str):
            return '"' + ''.join('\\' + c if c in ('"', '\\') else
                                 f'\\u{ord(c):04x}' if ord(c) < 32 else c for c in value) + '"'
        if isinstance(value, dict):
            return '{' + ','.join(native(k) + ':' + native(v) for k, v in value.items()) + '}'
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(native(environment).encode('utf-8')).hexdigest()


def number(value, name, positive=False, integer=False, nullable=False):
    if value is None and nullable:
        return value
    require((type(value) is int and abs(value) <= (1 << 64) - 1) or
            (type(value) is float and math.isfinite(value)), 'invalid ' + name)
    require(value > 0 if positive else value >= 0, 'out of range ' + name)
    if integer:
        require(type(value) is int, 'non-integer ' + name)
    return value


def frozen_cases():
    values = [(n, t, v, 1) for n, t, v in itertools.product(
        (1024, 16384, 65536), (2, 32), (0.1, 0.5, 1.0))]
    values += [(16384, 2, 0.5, b) for b in (16, 64)]
    return [dict(id=f'n{n}-t{t}-v{int(v * 100)}-b{b}', count=n, triangles=t,
                 visibleFraction=v, bins=b) for n, t, v, b in values]


def validate_cases(cases):
    require(isinstance(cases, list), '--list-cases must emit a bare array')
    require(cases == frozen_cases(), 'case list differs from frozen twenty-case matrix')
    # Equality alone would allow bool == 1 or float == int.
    for case in cases:
        for field in ('count', 'triangles', 'bins'):
            number(case[field], field, integer=True, positive=True)
        number(case['visibleFraction'], 'visibleFraction', positive=True)
    return cases


def validate_caps(caps):
    require(type(caps) is dict and type(caps.get('schemaVersion')) is int
            and caps['schemaVersion'] == VERSION,
            'invalid capability schema')
    modes = caps.get('modes')
    require(isinstance(modes, list) and all(isinstance(m, str) for m in modes)
            and len(set(modes)) == len(modes) and set(modes) <= set(MODES),
            'invalid capability modes')
    require('direct' in modes, 'direct baseline unavailable')
    require(caps.get('gpuSpan') in ('verified', 'unavailable'), 'invalid GPU span capability')
    icb = caps.get('icb', {})
    require(isinstance(icb, dict) and icb.get('status') in
            ('verified', 'unsupported', 'unresolved', 'unavailable')
            and isinstance(icb.get('reason'), str) and icb['reason'], 'invalid ICB capability')
    require(('gpu-icb' in modes) == (icb['status'] == 'verified'),
            'ICB mode and feasibility gate disagree')
    return caps


def schedule(cases):
    jobs = []
    for case, suite, lane, comparison, repetition in itertools.product(
            cases, ('S', 'E'), ('headline', 'gpu-span'), COMPARISONS, range(PAIRS)):
        jobs.append(dict(id=f'p{len(jobs):05d}', case=case, suite=suite, lane=lane,
                         pair=','.join(comparison), repetition=repetition,
                         order='AB' if repetition % 2 == 0 else 'BA'))
    return jobs


def unavailable(job, caps):
    absent = [mode for mode in job['pair'].split(',') if mode not in caps['modes']]
    if absent:
        return 'mode unavailable: ' + ','.join(absent) + '; ' + caps['icb']['reason']
    if job['lane'] == 'gpu-span' and caps['gpuSpan'] != 'verified':
        return 'GPU workload boundary ordering unverified; span unavailable'
    return None


def validate_pair(data, job, directory=None):
    """Validate one independent pair and reduce its two runs, preserving AB/BA identity."""
    require(type(data) is dict and type(data.get('schemaVersion')) is int
            and data['schemaVersion'] == VERSION, 'invalid result schema')
    finite_tree(data)
    for field in ('case', 'suite', 'lane', 'pair', 'order'):
        require(data.get(field) == job[field], 'mismatched ' + field)
    for field in ('count', 'triangles', 'bins'):
        number(data['case'][field], field, positive=True, integer=True)
    require(type(data.get('warmup')) is int and data['warmup'] == WARMUP, 'invalid warmup')
    require(type(data.get('frameCount')) is int and data['frameCount'] == FRAMES,
            'invalid measured frame count')
    for field in (*HASH_FIELDS, 'manifestHash'):
        sha(data.get(field), field)
    if directory is not None:
        require(digest(Path(directory) / 'manifest.json') == data['manifestHash'],
                'manifest content hash mismatch')
        manifest = load(Path(directory) / 'manifest.json')
        require(isinstance(manifest, dict) and type(manifest.get('schemaVersion')) is int
                and manifest['schemaVersion'] == VERSION and manifest.get('valid') is True,
                'invalid resolved manifest')
        require(all(manifest.get(field) == value for field, value in job['case'].items()),
                'manifest describes another case')
    environment = data.get('environment')
    require(isinstance(environment, dict), 'missing environment')
    require(environment.get('validation') is False and environment.get('capture') is False,
            'measurement has validation/capture enabled or unspecified')
    require(environment.get('shaderValidation') is False,
            'measurement has shader validation enabled')
    require(native_environment_hash(environment) == data['environmentHash'],
            'environment content hash mismatch')
    runs = data.get('runs')
    require(isinstance(runs, list) and len(runs) == 2, 'incomplete pair')
    require(all(isinstance(run, dict) for run in runs), 'invalid run object')
    order = job['pair'].split(',')
    if job['order'] == 'BA':
        order.reverse()
    require([run.get('variant') for run in runs] == order, 'duplicate/misordered variants')
    reduced = {}
    devices = set()
    for run in runs:
        frames = run.get('frames')
        require(isinstance(frames, list) and len(frames) == FRAMES, 'missing/extra frames')
        require(all(isinstance(row, dict) for row in frames), 'invalid frame object')
        ids = [row.get('frame') for row in frames]
        require(all(type(i) is int for i in ids) and sorted(ids) == list(range(FRAMES)),
                'duplicate, missing or invalid frame IDs')
        for row in frames:
            for field in ('cpuWorkNs', 'waitNs', 'drawCalls', 'copiedBytes'):
                require(field in row, 'missing frame field ' + field)
                number(row[field], field, integer=True, positive=field == 'cpuWorkNs')
            for field in ('gpuSpanMs', 'preparationMs', 'rasterMs'):
                require(field in row, 'missing nullable field ' + field)
                number(row[field], field, nullable=True, positive=True)
            if job['lane'] == 'headline':
                require(all(row[field] is None for field in
                            ('gpuSpanMs', 'preparationMs', 'rasterMs')),
                        'headline lane contains GPU markers')
            else:
                require(run.get('gpuSpanStatus') == 'verified' and row['gpuSpanMs'] is not None,
                        'missing or unverified GPU timestamp')
                require(row['preparationMs'] is None and row['rasterMs'] is None,
                        'stage markers in GPU-span lane')
        require(isinstance(run.get('gpuSpanStatus'), str) and run['gpuSpanStatus'],
                'missing GPU span status/reason')
        number(run.get('throughput'), 'throughput', positive=True)
        for field in ('setupMs', 'drainMs'):
            number(run.get(field), field)
        for field in ('requestedBytes', 'allocatedBytes', 'residentBytes'):
            require(field in run, 'missing memory field ' + field)
            number(run[field], field, integer=True, nullable=field != 'requestedBytes')
        require(run['requestedBytes'] <= MEMORY_LIMIT, 'requested storage exceeds 256 MiB')
        require(isinstance(run.get('device'), str) and run['device'], 'missing device')
        devices.add(run['device'])
        opaque = run.get('opaqueIcbAllocatedBytes')
        number(opaque, 'opaqueIcbAllocatedBytes', integer=True, nullable=True)
        reduced[run['variant']] = dict(
            cpuWorkNs=statistics.median(row['cpuWorkNs'] for row in frames),
            throughput=run['throughput'],
            gpuSpanMs=(statistics.median(row['gpuSpanMs'] for row in frames)
                       if job['lane'] == 'gpu-span' else None),
            waitNs=statistics.median(row['waitNs'] for row in frames),
            drawCalls=statistics.median(row['drawCalls'] for row in frames),
            copiedBytes=statistics.median(row['copiedBytes'] for row in frames),
            requestedBytes=run['requestedBytes'], allocatedBytes=run['allocatedBytes'],
            residentBytes=run['residentBytes'], opaqueIcbAllocatedBytes=opaque)
        reduced[run['variant']].update(setupMs=run['setupMs'], drainMs=run['drainMs'])
    require(len(devices) == 1, 'pair used different devices')
    return reduced, next(iter(devices))


def percentile(sorted_values, fraction):
    position = (len(sorted_values) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * (position - lower)


@lru_cache(maxsize=4096)
def bootstrap(deltas):
    require(len(deltas) == PAIRS, 'bootstrap requires twelve independent pairs')
    rng = random.Random(SEED)
    values = sorted(statistics.median(rng.choices(deltas, k=PAIRS)) for _ in range(RESAMPLES))
    return dict(median=statistics.median(deltas), low=percentile(values, 0.025),
                high=percentile(values, 0.975), pairs=PAIRS)


def improvement(candidate, control, metric):
    number(candidate, 'candidate metric', positive=True)
    number(control, 'control denominator', positive=True)
    return 100 * ((candidate - control) if metric == 'throughput' else
                  (control - candidate)) / control


def material_win(stat):
    return stat is not None and stat['median'] >= 15 and stat['low'] > 0


def evidence_path(root, reference):
    require(isinstance(reference, str) and reference, 'empty evidence reference')
    path = Path(reference)
    require(not path.is_absolute() and '..' not in path.parts and path.parts
            and path != Path('.'), 'evidence reference must stay within bundle')
    resolved = (root / path).resolve(strict=True)
    require(resolved.is_relative_to(root.resolve()) and resolved != root.resolve(),
            'evidence reference escapes bundle')
    return resolved


def validation_inventory(root):
    path = root / 'validation.json'
    if not path.exists():
        return None, {}
    data = load(path)
    require(isinstance(data, dict) and isinstance(data.get('gates', {}), dict),
            'invalid validation object')
    references = set()
    for gate in data.get('gates', {}).values():
        require(isinstance(gate, dict) and isinstance(gate.get('references', []), list)
                and all(isinstance(v, str) for v in gate.get('references', [])),
                'invalid gate references')
        references.update(gate.get('references', []))
    artifacts = {}
    for reference in sorted(references):
        resolved = evidence_path(root, reference)
        if resolved.is_file():
            artifacts[reference] = digest(resolved)
        else:
            contents = []
            for item in sorted(resolved.rglob('*')):
                require(not item.is_symlink(), 'evidence directory contains symlinks')
                if item.is_file():
                    contents.append((item.relative_to(resolved).as_posix(), digest(item)))
            artifacts[reference] = hashlib.sha256(encoded(contents).encode()).hexdigest()
    return digest(path), artifacts


def check_validation_freeze(root, config):
    validation_hash, artifacts = validation_inventory(root)
    require(validation_hash == config.get('validationHash') and
            artifacts == config.get('validationArtifacts', {}),
            'validation evidence changed since collection freeze')


def import_validation(source, root):
    """Copy the completed gate bundle, retaining only its explicitly referenced artifacts."""
    data = load(source)
    require(isinstance(data, dict) and isinstance(data.get('gates', {}), dict),
            'invalid validation object')
    references = []
    for gate in data.get('gates', {}).values():
        require(isinstance(gate, dict) and isinstance(gate.get('references', []), list)
                and all(isinstance(v, str) for v in gate.get('references', [])),
                'invalid gate references')
        references.extend(gate.get('references', []))
    sources = []
    reserved = {'validation.json', 'collection.json', 'cases.json', 'capabilities.json',
                'environment.json', 'discovery', 'sessions', 'pairs', 'reports'}
    for reference in sorted(set(references)):
        original = evidence_path(source.parent, reference)
        target = root / reference
        require(Path(reference).parts[0] not in reserved, 'evidence reference collides with driver output')
        if original.is_dir():
            require(not any(path.is_symlink() for path in original.rglob('*')),
                    'referenced directory contains symlinks')
        sources.append((original, target))
    copied = []
    for original, target in sorted(sources, key=lambda pair: len(pair[1].parts)):
        if any(target.is_relative_to(parent) for parent in copied):
            continue
        require(not target.exists(), 'evidence reference output collision')
        target.parent.mkdir(parents=True, exist_ok=True)
        if original.is_dir():
            shutil.copytree(original, target)
        else:
            shutil.copyfile(original, target)
        copied.append(target)
    save(root / 'validation.json', data)


def gates_for(root, identities, manifests, cases, caps):
    """CLI verification rows prove only their stated scope; additional gates are explicit."""
    gates = {}
    path = root / 'validation.json'
    if not path.exists():
        return {'validation': {'status': 'unavailable', 'reason': 'validation.json absent'}}
    try:
        data = load(path)
        require(isinstance(data, dict) and type(data.get('schemaVersion')) is int
                and data['schemaVersion'] == VERSION, 'invalid validation schema')
        require(isinstance(data.get('environment'), dict)
                and data['environment'].get('validation') is True,
                'verification must run with MTL_DEBUG_LAYER enabled')
        for field in ('shaderHash', 'executableHash', 'protocolHash'):
            require(identities and data.get(field) == identities[field],
                    'validation identity mismatch: ' + field)
        rows = data.get('results')
        require(isinstance(rows, list), 'missing validation results')
        indexed = {}
        for row in rows:
            require(isinstance(row, dict) and all(isinstance(row.get(k), str) for k in
                        ('case', 'suite', 'variant')), 'invalid validation row identifiers')
            key = (row.get('case'), row.get('suite'), row.get('variant'))
            require(key not in indexed, 'duplicate validation row')
            require(key[0] in {c['id'] for c in cases} and key[1] in ('S', 'E')
                    and key[2] in caps['modes'], 'unknown validation row')
            require(row.get('manifestHash') == manifests.get(key[0]),
                    'validation manifest mismatch')
            require(type(row.get('frames')) is int and row['frames'] >= FRAMES,
                    'incomplete scored replay')
            require(all(row.get(field) is True for field in
                        ('reference', 'scoredReplay', 'retirement')), 'failed validation row')
            indexed[key] = row
        expected = set(itertools.product([c['id'] for c in cases], ('S', 'E'), caps['modes']))
        require(set(indexed) == expected, 'incomplete validation coverage')
        gates['validation'] = {'status': 'verified', 'reason': 'complete CLI validation coverage'}
        extra = data.get('gates', {})
        require(isinstance(extra, dict), 'invalid aggregate gates')
        for name in GATES[1:]:
            gate = extra.get(name, {'status': 'unavailable', 'reason': name + ' evidence absent'})
            require(isinstance(gate, dict) and gate.get('status') in
                    ('verified', 'failed', 'unavailable', 'unresolved'), 'invalid gate ' + name)
            if gate['status'] == 'verified':
                require(isinstance(gate.get('reason'), str) and gate['reason']
                        and isinstance(gate.get('references'), list) and gate['references']
                        and all(isinstance(v, str) and v for v in gate['references']),
                        'verified gate lacks evidence references: ' + name)
                for reference in gate['references']:
                    evidence_path(root, reference)
                if name == 'lifetimeStress':
                    require(type(gate.get('frames')) is int and gate['frames'] >= 900,
                            'lifetime stress shorter than 900 frames')
            gates[name] = gate
    except (Invalid, ValueError, OSError, KeyError, TypeError) as error:
        gates['validation'] = {'status': 'failed', 'reason': str(error)}
    return gates


def decide(cells, cases, caps, gates, complete=True):
    """Section 10: same metric, both controls, two adjacent scales, strict loss guards."""
    index = {(r['case']['id'], r['suite'], r['pair'], r['lane']): r for r in cells}
    recommendations = []
    gates_ok = all(gates.get(k, {}).get('status') == 'verified' for k in GATES) and complete
    for candidate in ('gpu-args', 'gpu-icb'):
        reasons, regions = [], []
        if candidate not in caps['modes']:
            recommendations.append(dict(candidate=candidate, decision='defer', regions=[],
                                        reasons=['candidate unavailable: ' + caps['icb']['reason']]))
            continue
        missing_guard = False
        by_shape = {}
        for case in cases:
            by_shape.setdefault((case['triangles'], case['bins'], case['visibleFraction']), []).append(case)
        for shape, group in sorted(by_shape.items()):
            group.sort(key=lambda c: c['count'])
            for adjacent in zip(group, group[1:]):
                winners = {'cpuWorkNs', 'throughput'}
                guards_ok = True
                for case, control in itertools.product(adjacent, ('direct', 'batched')):
                    pair = candidate + ',' + control
                    headline = index.get((case['id'], 'E', pair, 'headline'), {})
                    span = index.get((case['id'], 'E', pair, 'gpu-span'), {})
                    metrics = headline.get('metrics', {})
                    winners &= {m for m in winners if material_win(metrics.get(m))}
                    for stat in (metrics.get('throughput'), span.get('metrics', {}).get('gpuSpanMs')):
                        if stat is None:
                            missing_guard = True
                            guards_ok = False
                        elif stat['low'] <= -15:
                            guards_ok = False
                    if candidate == 'gpu-icb' and not headline.get('icbMemoryKnown', False):
                        missing_guard = True
                        guards_ok = False
                if winners and guards_ok and gates_ok:
                    regions.append(dict(cases=[c['id'] for c in adjacent], triangles=shape[0],
                                        bins=shape[1], visibleFraction=shape[2],
                                        winningMetrics=sorted(winners)))
        if regions:
            decision = 'adopt'
            reasons.append('bounded E regions only; M7 production benchmark still required')
        elif not gates_ok or missing_guard:
            decision = 'defer'
            if not gates_ok:
                reasons.append('required correctness/capture/lifetime/freeze gates or collection incomplete')
            if missing_guard:
                reasons.append('GPU-span/throughput guard or opaque ICB memory unavailable')
        else:
            decision = 'retain'
            reasons.append('no adjacent region clears the same-metric wins and strict -15% guards')
        recommendations.append(dict(candidate=candidate, decision=decision, regions=regions, reasons=reasons))
    return recommendations


def reserve(path):
    path = Path(path).resolve()
    source = Path(__file__).resolve().parents[3]
    require(not path.is_relative_to(source), 'evidence must be outside the source tree')
    if path.exists():
        require(path.is_dir() and not any(path.iterdir()), 'output directory is nonempty')
    else:
        path.mkdir(parents=True)
    return path


def now():
    return datetime.now(timezone.utc).isoformat()


def telemetry():
    """Unparsed, actual macOS telemetry, outside the scored subprocess window."""
    result = {'observedAt': now()}
    for key, option in (('power', 'batt'), ('thermal', 'therm')):
        command = ['pmset', '-g', option]
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=15, check=False)
            result[key] = dict(command=command, value=run.stdout if run.returncode == 0 else None,
                               error=None if run.returncode == 0 else
                               (run.stderr.strip() or 'pmset failed'), exitCode=run.returncode,
                               stdout=run.stdout, stderr=run.stderr)
        except (OSError, subprocess.TimeoutExpired) as error:
            result[key] = dict(command=command, value=None, error=str(error), exitCode=None,
                               stdout=None, stderr=None)
    return result


def run_command(command, logs, timeout=None):
    logs.mkdir(parents=True, exist_ok=True)
    save(logs / 'command.json', {'command': command, 'startedAt': now()})
    code, error = None, None
    try:
        with (logs / 'stdout.log').open('x') as stdout, (logs / 'stderr.log').open('x') as stderr:
            completed = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout,
                                       check=False, cwd=logs)
            code = completed.returncode
    except (OSError, subprocess.TimeoutExpired) as failure:
        error = str(failure)
    receipt = dict(exitCode=code, error=error, finishedAt=now())
    save(logs / 'receipt.json', receipt)
    return receipt


def flags():
    return {key: os.environ.get(key) for key in FLAGS}


def collect(args):
    bench = Path(args.bench).resolve(strict=True)
    require(bench.is_file(), '--bench must name a binary')
    require(not any(value not in (None, '', '0') for value in flags().values()),
            'collection refuses active Metal validation/capture environment flags')
    if args.resume:
        root = Path(args.output).resolve(strict=True)
        config = load(root / 'collection.json')
        require(config['benchHash'] == digest(bench), 'resume executable changed')
        require(config['driverHash'] == digest(__file__), 'resume driver changed')
        require(config['schemaHash'] == digest(Path(__file__).with_name('schema.md')), 'resume schema changed')
        require(config['flags'] == flags(), 'resume environment flags changed')
        check_validation_freeze(root, config)
        cases = validate_cases(load(root / 'cases.json'))
        caps = validate_caps(load(root / 'capabilities.json'))
        require(config['casesHash'] == digest(root / 'cases.json') and
                config['capabilitiesHash'] == digest(root / 'capabilities.json'), 'resume inventory changed')
    else:
        root = reserve(args.output)
        inventories = {}
        for name, action in (('cases', '--list-cases'), ('capabilities', '--capabilities')):
            directory = root / 'discovery' / name
            receipt = run_command([str(bench), action], directory, args.timeout)
            require(receipt['exitCode'] == 0, name + ' discovery failed; raw logs retained')
            inventories[name] = loads((directory / 'stdout.log').read_text())
        cases = validate_cases(inventories['cases'])
        caps = validate_caps(inventories['capabilities'])
        save(root / 'cases.json', cases)
        save(root / 'capabilities.json', caps)
        if args.validation:
            import_validation(Path(args.validation).resolve(strict=True), root)
        validation_hash, validation_artifacts = validation_inventory(root)
        config = dict(schemaVersion=VERSION, bench=str(bench), benchHash=digest(bench),
                      driverHash=digest(__file__), schemaHash=digest(Path(__file__).with_name('schema.md')),
                      casesHash=digest(root / 'cases.json'), capabilitiesHash=digest(root / 'capabilities.json'),
                      flags=flags(), seed=SEED, pairs=PAIRS, warmup=WARMUP, frames=FRAMES,
                      resamples=RESAMPLES, createdAt=now(), validationHash=validation_hash,
                      validationArtifacts=validation_artifacts)
        save(root / 'collection.json', config)
    sessions = root / 'sessions'
    sessions.mkdir(exist_ok=True)
    session = sessions / f's{len(list(sessions.iterdir())):04d}'
    session.mkdir()
    environment = dict(schemaVersion=VERSION, flags=flags(), pre=telemetry(), post=None,
                       telemetryAffectsEnvironmentHash=False)
    save(session / 'pre.json', environment)
    attempted = 0
    jobs = schedule(cases)
    try:
        for job in jobs:
            directory = root / 'pairs' / job['id']
            if directory.exists():
                require(args.resume, 'pair output collision')
                # An interrupted or failed attempt is never replaced by a more favorable sample.
                continue
            directory.mkdir(parents=True)
            save(directory / 'job.json', job)
            reason = unavailable(job, caps)
            if reason:
                save(directory / 'skip.json', {'status': 'unavailable', 'reason': reason})
                continue
            command = [str(bench), '--measure', '--case', job['case']['id'],
                       '--suite', job['suite'], '--pair', job['pair'], '--order', job['order'],
                       '--lane', job['lane'], '--output', str(directory / 'output')]
            print(f"{job['id']}/{len(jobs)} {job['case']['id']} {job['suite']} "
                  f"{job['lane']} {job['pair']} {job['order']}", flush=True)
            run_command(command, directory / 'process', args.timeout)
            attempted += 1
            if args.max_pairs is not None and attempted >= args.max_pairs:
                break
    finally:
        environment['post'] = telemetry()
        save(session / 'environment.json', environment)
        # Append-only session snapshots preserve pre/post observations for every explicit resume.
        # The root file is a first-session snapshot; subsequent sessions remain separate evidence.
        if not (root / 'environment.json').exists():
            save(root / 'environment.json', environment)
    destination = root / 'reports' / session.name
    return summarize(root, reserve(destination))


def summarize(root, output):
    root = Path(root).resolve(strict=True)
    require(root != output and not root.is_relative_to(output), 'report output overlaps source evidence')
    cases = validate_cases(load(root / 'cases.json'))
    caps = validate_caps(load(root / 'capabilities.json'))
    config = load(root / 'collection.json')
    require(isinstance(config, dict) and type(config.get('schemaVersion')) is int
            and config['schemaVersion'] == VERSION, 'invalid collection schema')
    for field, value in (('seed', SEED), ('pairs', PAIRS), ('warmup', WARMUP),
                         ('frames', FRAMES), ('resamples', RESAMPLES)):
        require(config.get(field) == value, 'collection protocol mismatch: ' + field)
    require(config.get('casesHash') == digest(root / 'cases.json') and
            config.get('capabilitiesHash') == digest(root / 'capabilities.json'), 'inventory changed')
    require(config.get('driverHash') == digest(__file__) and
            config.get('schemaHash') == digest(Path(__file__).with_name('schema.md')),
            'analysis implementation differs from collection freeze')
    check_validation_freeze(root, config)
    jobs = schedule(cases)
    expected_dirs = {j['id'] for j in jobs}
    pairs_dir = root / 'pairs'
    if pairs_dir.exists():
        require(all(p.name in expected_dirs for p in pairs_dir.iterdir()), 'unknown/duplicate pair directory')
    identities, manifests, device = None, {}, None
    records, groups, audit_errors = [], {}, []
    with (output / 'frames.jsonl').open('x', encoding='utf-8') as frame_stream:
        for job in jobs:
            directory = pairs_dir / job['id']
            key = (job['case']['id'], job['suite'], job['lane'], job['pair'])
            group = groups.setdefault(key, [])
            record = dict(job=job, status='invalid', reason=None, runs=None)
            try:
                require(load(directory / 'job.json') == job, 'pair identity mismatch')
                reason = unavailable(job, caps)
                if reason:
                    skip = load(directory / 'skip.json')
                    require(skip == {'status': 'unavailable', 'reason': reason}, 'incorrect unavailable record')
                    record.update(status='unavailable', reason=reason)
                else:
                    require(not (directory / 'skip.json').exists(), 'supported pair was skipped')
                    receipt = load(directory / 'process' / 'receipt.json')
                    require(isinstance(receipt, dict) and type(receipt.get('exitCode')) is int
                            and receipt['exitCode'] == 0 and receipt.get('error') is None,
                            'failed process: ' + encoded(receipt))
                    require(not (directory / 'output' / 'failure.json').exists(), 'native failure record exists')
                    data = load(directory / 'output' / 'result.json')
                    require(isinstance(data, dict), 'invalid pair result object')
                    identity = {field: data.get(field) for field in HASH_FIELDS}
                    if identity['executableHash'] != config['benchHash'] or (
                            identities is not None and identity != identities):
                        audit_errors.append(job['id'] + ': global identities changed')
                        raise Invalid('global identities changed; collection invalidated')
                    runs, pair_device = validate_pair(data, job, directory / 'output')
                    if identities is None:
                        identities, device = identity, pair_device
                    if identities != identity or device != pair_device:
                        audit_errors.append(job['id'] + ': global identities/device changed')
                        raise Invalid('global identities/device changed; collection invalidated')
                    if job['case']['id'] in manifests:
                        if manifests[job['case']['id']] != data['manifestHash']:
                            audit_errors.append(job['id'] + ': case manifest changed')
                            raise Invalid('case manifest changed; collection invalidated')
                    else:
                        manifests[job['case']['id']] = data['manifestHash']
                    record.update(status='valid', runs=runs)
                    for run in data['runs']:
                        for frame in sorted(run['frames'], key=lambda row: row['frame']):
                            row = dict(pairId=job['id'], runId=job['id'] + ':' + run['variant'],
                                       case=job['case']['id'], suite=job['suite'], lane=job['lane'],
                                       variant=run['variant'], manifestHash=data['manifestHash'], **frame)
                            frame_stream.write(encoded(row) + '\n')
            except (Invalid, OSError, ValueError, KeyError, TypeError) as error:
                # Paths in diagnostics are relative so summaries survive moving the raw bundle.
                reason = str(error).replace(str(root), '<bundle>')
                record.update(status='invalid', reason=reason, runs=None)
            records.append(record)
            group.append(record)
    cells = []
    case_lookup = {case['id']: case for case in cases}
    for (case_id, suite, lane, pair), group in groups.items():
        valid = all(record['status'] == 'valid' for record in group) and not audit_errors
        all_unavailable = all(record['status'] == 'unavailable' for record in group)
        metrics = dict.fromkeys(METRICS)
        candidate, control = pair.split(',')
        deltas = dict.fromkeys(METRICS)
        if valid:
            for metric in (('cpuWorkNs', 'throughput') if lane == 'headline' else ('gpuSpanMs',)):
                delta = tuple(improvement(record['runs'][candidate][metric],
                                          record['runs'][control][metric], metric) for record in group)
                deltas[metric] = delta
                metrics[metric] = bootstrap(delta)
        reasons = sorted({record['reason'] for record in group if record['reason']})
        if audit_errors:
            reasons.append('global evidence identity changed; recollect all compared modes')
        run_stats = None
        if valid:
            run_stats = {}
            for mode in (candidate, control):
                run_stats[mode] = {}
                for field in group[0]['runs'][mode]:
                    values = [record['runs'][mode][field] for record in group]
                    run_stats[mode][field] = (None if any(v is None for v in values) else
                                              max(values) if field.endswith('Bytes') and
                                              field != 'copiedBytes' else
                                              statistics.median(values))
        cells.append(dict(case=case_lookup[case_id], suite=suite, lane=lane, pair=pair,
                          status='valid' if valid else 'unavailable' if all_unavailable else 'invalid',
                          validPairs=sum(r['status'] == 'valid' for r in group), metrics=metrics,
                          pairedDeltas=deltas, reasons=reasons, runStats=run_stats,
                          icbMemoryKnown=valid and all(r['runs'][candidate]['opaqueIcbAllocatedBytes']
                                                      is not None for r in group)))
    complete = all(record['status'] != 'invalid' for record in records) and not audit_errors
    gates = gates_for(root, identities, manifests, cases, caps)
    recommendations = decide(cells, cases, caps, gates, complete)
    result = dict(schemaVersion=VERSION, complete=complete, identities=identities, device=device,
                  gates=gates, auditErrors=sorted(set(audit_errors)), recommendations=recommendations,
                  observerOverhead=observer_comparisons(cells),
                  protocol=dict(seed=SEED, bootstrapResamples=RESAMPLES, pairs=PAIRS,
                                frames=FRAMES, warmup=WARMUP, confidence=0.95), cells=cells)
    save(output / 'summary.json', result)
    for filename, rows in (('pairs.jsonl', records), ('summary.jsonl', cells)):
        with (output / filename).open('x', encoding='utf-8') as stream:
            for row in rows:
                stream.write(encoded(row) + '\n')
    write_tables(output, result)
    print(f"Report: {output / 'summary.md'}; complete={complete}", flush=True)
    return 0 if complete else 2


def observer_comparisons(cells):
    """Descriptive lane comparison only; does not pool samples or create an adoption metric."""
    spans = {(c['case']['id'], c['suite'], c['pair']): c for c in cells if c['lane'] == 'gpu-span'}
    observations = []
    for cell in cells:
        if cell['lane'] != 'headline':
            continue
        span = spans.get((cell['case']['id'], cell['suite'], cell['pair']), {})
        for mode in cell['pair'].split(','):
            overhead = None
            if cell.get('runStats') and span.get('runStats'):
                headline = cell['runStats'][mode]['throughput']
                marked = span['runStats'][mode]['throughput']
                overhead = 100 * (headline - marked) / headline
            observations.append(dict(case=cell['case']['id'], suite=cell['suite'], pair=cell['pair'],
                                     variant=mode, throughputLossPct=overhead, scored=False,
                                     reason='descriptive median throughput comparison across lanes' if
                                     overhead is not None else 'complete verified lane comparison unavailable'))
    return observations


def write_tables(output, result):
    fields = ('case', 'suite', 'lane', 'pair', 'status', 'validPairs', 'metric',
              'medianImprovementPct', 'low95Pct', 'high95Pct', 'reason')
    lines = ['# GPU work-submission report', '',
             'Native-host mechanism evidence; production improvement requires an M7 benchmark.', '',
             'Durations: positive means faster. Throughput: positive means more completed frames/s.',
             'Each unit is one independent pair; run medians precede paired deltas. No outliers removed.',
             'Headline CPU/throughput and verified GPU spans remain separate lanes. Null is unavailable.',
             'Throughput includes finite-window fill/drain and waits; it is not reciprocal CPU encode time.',
             'Startup does not imply a cold driver shader cache. Power/thermal snapshots are descriptive.',
             'Run medians and storage high-water values are in runs.csv. Descriptive observer overhead',
             'is in summary.json; unavailable lane comparisons remain null.',
             '', f"Collection complete: {str(result['complete']).lower()}", '']
    for item in result['recommendations']:
        lines.append(f"- {item['candidate']}: {item['decision']} — " + '; '.join(item['reasons']))
        for region in item['regions']:
            lines.append('  Region: ' + ', '.join(region['cases']) + '; ' + ', '.join(region['winningMetrics']))
    lines += ['', 'Gates: `' + encoded(result['gates']) + '`', '',
              '| Case | Suite | Lane | Pair | Metric | Median % | 95% interval | Status |',
              '|---|---|---|---|---|---:|---|---|']
    with (output / 'summary.csv').open('x', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fields)
        writer.writeheader()
        for cell in result['cells']:
            for metric in METRICS:
                stat = cell['metrics'][metric]
                row = dict(case=cell['case']['id'], suite=cell['suite'], lane=cell['lane'],
                           pair=cell['pair'], status=cell['status'], validPairs=cell['validPairs'],
                           metric=metric, medianImprovementPct=stat['median'] if stat else 'null',
                           low95Pct=stat['low'] if stat else 'null', high95Pct=stat['high'] if stat else 'null',
                           reason='; '.join(cell['reasons']) or ('metric belongs to other lane' if not stat else ''))
                writer.writerow(row)
                median = f"{stat['median']:.3f}" if stat else 'null'
                interval = f"[{stat['low']:.3f}, {stat['high']:.3f}]" if stat else 'null'
                lines.append(f"| {row['case']} | {row['suite']} | {row['lane']} | {row['pair']} | "
                             f"{metric} | {median} | {interval} | {row['status']} |")
    with (output / 'summary.md').open('x', encoding='utf-8') as stream:
        stream.write('\n'.join(lines) + '\n')
    run_fields = ('case', 'suite', 'lane', 'pair', 'variant', 'status', *METRICS, 'waitNs',
                  'drawCalls', 'copiedBytes', 'requestedBytes', 'allocatedBytes', 'residentBytes',
                  'opaqueIcbAllocatedBytes', 'setupMs', 'drainMs')
    with (output / 'runs.csv').open('x', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, run_fields)
        writer.writeheader()
        for cell in result['cells']:
            for mode in cell['pair'].split(','):
                row = dict(case=cell['case']['id'], suite=cell['suite'], lane=cell['lane'],
                           pair=cell['pair'], variant=mode, status=cell['status'])
                values = (cell.get('runStats') or {}).get(mode, {})
                row.update({field: values[field] if values.get(field) is not None else 'null'
                            for field in run_fields[6:]})
                writer.writerow(row)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--bench', help='absolute or caller-relative benchmark binary')
    mode.add_argument('--summarize', metavar='ROOT', help='regenerate from retained raw evidence')
    parser.add_argument('--output', required=True, help='fresh external output directory')
    parser.add_argument('--resume', action='store_true', help='resume missing jobs; never replace attempts')
    parser.add_argument('--validation', help='CLI validation.json, optionally carrying evidence gates')
    parser.add_argument('--timeout', type=float, default=600, help='per-process seconds; failures retained')
    parser.add_argument('--max-pairs', type=int, help='stop after this many new attempts; incomplete/unscored')
    args = parser.parse_args(argv)
    try:
        number(args.timeout, 'timeout', positive=True)
        if args.max_pairs is not None:
            number(args.max_pairs, 'max-pairs', positive=True, integer=True)
        require(not args.resume or args.bench, '--resume requires --bench')
        require(not (args.resume and args.validation), 'resume cannot replace validation evidence')
        require(not args.summarize or not (args.validation or args.max_pairs),
                'summarize cannot change collection inputs')
        if args.summarize:
            return summarize(Path(args.summarize), reserve(args.output))
        return collect(args)
    except (Invalid, OSError, ValueError, KeyError, TypeError) as error:
        print('paired.py: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
