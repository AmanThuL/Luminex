# GPU submission: CPU versus GPU argument producer

**Status**: Frozen — non-normative

**Date:** 2026-09-06. Follow-up to the
[validation-state comparison](2026-09-06-gpu-submission-instrumentation-diagnostic.md).
The user requested continued execution toward complete M5.6. No production code changed and
no milestone-completion or accepted performance claim is made.

## Bounded result

Four fresh processes used the same executable, shaders, manifest, three-slot executor and
gpu-args/S case N=16,384/T=32/visibility=0.5/B=1. API/shader validation and capture were off;
each requested 32 warmup and 900 replay frames. Both pipeline states queried disabled.

| Order | Argument producer | Outcome |
|---|---|---|
| 1 | CPU bitmap expansion | 900 replay frames retired |
| 2 | Original Prepare dispatch | 900 replay frames retired |
| 3 | CPU bitmap expansion | 900 replay frames retired |
| 4 | Original Prepare dispatch | Timeout during warmup; replay never began |

The fourth run reports MTL4CommandQueueErrorDomain code 1 for submission 2, slot 1/frame 1,
with failures for submissions 3/4 too. Kernel GPURestartBegin is recorded at 22:36:24.322
Asia/Shanghai. The process returns exit1 after feedback delivery. No GPU diagnostic follows
that reset. This is not a successful 900-frame GPU stress test or an estimated failure rate.

CPU mode fills all N 16-byte records with the same Suite S layout as Prepare: vertexCount=3*T,
instanceCount=bitmap[id]?1:0, firstVertex=id*3*T, firstInstance=0. It skips the compute encoder
and its consumer barrier. N indirect calls, zero-instance entries, argument storage, bindings,
rendering and three-slot lifetime discipline remain. CPU uploads/costs and scheduling differ,
so the two CPU passes do not prove a faulty compute shader or establish a replacement algorithm.
The GPU control also passed once. A smaller failing reproducer or a proven contract violation
is still needed; earlier failed attempts remain relevant.

## Diagnostic isolation

`--diagnostic-arguments cpu|gpu` is diagnostic-only and requires gpu-args. CPU additionally
requires Suite S and excludes all-stage dependency selection, since there is no compute producer.
The native API enforces these restrictions and rejects CPU diagnostic arguments from verification,
capture and scored paths before device creation. The default measured producer is unchanged.

Start/outcome JSON use unscored protocol `pipelined-feedback-arguments-v3` and explicitly record
the argument source. Dependency `declared` is the selected option; CPU mode executes no producer
barrier. Other variants without this option record source `native`. No image/argument parity or
performance result is emitted. CPU comparison is not the scored cpu-indirect mode, which packs
only visible work and has a different draw count.

Rebuilt benchmark/tests passed 23 CPU cases / 37,050 assertions and 11 CLI tests. Rejection
coverage checks expected errors and absence of output, not merely nonzero exit. Independent
review found no blocking discrepancy between the CLI, native guards and producer implementation.

A CPU-only diagnostic audit checks full start/outcome context, known protocol versions, unscored
status, exact retirement counts, and failure records. Twelve regression tests passed. It audited
all four current and thirteen prior diagnostics: 13 retired, 4 failed, zero inconsistent records.
This is record consistency, not hash authentication, reliability acceptance or a parity claim;
the four failed runs remain failures. It neither executes GPU work nor changes old evidence.

## Completion audit

The [spec decision rule](../specs/2026-09-06-m5.6-gpu-work-submission-design.md) supports a DEFER
recommendation when measurement gates cannot be met. It does not waive the supported report:
the [active plan](../plans/2026-09-06-m5.6-gpu-work-submission.md) expressly keeps stages 5–6 open
pending reliability correction, new validation/freeze, full 1,920 headline pairs and report
reproduction. ICB and GPU-span may remain explicitly unavailable; failed ordinary runs cannot
be silently reclassified or omitted. The final evidence tag and ADR acceptance remain pending.

A terminal reliability-failed experiment closure, without that complete measurement corpus,
would need an explicitly accepted change to the closure contract. This investigation has not
made that change. M6 remains independently unblocked. No repeated GPU resets are treated as
performance evidence or as proof of a hardware/driver/compiler defect.

Capture remains unverified. Read-only inspection found an installed MTLReplayer and Xcode replay
infrastructure, but invoking MTLReplayer with `--help` returned SIGKILL/exit137 without usage
output. No archive was passed or replay completed, and no post-dispatch buffer extraction was
established. This failed tool launch does not prove capture replay is impossible. Raw initial-state
buffer decoding remains insufficient for the captured generated-argument gate.

## Artifact custody

The external local bundle `luminex-m5.6-argument-source.FWZ50j` retains snapshot bin/Shaders,
source.patch, all four start/outcome JSON pairs, logs, kernel observations and build/test/policy
logs. It is local, not an uploaded public artifact. Older bundles/tags are untouched.

| Snapshot identity | SHA-256 |
|---|---|
| Executable | `cf0e9e526c592d8c6eca47e31ab54b7be566e3c8df1e783a97849747868524a4` |
| Shader bundle | `ae045f9a07b7a002e899f9e6b25c3936c4ef537bd0b328311d347304ec10bd1c` |
| Target manifest | `f2969cecb51bc15feaf7f055495afe5d0bf6950ffd8b0d1b6e324602b32995f7` |
