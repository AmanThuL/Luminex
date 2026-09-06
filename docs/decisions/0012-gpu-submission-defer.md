# ADR 0012: Defer GPU work-submission adoption pending required evidence

**Status**: Proposed
**Date**: 2026-09-06

## Context

The [M5.6 spec](../specs/2026-09-06-m5.6-gpu-work-submission-design.md) compares native
Metal 4 submission mechanisms, with a separate untimed production RHI correctness anchor.
Four native modes are implemented: `direct`, `cpu-indirect`, `gpu-args`, and `batched`.
Their timings do not measure production renderer performance. Production M5.5 runtime code
in `Source/` and `RHI/` remains unchanged, including the frame-data contract adopted by
[ADR 0010](0010-execution-model-partial-reshape.md).

Formal collection was stopped after GPU global restarts and retirement failures. It contains
1,043 structurally valid pairs, two aborted processes and one operator-interrupted pair, not
the required 1,920 supported pairs. No performance finding or break-even region is accepted.
The protocol specifies 20 cases, submission suite S and end-to-end suite E, 12 alternating
AB/BA pairs per comparison, and 32 warmup plus 256 measured frames per run. The four
comparisons are each other implemented mode against `direct`, plus `gpu-args` against
`batched`. The [attempt report](../research/2026-09-06-gpu-submission-evidence.md) retains the
incomplete matrix, failures, artifact identities and pre-collection validation environment.

## Proposed decision

**DEFER GPU-generation adoption; keep M5.6 open.** Required reliability, measurement and capture
evidence is incomplete. Partial headline results cannot override those gates. This is a proposed
not a claim that GPU submission cannot win or that direct submission won every comparison.
Keep the production submission path; introduce no ICB or public RHI API through this decision.
This ADR remains Proposed; the active implementation plan has not satisfied stages 5–6.

The spec requires an E-suite material win on the same metric against both direct and batched
at two adjacent tested N scales, with median improvement at least 15% and a paired 95% interval
above zero. GPU-span and throughput guard intervals must also stay above the -15% loss margin.
Unavailable guards prevent adoption regardless of headline CPU work or throughput.

## Evidence and limits

- Frozen executable SHA-256:
  `7dcc36ffeb60eb469b7b1c1f7175abbc78a30889252e7af54e3421bafa199a42`.
  Pre-collection scored-artifact replay passed all 160 case/suite/mode combinations over 256 frames;
  actual post-retirement parity passed separately from capture inspection. The 900-frame
  E-suite stress passed for all four native modes.
- Final reported tests passed: experiment CPU 21 cases / 37,011 assertions, experiment GPU
  6 / 54,105; production unit 405 / 77,439, production GPU 97 / 11,395; unchanged checkpoint A
  19 / 1,856. These precede the collection faults; they are not post-fault certification.
- `gpu-icb` is unavailable in this experiment. The pinned Slang 2026.14.1 probe encountered
  E30015 for opaque `command_buffer`, `render_command`, and `primitive_type`; the lowering
  route remains unresolved. This establishes neither universal Slang impossibility nor a
  Metal hardware limitation, and does not validate native ICB execution or graph coverage.
- `gpu-span` and `stages` are unavailable: those lanes are not implemented and workload
  timestamp boundaries are not proven. This is an instrumentation gap, not a physical GPU limit.
- The capture gate is unavailable. The sparse capture blob exposes initial state from warmup
  phase 6, not post-dispatch phase 0. Labels/images were decoded and bindings checked against
  source; that does not establish captured post-dispatch generated arguments. Separate retired
  readback parity does not substitute for this capture evidence.
- A pre-freeze long replay encountered an IOGPU fault of unknown cause. Retain that failure
  in the evidence record; the successful final replay does not prove the fault was fixed.
- The no-verify collection subsequently failed in two pairs at N=16,384/T=32/visibility=0.5/B=1.
  Faulting modes and root cause remain unresolved. Source/ABI audits found no demonstrated cause.
  Verification adds synchronous readback and changes overlap, so it does not certify uninterrupted
  three-slot measurement scheduling. No further GPU runs were attempted after the safety stop.

## Consequences and closure

M6 remains unblocked. M7 first establishes a maintained CPU/batched production benchmark and
oracle, then considers only proven GPU work at interface gate B. Neither an ICB implementation
nor an RHI extension follows automatically from this experiment. Any adoption needs bounded
production evidence and the missing gates; frozen experimental code is not promoted.

The final `m5.6-gpu-submission-evidence` tag is reserved for closure and has not been created.
Keep raw bundles outside the published tree. First localize the fault in an isolated test session
without hiding overlap, then verify any proven fix and recollect under a new freeze. Do not replace
failed pairs. [M5.6](../milestones/m5.6.md) remains Proposed, and experimental code stays off main.
