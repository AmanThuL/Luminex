# GPU submission experiment

An isolated Metal 4 workload executor and a separate public-RHI correctness reference. This
directory is experimental evidence, not renderer code. See the
[accepted design](../../docs/specs/2026-09-06-m5.6-gpu-work-submission-design.md),
[native contracts](Metal/README.md), and [result schema](Tools/schema.md).

**2026-09-06 safety stop:** the first formal no-verify collection triggered GPU global restarts
and retirement assertions. It is incomplete and no performance finding is accepted. Do not run
the measurement commands below on a shared GPU without arranging an isolated test session/host.
Pre-collection verification passed with readback-altered scheduling; that does not certify the
uninterrupted measurement path. See the [attempt record](../../docs/research/2026-09-06-gpu-submission-evidence.md).
CPU selftests, report regeneration and evidence indexing do not submit GPU work.

## Bounded diagnosis

After arranging a GPU test session, `--diagnose` accepts one explicit case, suite and ordinary
variant, at most 32 warmup and 900 replay frames. It logs completion failures without the
verification path's synchronous readback. Timing is unscored, and success means retirement only.
It cannot be combined with `--measure`, captures or timestamp lanes. For example:

```sh
GpuSubmissionBench --diagnose --case n16384-t32-v50-b1 --suite S --variant gpu-args --frames 256 --output <fresh-directory>
```

The first controlled follow-up reproduced a queue timeout with API/shader validation off in this
single-mode path, even though the fully validated run retired. Stop after a reset and retain the
diagnostic artifacts; enabling validation is not a proven fix. The
[follow-up record](../../docs/research/2026-09-06-gpu-submission-diagnostic.md) states the scope.

`--diagnostic-dependency declared|all` is accepted only with `--diagnose`; `all` additionally
requires `gpu-args`. It expands the dispatch consumer mask to all stages without changing the
measured default. Both diagnostic JSON files record the choice. This also orders later passes
on the queue, not just indirect argument consumption. The
[dependency follow-up](../../docs/research/2026-09-06-gpu-submission-dependency-diagnostic.md)
observed retirement and timeout with `all`; this is a discriminator, not a correction.

Selective shader-validation diagnostics retain the exact six allowlisted selection/reporting
environment values in `diagnosticShaderValidationEnvironment` (unset is null). Native diagnostic
setup also logs each pipeline's queried `shaderValidation` state: 0 default, 1 enabled, 2 disabled.
Do not infer actual instrumentation from requested flags alone. The CPU `--selftest` prints the
same environment serialization before its success line, without creating a Metal device.
The [instrumentation record](../../docs/research/2026-09-06-gpu-submission-instrumentation-diagnostic.md)
retains the four configurations and a later API-only/all-off pair. The latter failed with API
validation off despite an API-only retirement pass; validation is not an adopted correction.

## Build and validate

Use the pinned dependencies from `xmake setup`, an Apple Silicon Metal 4 device, and release mode.
The five experiment targets are non-default. Generated Scene/Prepare MSL is retained under each
binary's own `Shaders` directory; runtime MSL compilation works without the optional Metal
Toolchain. Neither fetched scenes nor the editor are needed by these binaries.

```sh
xmake f -m release -P .
xmake build GpuSubmissionBench
xmake build GpuSubmissionTests
build/macosx/arm64/release/gpu-submission-tests/GpuSubmissionTests '~[gpu]'
MTL_DEBUG_LAYER=1 build/macosx/arm64/release/gpu-submission-tests/GpuSubmissionTests '[gpu]'
python3 Experiments/GpuSubmission/Tools/test_paired.py
python3 Experiments/GpuSubmission/Tools/test_assemble_validation.py
python3 Experiments/GpuSubmission/Tools/test_cli.py --bench build/macosx/arm64/release/gpu-submission/GpuSubmissionBench
xmake project -k compile_commands -P .
python3 Experiments/GpuSubmission/Tools/check_experiment.py
```

The experiment checker explicitly applies formatting, file envelopes, compiler-backed public
documentation and function separators; production checker source globs are not silently extended.
Production App/Tests and checkpoint A must still pass independently.

## Evidence workflow

Use an absolute binary path and an external evidence directory below. Each CLI output directory
must be new. Case IDs come from `--list-cases`; `empty`, `single`, and `tail` are correctness-only
aliases and never enter the scored twenty-case matrix.

```text
<bench> --selftest
<bench> --capabilities --output <evidence>/capabilities
MTL_DEBUG_LAYER=1 <bench> --verify --suite all --output <evidence>/replay
MTL_DEBUG_LAYER=1 <bench> --verify --case n16384-t2-v50-b64 --suite E --frames 900 --output <evidence>/stress
MTL_DEBUG_LAYER=1 MTL_CAPTURE_ENABLED=1 <bench> --verify --case n1024-t2-v10-b1 --suite E --variant gpu-args --frames 3 --capture <evidence>/sparse.gputrace --output <evidence>/capture-sparse
```

Capture `empty` and `n1024-t2-v100-b1` separately as well. Captures contain one retired measured
frame; the surrounding short replay is unscored. Inspect the trace, its native `.capture.json`
sidecar and validation logs. `Tools/inspect_capture.py` recovers labels/targets and checks GPU
arguments against the resolved manifest when exact size plus the changing slot guard identifies
a unique blob. It reports ambiguity rather than assigning a same-sized buffer by guesswork.
Its buffer attribution is not a capture-format guarantee; retain both the raw trace and review.

Assemble the complete CLI replay, 900-frame stress, validated checkpoint log, explicit capture
reviews and measurement-freeze record with `Tools/assemble_validation.py --help`. Its module
documentation defines the review JSON fields. Missing evidence does not become a successful gate.
It writes a new `validation.json` into the staging bundle; do not change an existing collection's
validation evidence after its hashes have been frozen.

```text
python3 Experiments/GpuSubmission/Tools/paired.py --bench <absolute-bench> --validation <evidence>/validation.json --output <new-collection>
python3 Experiments/GpuSubmission/Tools/paired.py --summarize <collection> --output <new-report>
```

The driver runs sequential fresh-process AB/BA pairs, never simultaneous timed GPU jobs. It
retains every supported case, unavailable lane, process failure and raw frame. `--max-pairs N`
is an explicitly incomplete/unscored smoke; `--resume` fills only missing jobs and never replaces
a failed or unfavorable attempt. A changed binary, shader, manifest, driver, schema or frozen
validation invalidates comparisons instead of mixing populations. Preserve invalidated attempts
and collect into a different directory after a correction.

Headline CPU work and sustained throughput have no GPU markers. GPU-span and stage timings are
unavailable because their workload boundaries have not been proven. An unavailable GPU-span guard
defers GPU-generation adoption even if headline timings improve. The unresolved ICB route has a
separate bounded probe; it is not silently emulated by N indirect draw calls.

The canonical workload serializes phase-zero float32 transforms once, fixed bin ranges, camera,
geometry, colors and exact phase permutations, plus eight diagnostic frame hashes. Each raw pair
also carries a cryptographic SHA256 of that complete manifest, shader artifacts and executable.
All measured frames still copy the changing transforms; manifest deduplication is not a timed
input-reuse optimization.

Do not run other GPU workloads, validation, capture or builds during collection. Record power and
thermal observations before/after; the driver preserves `pmset` output without pretending it is
continuous thermal sampling. GPU buffers' allocated sizes and allocator high water are distinct
from requested CPU/GPU storage and from unavailable resident memory. See the native contract for
unqueried opaque allocations, pool cleanup and allocator-query observer overhead.

At closure, `Tools/archive_evidence.py` writes an exclusive SHA256 inventory of an external bundle.
Preserve source and commands at the immutable evidence tag; keep experiments off the production
branch and carry back only the research conclusion, ADR and milestone record.
