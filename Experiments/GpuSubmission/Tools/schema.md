# GPU submission evidence schema

**Status**: Implemented

This version-1 stdlib driver implements the paired protocol in the
[experiment design](../../../docs/specs/2026-09-06-m5.6-gpu-work-submission-design.md).
The native CLI writes each measurement pair; the driver validates, inventories and reduces it.
No result in this native-host report alone establishes a production RHI speedup.

## Commands and output ownership

```sh
python3 Experiments/GpuSubmission/Tools/paired.py --bench /absolute/GpuSubmissionBench --output /external/evidence
python3 Experiments/GpuSubmission/Tools/paired.py --bench /absolute/GpuSubmissionBench --output /external/evidence --resume
python3 Experiments/GpuSubmission/Tools/paired.py --summarize /external/evidence --output /external/report
python3 Experiments/GpuSubmission/Tools/test_paired.py
```

`--validation /external/validation.json` imports completed CLI verification on initial collection,
including artifacts named in its gate references, preserving their relative paths in the bundle.
`--max-pairs N` bounds a smoke session; missing pairs make its report incomplete and unscored.
`--timeout SECONDS` defaults to 600 per process. There is no adjustable scored matrix, pair count,
warmup, replay length, bootstrap seed or loss threshold. Run the driver with Python 3.11 or newer.

Output must be fresh or empty, outside the source tree. The native pair output path must not yet
exist. All binary/output paths resolve before changing working directory. Each pair is one fresh
process containing two drained variant runs. The driver invokes the benchmark and macOS `pmset`
directly with argument arrays and no shell; RTK is not a runtime dependency. Discovery invokes
`--list-cases` and `--capabilities` before collection.
The complete plan includes unavailable ICB combinations so their absence stays visible.

`--resume` continues only never-attempted jobs. It does not rerun failed, interrupted, invalid or
completed jobs; replacing a failure with a favorable repetition would bias the sample. A collection
with such a failure requires a new output bundle for a new scored collection. Resume rejects changes
to the executable, analysis driver, schema, inventories or validation/capture flags. Cross-pair
shader, protocol, environment, device and per-case manifest mismatches invalidate collection.
Do not change the frozen shaders/configuration during a session; changes require recollection.

## Discovery and collection

`cases.json` is the CLI's bare array of exactly twenty objects in canonical order. Each object has
`id`, `count`, `triangles`, `visibleFraction`, `bins`. IDs are `n<N>-t<T>-v<percent>-b<B>`.
The core matrix is N=1024/16384/65536, T=2/32, visibility=0.1/0.5/1, B=1; the last two
cases are N=16384, T=2, visibility=0.5, B=16/64. These are fixed numeric inputs, not inferred IDs.

`capabilities.json` contains `schemaVersion: 1`, `modes` (unique canonical strings),
`gpuSpan: "verified" | "unavailable"`, and `icb: {status, reason}`. ICB status is verified,
unsupported, unresolved or unavailable; only verified ICB may appear in `modes`.
Direct must be available. Unsupported counters are never successful zero timings.

`collection.json` freezes `benchHash`, `driverHash`, `schemaHash`, `casesHash`, `capabilitiesHash`,
`validationHash` and `validationArtifacts` (per-reference file or directory-content hashes),
actual validation/capture `flags`, seed=1280137270 (`0x4C4D5836`), pairs=12, warmup=32,
frames=256 and resamples=10000, plus descriptive executable path and creation time. Hashes are
lowercase SHA-256 over exact file bytes. `schemaVersion` is 1 throughout.

The job plan is deterministic: case, suite S/E, lane headline/gpu-span, comparison, repetition.
Comparisons are cpu-indirect/direct, gpu-args/direct, batched/direct, gpu-icb/direct,
gpu-args/batched, gpu-icb/batched. Repetitions 0..11 alternate AB/BA. IDs `p00000` onward are
unique across the bundle. There are 5,760 planned pairs including unavailable modes/lanes;
with four supported modes and verified GPU spans, 3,840 processes execute. GPU-span unavailable
skips that lane explicitly; no unverified timestamp is treated as a workload span.

Each `pairs/<pair-id>/` contains immutable `job.json`, either `skip.json` or a `process/` directory,
and the native `output/` if created. `process/command.json`, `stdout.log`, `stderr.log`, and
`receipt.json` retain command, exit status/error and raw process output. Missing receipts identify
interruptions. Native `failure.json` remains untouched and invalidates a purported success.
Known-unavailable jobs have `{status: "unavailable", reason}` in `skip.json`.

## Measurement pair

Native `output/result.json` fields are:

| Field | Contract |
|---|---|
| schemaVersion | Integer 1 |
| case | Exact resolved case object from the frozen inventory |
| suite, lane | S/E and headline/gpu-span; stage diagnostics cannot enter scored collection |
| pair | Candidate,control in that order, independent of execution order |
| order | AB or BA; A is candidate and B is control |
| warmup, frameCount | Exactly 32 and 256 |
| manifestHash | SHA-256 of adjacent native `manifest.json` bytes |
| shaderHash, executableHash, protocolHash | Frozen native identities, equal across all pairs |
| environmentHash | Native invariant environment identity, equal across all pairs |
| environment | Actual CLI host/configuration record, validation/capture false |
| runs | Exactly two objects, in stated AB/BA execution order |

Each run has `variant`, `frames`, positive completed-frame `throughput` (frames/second), nonempty
`device`, `gpuSpanStatus`, nonnegative `setupMs` and `drainMs`, `requestedBytes`, nullable
`allocatedBytes`, nullable `residentBytes`. Setup is outside steady state; final drain is included
in throughput. Actual `environment.shaderValidation` must be false for measurement. Environment
hash is verified against the CLI's exact compact JSON encoding, including control escapes.
Requested bytes cover the whole three-slot variant working set and cannot exceed 268,435,456.
Requested, allocated and resident are distinct labels; a requested sum is never physical memory.
An ICB adopter also needs measured `opaqueIcbAllocatedBytes`; absence defers its memory gate.

Each frame contains integer `frame` in exactly the set 0..255, positive integer `cpuWorkNs`,
nonnegative integer `waitNs`, `drawCalls`, `copiedBytes`, and explicitly nullable `gpuSpanMs`,
`preparationMs`, `rasterMs`. Headline has all GPU metrics null. GPU-span requires verified,
positive `gpuSpanMs` for every frame and null stage metrics. Duplicate/missing IDs, timings,
fields, hash mismatches, invalid/NaN/infinite numbers, and partial pairs invalidate the cell.
JSON duplicate keys are rejected. Neither zero denominators nor absent fields become null silently.

The current CLI exposes retired logical frame IDs through `frame`; the driver validates that
complete set but does not claim to prove native event ordering from these IDs. The CLI retirement
gate and lifetime stress provide that separate evidence. Normalized JSONL adds globally unique
`pairId` and `runId` (`<pair-id>:<variant>`) alongside case, suite, lane, variant and manifest hash.

## Environment telemetry

`environment.json` records actual `pmset -g batt` and `pmset -g therm` observations immediately
before and after the first collection session, outside scored subprocesses. Each explicit resume
adds `sessions/sNNNN/environment.json`; `sessions/sNNNN/pre.json` preserves a pre-snapshot even
after interruption. Each snapshot includes `observedAt`, power and thermal records with command,
raw value, stdout, stderr, exitCode, and error. Failure/unavailable is `value: null` with an error.
There is no inferred power source or thermal state. The direct `pmset` argv is the saved command.

Telemetry is descriptive, can change between pairs, and is not part of the CLI's invariant
`environmentHash`. The driver does not fill the CLI's null power/thermal fields using unrelated
timestamps, and does not discard runs based on favorable/unfavorable telemetry. Actual Metal
validation/capture/shader-validation environment flags are retained separately and enabled flags
prevent scored collection. Per-session snapshots complement, rather than replace, CLI environment.

## Validation and decision gates

The optional root `validation.json` is a completed CLI result: `schemaVersion`, `shaderHash`,
`executableHash`, `protocolHash`, `environment`, and `results`. Each results row contains exact
`case` ID, suite, variant, `frames >= 256`, matching `manifestHash`, and true `reference`,
`scoredReplay`, `retirement`. Verification `environment.validation` must be true, recording
actual `MTL_DEBUG_LAYER` enablement. All supported modes, twenty cases and both suites must be covered
exactly once. A partial progress file is not completed verification. Validation may enable Metal
validation, so its environment is not required to hash like the scoring environment.

The native CLI's existing rows do not establish capture, 900-frame lifetime stress or protocol
freeze. Absent additional gate evidence conservatively defers recommendation. Main integration may
attach a `gates` object to verification evidence with these exact keys:

```json
{
  "capture": {"status": "verified", "reason": "Inspected empty/sparse/dense captures", "references": ["captures/index.json"]},
  "lifetimeStress": {"status": "verified", "frames": 900, "reason": "Slot canaries, growth and teardown passed", "references": ["lifetime/log.txt"]},
  "measurementFreeze": {"status": "verified", "reason": "Protocol, boundaries and observer overhead audited before scoring", "references": ["freeze.json"]},
  "checkpointA": {"status": "verified", "reason": "Frozen checkpoint passed under Metal validation", "references": ["checkpoint-a.log"]}
}
```

Other statuses are failed, unavailable and unresolved. Verified gates require a nonempty reason
and evidence-reference list; stress also requires at least 900 frames. Every reference must resolve
to an existing artifact inside the bundle; absolute paths, traversal and escaping symlinks fail.
The driver checks presence, not the meaning of GPU captures or checkpoint logs. Report authors
must inspect that evidence; no gate is inferred from a timing win. Missing checkpoint A defers.

## Reduction and reports

For each run reduce CPU work, waits and GPU spans to medians; retain its one measured throughput.
For each cell compute twelve paired relative improvements: `100*(control-candidate)/control`
for durations and `100*(candidate-control)/control` for throughput. Report their median and a
two-sided percentile 95% interval from 10,000 bootstrap resamples of twelve complete pairs,
Python stdlib Random seed `0x4C4D5836`, linearly interpolated 2.5/97.5 percentiles. Frames are never
independent repetitions. Every outlier remains in raw evidence; an invalid repetition invalidates
the entire cell rather than reducing its denominator. Global identity changes invalidate all cells.

Only headline CPU/throughput and verified GPU-span GPU duration supply decision metrics. A win
needs median >=15% and lower interval bound >0. Adoption candidates are gpu-args and verified
gpu-icb only. In E, the same metric must win against both direct and batched at two adjacent N
scales holding T/B/visibility fixed. Both controls at both scales must have GPU-span and throughput
lower bounds strictly above -15%; even uncertain regressions or exactly -15 block adoption.
Unavailable guards, incomplete collection or missing/failed gates defer. Otherwise no qualifying
region retains incumbents. Adoption is bounded to listed regions, never a blanket default.

Every report writes deterministic `summary.json`, `summary.jsonl`, `summary.csv`, `summary.md`,
`runs.csv`, `pairs.jsonl`, and `frames.jsonl`. Run CSV records medians for CPU work, throughput,
waits, calls and setup/drain, plus high-water storage from the twelve runs. `summary.json` also
records descriptive marker-enabled throughput loss relative to headline for each variant/pair;
it is not an adoption metric or a pooled interval, and unavailable lane comparisons are null.
Pair records preserve reduced counts/storage/waits and reasons;
full native frames/manifests/failures remain in the collection bundle. JSON/CSV unsupported metrics
are explicit null (CSV literal `null`). Reports list all cells, intervals, gates and recommendations.
Re-summarizing an unchanged bundle with the frozen driver into another fresh directory yields
byte-identical reports; collection timestamps and new telemetry are not generated by summarization.
Exit 0 means a complete inventory of valid/unavailable pairs, not adopted results. Exit 2 means
invalid input, failed/incomplete collection or output collision. Missing capability may legitimately
produce exit 0 and a defer recommendation.
