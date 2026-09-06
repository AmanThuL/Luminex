# GPU submission: selective shader instrumentation

**Status**: Frozen — non-normative

**Date:** 2026-09-06. Follow-up to the
[dependency comparison](2026-09-06-gpu-submission-dependency-diagnostic.md), on the same
user-authorized Mac. The initial four bounded fresh processes retired without a new GPU reset;
a subsequent API-only/all-off comparison reproduced the timeout with all validation off.
No root-cause correction, accepted performance result or milestone closure is established.

## Fixed-length comparison

All runs used one executable, shader bundle and manifest; gpu-args/S,
N=16,384/T=32/visibility=0.5/B=1; 32 warmup plus 900 replay frames; three slots;
the original dispatch → vertex|fragment dependency, with Device visibility.
API validation and the global shader-validation framework remained enabled. Capture was off.
Only the pipeline-enable list changed, with default state `none` and an empty disable list.

| Order | Selected instrumentation | Queried raster / prepare state | Outcome |
|---|---|---|---|
| 1 | Both pipelines | 1 / 1 | 900 replay frames retired |
| 2 | Prepare only | 2 / 1 | 900 replay frames retired |
| 3 | Raster only | 1 / 2 | 900 replay frames retired |
| 4 | Neither pipeline | 2 / 2 | 900 replay frames retired |

The installed SDK defines the pipeline-state query as its current shader-validation state;
the pinned enum maps enabled to 1 and disabled to 2 (default is 0). These are queried states,
not a conclusion drawn only from requested environment flags. All runs exited 0, with no
reported validation error. Kernel queries from 18:55:40 Asia/Shanghai through the completed
fourth run found no GPURestartBegin or MetalError. Pipeline UID queries returned no matching
records; the effective-state log is the evidence for selection, not an assumed UID dump.

The fourth process completed despite a conversational interruption; its final JSON and exit
status were recovered before doing further work. It was not restarted or counted twice.

## Interpretation

This bounded run set did not distinguish producer versus consumer shader sensitivity. Shader
instrumentation of either pipeline was not necessary for the fourth observed retirement pass.
It does not establish that disabling instrumentation is a fix: the API/debug framework remained
active, observer logging/callbacks affect scheduling, and prior unvalidated runs also sometimes
retired. Four passes do not establish a failure rate or erase the earlier resets.

Apple's [shader-validation guide](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage)
describes instrumentation's effects. This session explicitly used `zerofill`: invalid reads
can become zero and invalid writes can be dropped. No reported validation error is not a full
correctness proof. Diagnostics perform no image/argument readback and accept no timing results.

## API validation follow-up

After the user's continuation, two further fresh processes used the same snapshot and requested
32+900 frames, with shader validation globally disabled and both pipeline states queried as 2/2.
The only explicit setting changed between these two requests was MTL_DEBUG_LAYER (1 → 0).
This pair ran later, around 22:19, rather than adjacent to the initial four runs around 18:55–19:02;
ambient machine conditions across that gap are not treated as controlled.

| API validation | Global shader validation | Outcome |
|---|---|---|
| On | Off | 900 replay frames retired; no reported validation error/reset |
| Off | Off | Timeout during warmup; replay never began |

The all-off failure again identifies submission 2, slot 1, frame 1, with timeout callbacks for
submissions 3/4 as well. The kernel reports GPURestartBegin at 22:19:59.464 Asia/Shanghai.
The process returned exit1 and failure JSON; no further GPU work was attempted after the reset.

This localizes an observed sensitivity to API-validation state without requiring shader
instrumentation. It is one ordered pair, not a reproducible failure-rate estimate or proof of
what API validation changes internally. Prior all-off passes remain relevant. Neither a missing
application dependency nor a driver/compiler defect is established. The next investigation
should distinguish CPU submission pacing from altered native API execution, ideally in a smaller
reproducer. Do not adopt validation or arbitrary delays as fixes. The full comparison matrix
still requires a demonstrated correction, new validation and a fresh measurement freeze.

## Tooling and custody

The diagnostic CLI now records exact strings or null for six allowlisted shader-validation
environment variables: DEFAULT_STATE, ENABLE_PIPELINES, DISABLE_PIPELINES, REPORT_TO_STDERR,
FAIL_MODE and DUMP_PIPELINES (all prefixed MTL_SHADER_VALIDATION_). Both start and outcome JSON
include them; scored environment/schema output is unchanged. Arbitrary environment data is not
copied. Native diagnostics log both successfully created pipeline states before warmup.

The existing CPU `--selftest` exposes the same serialization for regression coverage without
creating a Metal device. Ten CLI tests passed, including quoting, empty/unset values, unchanged
scored environment and exclusion of unlisted variables. Rebuilt CPU tests passed 22 cases /
37,019 assertions. Production source, RHI, shaders and dependencies remain unchanged.

The external local bundle `luminex-m5.6-instrumentation.KtOwrj` holds snapshot bin/Shaders,
source.patch over the previous dependency attempt, exact session settings, six start/outcome
pairs and logs, kernel observations and CPU/policy logs. It is not an uploaded public artifact.
Earlier bundles and immutable tags remain untouched; the final milestone evidence tag is absent.

| Snapshot identity | SHA-256 |
|---|---|
| Executable | `fa42a145f7a975f8935cd672e767f880ad44ada4faa57b6e4355c2f0de2ec930` |
| Shader bundle | `ae045f9a07b7a002e899f9e6b25c3936c4ef537bd0b328311d347304ec10bd1c` |
| Target manifest | `f2969cecb51bc15feaf7f055495afe5d0bf6950ffd8b0d1b6e324602b32995f7` |
