#!/usr/bin/env python3
"""Assemble audited CLI evidence into an existing evidence staging root.

Usage (all source paths are relative to --bundle):
  --bundle ROOT --verify evidence/main/validation.json
  --stress evidence/stress/validation.json --checkpoint evidence/checkpoint-a.log
  --capture-index evidence/captures.json --freeze evidence/measurement-freeze.md
Repeat --verify for disjoint scoped CLI runs and --stress for disjoint stress runs.
Copy external evidence into ROOT/evidence before invoking this command. Only
ROOT/validation.json is written, exclusively; nothing is copied or overwritten.
Then run paired.py --validation ROOT/validation.json --output FRESH_COLLECTION
with its other collection arguments. The driver imports the referenced evidence
and freezes its hashes. Do not add evidence to an already-frozen collection.

The capture index is JSON: schemaVersion=1, shaderHash/executableHash/protocolHash,
environment.validation=true, and captures containing exactly empty/sparse/dense.
Each capture has case, suite, variant, reviewer, reviewedAt (ISO timestamp with zone),
reviewed=true, validationClean=true, generatedWorkReviewed=true,
bindingsReviewed=true, residencyReviewed=true, labelsReviewed=true, and references
(existing bundle-relative artifacts, including the .gputrace and review notes).
These are explicit reviewer attestations, never conclusions from file existence.
A reviewer may be identified as "Codex (artifact/source inspection)"; no human
review gate is required. Mark facts true only when the inspection establishes them;
unknown bindings or other unproven facts cannot satisfy the corresponding gate.

The Markdown freeze document has exactly one fenced json block: schemaVersion=1,
the same three hashes, status="frozen", reviewed=true, reviewer, reviewedAt,
configurationFrozen=true, algorithmsFrozen=true, shadersFrozen=true,
schemasFrozen=true, counterPlacementFrozen=true, observerOverheadReviewed=true,
and references to existing bundle-relative artifacts supporting the review.

Main verification must cover exactly 20 cases x 2 suites x 4 ordinary modes, each
with >=256 verified frames and matching resolved manifest contents. Stress must
cover all four modes in E for one changing-visibility representative case (a
matrix point or tail), each with >=900 frames; optional S rows are also checked.
Checkpoint evidence must include the exact Catch2 checkpoint-a filter, a positive
All tests passed summary, and Metal API validation's enabled message.
Missing, failed, unsafe, inconsistent or unreviewed evidence aborts without output.
"""

import argparse
from datetime import datetime
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys

import paired

ORDINARY = ('direct', 'cpu-indirect', 'gpu-args', 'batched')
IDENTITIES = ('shaderHash', 'executableHash', 'protocolHash')
CAPTURE_FACTS = ('reviewed', 'validationClean', 'generatedWorkReviewed',
                 'bindingsReviewed', 'residencyReviewed', 'labelsReviewed')
FREEZE_FACTS = ('reviewed', 'configurationFrozen', 'algorithmsFrozen',
                'shadersFrozen', 'schemasFrozen', 'counterPlacementFrozen',
                'observerOverheadReviewed')
RESERVED = {'validation.json', 'collection.json', 'cases.json', 'capabilities.json',
            'environment.json', 'discovery', 'sessions', 'pairs', 'reports'}
require = paired.require


def artifact(root, relative, directory=False):
    """Reject traversal, links and special files, including inside capture packages."""
    require(isinstance(relative, str) and relative and '\\' not in relative
            and ':' not in relative and '\x00' not in relative, 'invalid artifact path')
    path = PurePosixPath(relative)
    require(not path.is_absolute() and all(p not in ('', '.', '..')
            for p in relative.split('/')), 'unsafe artifact path: ' + relative)
    current = root
    for part in path.parts:
        current = current / part
        require(not current.is_symlink(), 'symlink artifact: ' + relative)
    require(current.resolve(strict=True).is_relative_to(root), 'artifact escapes bundle')
    mode = current.stat().st_mode
    if stat.S_ISDIR(mode) and directory:
        require(any(current.iterdir()), 'empty artifact directory: ' + relative)
        files = 0
        for parent, dirs, names in os.walk(current, followlinks=False):
            for name in dirs + names:
                child = Path(parent) / name
                info = child.lstat()
                require(stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode),
                        'linked or special capture artifact: ' + str(child))
                files += stat.S_ISREG(info.st_mode)
        require(files > 0, 'capture directory has no files: ' + relative)
    else:
        require(stat.S_ISREG(mode) and current.stat().st_size > 0,
                'artifact must be a nonempty regular file: ' + relative)
    return current


def schema(data):
    require(type(data) is dict and type(data.get('schemaVersion')) is int
            and data['schemaVersion'] == 1, 'invalid evidence schema')


def identity(data, expected=None):
    result = {key: paired.sha(data.get(key), key) for key in IDENTITIES}
    if expected is not None:
        require(result == expected, 'shader/executable/protocol identity mismatch')
    return result


def validated_environment(data):
    require(type(data.get('environment')) is dict
            and data['environment'].get('validation') is True,
            'evidence environment.validation must be true')


def cases():
    result = {case['id']: case for case in paired.frozen_cases()}
    for name, count, fraction in (('empty', 0, 0), ('single', 1, 1), ('tail', 257, 0.5)):
        result[name] = dict(id=name, count=count, triangles=2, bins=1,
                            visibleFraction=fraction)
    return result


def read_runs(root, sources, minimum, expected_identity=None):
    indexed, manifests, references = {}, {}, set()
    environments = []
    known = cases()
    for source in sources:
        path = artifact(root, source)
        require(path.name == 'validation.json', 'use final CLI validation.json, not progress')
        require(not (path.parent / 'failure.json').exists(), 'CLI failure record exists')
        data = paired.load(path)
        schema(data)
        observed = identity(data, expected_identity)
        expected_identity = observed
        validated_environment(data)
        environments.append(data['environment'])
        rows = data.get('results')
        require(type(rows) is list and rows, 'missing verification results')
        references.add(source)
        for row in rows:
            require(type(row) is dict, 'invalid verification row')
            case = row.get('case')
            require(isinstance(case, str) and case in known, 'unknown verification case')
            require(row.get('suite') in ('S', 'E') and row.get('variant') in ORDINARY,
                    'unknown suite or mode')
            key = (case, row['suite'], row['variant'])
            require(key not in indexed, 'duplicate verification row: ' + repr(key))
            require(type(row.get('frames')) is int and minimum <= row['frames'] <= 0xffffffff,
                    'invalid or incomplete verified frames')
            require(all(row.get(field) is True for field in
                        ('reference', 'scoredReplay', 'retirement')), 'failed verification row')
            manifest_ref = (PurePosixPath(source).parent / (case + '-manifest.json')).as_posix()
            manifest_path = artifact(root, manifest_ref)
            manifest_hash = paired.digest(manifest_path)
            require(paired.sha(row.get('manifestHash'), 'manifestHash') == manifest_hash,
                    'manifest content hash mismatch')
            manifest = paired.load(manifest_path)
            schema(manifest)
            require(manifest.get('valid') is True, 'invalid resolved manifest')
            for field, value in known[case].items():
                require(manifest.get(field) == value, 'resolved manifest differs: ' + field)
            for field in ('count', 'triangles', 'bins'):
                require(type(manifest[field]) is int, 'invalid manifest integer: ' + field)
            paired.number(manifest['visibleFraction'], 'visibleFraction')
            require(case not in manifests or manifests[case] == manifest_hash,
                    'resolved manifest changed between scopes')
            manifests[case] = manifest_hash
            indexed[key] = row.copy()
            references.add(manifest_ref)
    require(indexed, 'no CLI verification bundles supplied')
    return indexed, manifests, expected_identity, environments, references


def review(data, facts):
    require(all(data.get(field) is True for field in facts), 'missing or failed review facts')
    require(isinstance(data.get('reviewer'), str) and data['reviewer'].strip(),
            'reviewer must be explicit')
    require(isinstance(data.get('reviewedAt'), str), 'review timestamp absent')
    stamp = datetime.fromisoformat(data['reviewedAt'].replace('Z', '+00:00'))
    require(stamp.utcoffset() is not None, 'review timestamp must include timezone')


def review_references(root, data):
    refs = data.get('references')
    require(type(refs) is list and refs and all(isinstance(ref, str) for ref in refs)
            and len(set(refs)) == len(refs), 'review requires distinct artifact references')
    for ref in refs:
        artifact(root, ref, directory=True)
    return refs


def capture_gate(root, source, expected):
    data = paired.load(artifact(root, source))
    schema(data)
    identity(data, expected)
    validated_environment(data)
    entries = data.get('captures')
    require(type(entries) is list and len(entries) == 3, 'need empty, sparse and dense captures')
    kinds, refs, trace_refs = set(), {source}, set()
    known = cases()
    for entry in entries:
        require(type(entry) is dict, 'invalid capture review')
        kind = entry.get('kind')
        require(kind in ('empty', 'sparse', 'dense') and kind not in kinds,
                'duplicate or invalid capture kind')
        kinds.add(kind)
        require(entry.get('case') in known and entry.get('suite') in ('S', 'E')
                and entry.get('variant') in ORDINARY, 'invalid capture scope')
        spec = known[entry['case']]
        fraction = spec['visibleFraction']
        actual_kind = 'empty' if spec['count'] == 0 or fraction == 0 else (
            'dense' if fraction == 1 else 'sparse')
        require(kind == actual_kind, 'capture kind disagrees with case')
        review(entry, CAPTURE_FACTS)
        artifacts = review_references(root, entry)
        traces = {ref for ref in artifacts if PurePosixPath(ref).suffix == '.gputrace'}
        require(traces and not trace_refs.intersection(traces),
                'capture needs its own .gputrace artifact')
        require(any(PurePosixPath(ref).suffix in ('.md', '.txt', '.json')
                    and ref != source for ref in artifacts), 'capture review notes absent')
        trace_refs.update(traces)
        refs.update(artifacts)
    return dict(status='verified', reason='Explicit empty/sparse/dense capture reviews',
                references=sorted(refs), captures=entries)


def checkpoint_gate(root, source):
    log = artifact(root, source).read_text(encoding='utf-8')
    log = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', log)
    filters = re.findall(r'^Filters:\s*(.*?)\s*$', log, re.MULTILINE)
    require(len(filters) == 1 and filters[0].strip('"') == '[checkpoint-a]',
            'checkpoint log does not prove the exact checkpoint-a filter')
    passed = re.findall(r'^All tests passed \((\d+) assertions? in (\d+) test cases?\)\s*$',
                        log, re.MULTILINE)
    require(len(passed) == 1 and all(int(value) > 0 for value in passed[0]),
            'checkpoint log lacks a positive All tests passed summary')
    require('Metal API Validation Enabled' in log, 'checkpoint Metal validation not confirmed')
    require(not re.search(r'\bfailed\b|\bSIG(?:ABRT|SEGV)\b|\berror\s*:|'
                          r'validation error|no test cases matched|no tests ran', log, re.I),
            'checkpoint log contains failure diagnostics')
    return dict(status='verified', reason='Validated checkpoint-a Catch2 run passed',
                assertions=int(passed[0][0]), testCases=int(passed[0][1]), references=[source])


def freeze_gate(root, source, expected):
    path = artifact(root, source)
    require(path.suffix == '.md', 'measurement freeze must be a Markdown document')
    blocks = re.findall(r'^```json\s*\n(.*?)^```\s*$', path.read_text(encoding='utf-8'),
                        re.MULTILINE | re.DOTALL)
    require(len(blocks) == 1, 'freeze document needs exactly one fenced json record')
    data = paired.loads(blocks[0])
    schema(data)
    identity(data, expected)
    require(data.get('status') == 'frozen', 'measurement configuration is not frozen')
    review(data, FREEZE_FACTS)
    refs = review_references(root, data)
    require(source not in refs, 'freeze document cannot cite itself as its evidence')
    return dict(status='verified', reason='Explicit reviewed measurement freeze',
                references=sorted({source, *refs}), review=data)


def assemble(bundle, verify, stress, checkpoint, capture_index, freeze):
    root = Path(bundle).resolve(strict=True)
    require(root.is_dir(), '--bundle must be an existing evidence directory')
    require(not root.is_relative_to(Path(__file__).resolve().parents[3]),
            'evidence must be outside the source tree')
    output = root / 'validation.json'
    require(not output.exists() and not output.is_symlink(), 'validation.json already exists')
    rows, manifests, identities, environments, refs = read_runs(root, verify, 256)
    expected = {(case['id'], suite, mode) for case in paired.frozen_cases()
                for suite in ('S', 'E') for mode in ORDINARY}
    require(set(rows) == expected, 'incomplete or extra 20-case/2-suite/4-mode verification scope')
    stress_rows, stress_manifests, _, _, stress_refs = read_runs(root, stress, 900, identities)
    stress_cases = {key[0] for key in stress_rows}
    require(len(stress_cases) == 1, 'stress must name one representative case')
    stress_case = next(iter(stress_cases))
    require(0 < cases()[stress_case]['visibleFraction'] < 1,
            'stress must exercise changing visibility')
    require(all((stress_case, 'E', mode) in stress_rows for mode in ORDINARY),
            'stress lacks an E replay for every ordinary mode')
    for case, value in stress_manifests.items():
        require(case not in manifests or manifests[case] == value,
                'stress and main manifest mismatch')
    collection = root / 'collection.json'
    if collection.exists():
        config = paired.load(artifact(root, 'collection.json'))
        require(config.get('benchHash') == identities['executableHash'],
                'collection executable hash mismatch')
        require('validationHash' not in config,
                'collection already froze validation; assemble in a staging bundle and import '
                'with paired.py --validation into a fresh collection')
    gates = dict(
        validation=dict(status='verified', reason='Complete audited CLI verification sources',
                        references=sorted(refs)),
        checkpointA=checkpoint_gate(root, checkpoint),
        capture=capture_gate(root, capture_index, identities),
        measurementFreeze=freeze_gate(root, freeze, identities),
        lifetimeStress=dict(status='verified',
                            reason='CLI verified changing-visibility E replay in all four modes',
                            frames=min(row['frames'] for row in stress_rows.values()),
                            case=stress_case, references=sorted(stress_refs),
                            results=[stress_rows[key] for key in sorted(stress_rows)]))
    refs.update(stress_refs)
    for gate in gates.values():
        refs.update(gate['references'])
    sources = []
    for ref in sorted(refs):
        require(PurePosixPath(ref).parts[0] not in RESERVED,
                'artifact reference collides with paired driver output')
        path = artifact(root, ref, directory=True)
        sources.append(dict(path=ref, sha256=paired.digest(path) if path.is_file() else None,
                            kind='file' if path.is_file() else 'directory'))
    data = dict(schemaVersion=1, **identities, environment=environments[0],
                verificationEnvironments=environments, results=[rows[key] for key in sorted(rows)],
                gates=gates, sources=sources)
    payload = (paired.encoded(data) + '\n').encode('utf-8')
    with output.open('xb') as stream:
        stream.write(payload)
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--verify', required=True, action='append')
    parser.add_argument('--stress', required=True, action='append')
    parser.add_argument('--checkpoint', required=True)
    parser.add_argument('--capture-index', required=True)
    parser.add_argument('--freeze', required=True)
    args = parser.parse_args(argv)
    try:
        output = assemble(args.bundle, args.verify, args.stress, args.checkpoint,
                          args.capture_index, args.freeze)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print('validation assembly failed: ' + str(error), file=sys.stderr)
        return 1
    print('Assembled ' + str(output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
