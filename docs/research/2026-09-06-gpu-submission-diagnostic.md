# GPU submission: bounded pipelined timeout diagnosis

**Status**: Frozen — non-normative

**Date:** 2026-09-06. Follow-up to the [interrupted attempt](2026-09-06-gpu-submission-evidence.md).
The user authorized diagnosis on the same Mac after the global-reset risk was explained.
This is new unscored evidence, not a correction to the frozen performance corpus or a completed
M5.6. The current [plan](../plans/2026-09-06-m5.6-gpu-work-submission.md) remains in progress.

## Result

The failure is reproducible in a **single gpu-args/S run**, without the two-mode process ambiguity.
For N=16,384/T=32/visibility=0.5/B=1, with API and shader validation disabled, the first observed
failure callback identifies submission 2, slot 1, warmup frame 1. Its domain is
MTL4CommandQueueErrorDomain, code 1: the pinned Metal headers map this to CommandQueueErrorTimeout.
Submissions 3 and 4 also reported timeout. The kernel logged a global GPU restart at
18:30:45.826 (Asia/Shanghai). The process returned a failed diagnostic, exit 1, after callback
delivery; it did not rely on the previous teardown assertion to report failure.

This identifies a failing submission path, not whether compute, indirect argument consumption,
raster execution, resource visibility or a driver/compiler issue caused the timeout. It does not
identify the failing modes in the earlier two-mode pairs retroactively, or prove hardware damage.
The session stopped after this reset; no further GPU run was attempted.

## Three runs and their limits

| Run | Flags | Requested work | Observed outcome |
|---|---|---|---|
| Empty gpu-args/E smoke | Metal API validation on | 3 warmup + 6 replay | Retired; no reset |
| Target gpu-args/S | Metal API + GPU shader validation on | 32 warmup + 256 replay | Retired; both validation-enabled messages observed; no reported validation error/reset |
| Target gpu-args/S | API/shader validation off | 32 warmup + 900 replay | Failed during warmup, after submission 4 was issued; replay never began |

The final row is NOT a 900-frame stress pass. Both target runs share the diagnostic executable,
shader hash, manifest and warmup recipe. The different requested replay length changes reserved
CPU result/feedback capacity, so this is not a perfectly one-variable A/B proof of validation's
effect. It demonstrates that enabling validation is not evidence of a fix. The earlier frozen
collection also failed with 256 replay frames requested. Diagnostic feedback and flushed logging
can alter CPU overhead and scheduling; no timing or performance finding is accepted.

## Diagnostic mechanism

`RunConfig::diagnostics` enables immutable case/suite/mode/slot/frame/submission feedback without
verification readback, canaries, image-oracle rendering or additional GPU submissions. All three
slots remain in use. Reuse waits for that slot's feedback plus its ordinary event; no per-frame
wait-idle is inserted. Warmup still drains once before replay, as in the original protocol.
The default measurement path remains callback-free. Source changes do not modify production RHI,
renderer, shaders or pinned dependencies.

`--diagnose` requires one explicit case, suite and ordinary variant; it caps warmup at 32 and
replay at 900, refuses capture/timestamp lanes, and cannot be combined with `--measure`.
diagnostic-start.json is written before GPU work; diagnostic.json records retirement or returned
failure. Both use an unscored diagnostic format; no result.json is emitted. Retirement is not an
image/argument parity claim. Flushed stderr preserves the last submissions even if reporting fails.

CPU tests passed: 21 experiment cases / 37,011 assertions and eight CLI tests, including rejection
of oversized/scoped/measurement-conflicting diagnostic requests before GPU creation. The benchmark
and tests rebuilt. The successful Metal-validation diagnostic is the bounded native smoke; no
full production GPU suite or full collection was rerun.

## Contract check and next discriminator

The executor's dispatch-to-vertex/fragment consumer barrier matches the production adapter's
stage mapping. Apple's [consumer-barrier contract](https://developer.apple.com/documentation/metal/mtl4commandencoder/barrier%28afterqueuestages%3Abeforestages%3Avisibilityoptions%3A%29)
covers prior encoders; compute and raster here are distinct encoders. This check does not establish
fault-time resource visibility. No demonstrated synchronization defect was found, so no speculative
barrier or shader change is presented as a fix. The error classification is also exposed by
Apple's [command queue error API](https://developer.apple.com/documentation/metal/mtl4commandqueueerror-swift.struct).

The useful next experiment is an explicitly unscored discriminator that keeps mode, workload and
requested lengths fixed, records completed submission identities, and varies only one factor
such as validation or a conservative dependency boundary. A pass after serialization or validation
must be reported as a scheduling-sensitive observation, not adopted as a root-cause fix. A proven
correction still requires complete new validation and a fresh measurement freeze. ICB, GPU-span
and capture gates remain unresolved; the old corpus and final evidence-tag reservation are intact.

## Artifact custody

The separate local bundle is `luminex-m5.6-diagnostic.0aHXYM`, not an uploaded public artifact.
It retains the executable/MSL snapshot, source patch, start/outcome JSON, all three logs, kernel
observations, build/CPU-test logs and artifact-index.json. The old bundle is untouched.
The diagnostic source is retained at `m5.6-gpu-submission-diagnostic-2026-09-06`, distinct from
the previous attempt tag and the still-uncreated final milestone evidence tag.

| Identity | SHA-256 |
|---|---|
| Diagnostic executable | `296d9af2c4cbc621972bfdc694ae0a964529d631f6455abc48aa2776b320d8fb` |
| Shader bundle (unchanged) | `ae045f9a07b7a002e899f9e6b25c3936c4ef537bd0b328311d347304ec10bd1c` |
| Target manifest | `f2969cecb51bc15feaf7f055495afe5d0bf6950ffd8b0d1b6e324602b32995f7` |

Reproduction, only in an arranged GPU test session, from the experiment checkout:

```sh
GpuSubmissionBench --diagnose --case n16384-t32-v50-b1 --suite S --variant gpu-args --frames 900 --output <new-directory>
```

Keep diagnostics out of paired timing summaries. Rebuilt executable identities may differ;
never splice this diagnostic result into the previous collection or replace failed pairs.
