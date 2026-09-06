#!/usr/bin/env python3
"""CPU-only structural audit of explicit diagnostic run directories; never executes a run.

Exit 0 means records are consistent, including recorded failures. It does not certify
parity, performance, reliability, or any milestone gate. Identities are compared between
records, not authenticated against executables or shader artifacts.
"""

import argparse
import json
import math
from pathlib import Path
import re


PROTOCOLS = ('pipelined-feedback-v1', 'pipelined-feedback-dependency-v2',
             'pipelined-feedback-arguments-v3')
OUTCOME_FIELDS = {'status', 'verified', 'retiredFrames', 'error'}
MODES = ('direct', 'cpu-indirect', 'gpu-args', 'batched')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError('non-finite JSON constant: ' + value)


def read_record(path):
    data = json.loads(path.read_text(), object_pairs_hook=object_pairs,
                      parse_constant=reject_constant)
    require(type(data) is dict, path.name + ' must be an object')
    return data


def same_context(left, right, path='context'):
    """Compare all fields, including unknown settings, without bool/int coercion."""
    require(type(left) is type(right), path + ': type mismatch')
    if isinstance(left, dict):
        require(left.keys() == right.keys(), path + ': field set mismatch')
        for key in left:
            same_context(left[key], right[key], path + '.' + key)
    elif isinstance(left, list):
        require(len(left) == len(right), path + ': length mismatch')
        for index, (a, b) in enumerate(zip(left, right)):
            same_context(a, b, f'{path}[{index}]')
    else:
        if type(left) is float:
            require(math.isfinite(left), path + ': non-finite value')
        require(left == right, path + ': value mismatch')


def integer(value, minimum, maximum, label):
    require(type(value) is int and minimum <= value <= maximum,
            label + ': invalid integer')


def validate_context(data):
    require(type(data.get('schemaVersion')) is int and data['schemaVersion'] == 1,
            'schemaVersion must be 1')
    require(data.get('format') == 'lmx.submission.diagnostic', 'invalid diagnostic format')
    require(data.get('scored') is False, 'scored must be false (unscored diagnosis)')
    if 'unscored' in data:
        require(data['unscored'] is True, 'unscored must be true when present')
    require(data.get('protocol') in PROTOCOLS, 'unknown diagnostic protocol')
    for key in ('manifestHash', 'shaderHash', 'executableHash', 'protocolHash'):
        require(isinstance(data.get(key), str)
                and re.fullmatch(r'[0-9a-fA-F]{64}', data[key]), key + ': invalid SHA-256')
    require(data.get('suite') in ('S', 'E'), 'invalid suite')
    require(data.get('variant') in MODES, 'invalid diagnostic variant')
    integer(data.get('warmup'), 0, 32, 'warmup')
    integer(data.get('frameCount'), 1, 900, 'frameCount')
    case = data.get('case')
    require(type(case) is dict, 'case must be an object')
    require(isinstance(case.get('id'), str) and case['id'].strip(), 'case.id missing')
    for key, minimum in (('count', 0), ('triangles', 1), ('bins', 1)):
        integer(case.get(key), minimum, 2**32 - 1, 'case.' + key)
    fraction = case.get('visibleFraction')
    require(type(fraction) in (int, float) and math.isfinite(fraction)
            and 0 <= fraction <= 1, 'invalid visibleFraction')
    env = data.get('environment')
    require(type(env) is dict, 'environment must be an object')
    for key in ('osRelease', 'osVersion', 'machine', 'hostModel', 'compiler'):
        require(isinstance(env.get(key), str) and env[key].strip(), 'environment.' + key + ' missing')
    for key in ('validation', 'capture', 'shaderValidation'):
        require(type(env.get(key)) is bool, 'environment.' + key + ' must be boolean')
    require(env['capture'] is False, 'diagnosis cannot enable capture')
    version = PROTOCOLS.index(data['protocol']) + 1
    if version >= 2 or 'diagnosticDependency' in data:
        require(data.get('diagnosticDependency') in ('declared', 'all'), 'invalid diagnosticDependency')
        require(data['diagnosticDependency'] != 'all' or data['variant'] == 'gpu-args',
                'all dependency requires gpu-args')
    if version >= 3 or 'diagnosticArguments' in data:
        source = data.get('diagnosticArguments')
        require(source in ('cpu', 'gpu', 'native'), 'invalid diagnosticArguments')
        require((source in ('cpu', 'gpu')) == (data['variant'] == 'gpu-args'),
                'argument source incompatible with variant')
        require(source != 'cpu' or (data['suite'] == 'S'
                and data.get('diagnosticDependency') == 'declared'),
                'CPU argument source requires S/declared')
    if 'diagnosticShaderValidationEnvironment' in data:
        flags = data['diagnosticShaderValidationEnvironment']
        require(type(flags) is dict and all(v is None or type(v) is str for v in flags.values()),
                'shader-validation environment must contain strings or null')


def audit_directory(directory):
    """Return the recorded status after structural validation; read only these records."""
    directory = Path(directory)
    require(directory.is_dir(), 'not a run directory')
    result = directory / 'result.json'
    require(not result.exists() and not result.is_symlink(), 'result.json present: not diagnostic-only')
    start = read_record(directory / 'diagnostic-start.json')
    outcome = read_record(directory / 'diagnostic.json')
    require(not OUTCOME_FIELDS.intersection(start), 'start contains outcome fields')
    validate_context(start)
    context = {k: v for k, v in outcome.items() if k not in OUTCOME_FIELDS}
    same_context(start, context)
    status = outcome.get('status')
    require(status in ('retired', 'failed'), 'invalid outcome status')
    if 'verified' in outcome:
        require(outcome['verified'] is False, 'verified must be false; diagnosis is not parity')
    if status == 'retired':
        require(outcome.get('verified') is False, 'retired outcome requires verified=false')
        require(type(outcome.get('retiredFrames')) is int
                and outcome['retiredFrames'] == start['frameCount'], 'retiredFrames != frameCount')
        require('error' not in outcome, 'retired outcome contains error')
    else:
        require(isinstance(outcome.get('error'), str) and outcome['error'].strip(),
                'failed outcome requires nonempty error')
        if 'retiredFrames' in outcome:
            integer(outcome['retiredFrames'], 0, start['frameCount'], 'retiredFrames')
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dirs', nargs='+', type=Path, help='explicit diagnostic run directories')
    args = parser.parse_args(argv)
    counts = {'retired': 0, 'failed': 0, 'invalid': 0}
    seen = set()
    for directory in args.dirs:
        try:
            resolved = directory.resolve()
            require(resolved not in seen, 'duplicate run directory')
            seen.add(resolved)
            status = audit_directory(directory)
            counts[status] += 1
            print(f'{directory}: consistent diagnostic record; {status}')
        except (OSError, ValueError, RecursionError) as error:
            counts['invalid'] += 1
            print(f'{directory}: INVALID: {error}')
    print(f"Unscored records: {counts['retired']} retired, {counts['failed']} failed, "
          f"{counts['invalid']} invalid. Recorded failures are diagnostic evidence, not gate passes.")
    print('Structural audit only; no parity, performance, reliability, or milestone gate certified.')
    return 1 if counts['invalid'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
