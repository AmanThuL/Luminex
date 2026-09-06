# ADR 0012: Close GPU submission experiment with adoption deferred

**Status**: Accepted
**Date**: 2026-09-06

## Context

The frozen M5.6 protocol compared native
Metal 4 submission mechanisms, with a separate untimed production RHI correctness anchor.
The experiment implemented four native modes: `direct`, `cpu-indirect`, `gpu-args`, and `batched`.
Their timings do not measure production renderer performance. Production M5.5 runtime code
in `Source/` and `RHI/` remains unchanged, including the frame-data contract adopted by
[ADR 0010](0010-execution-model-partial-reshape.md).

Formal collection was stopped after GPU global restarts and retirement failures. It contains
1,043 structurally valid pairs, two aborted processes and one operator-interrupted pair, not
the required 1,920 supported pairs. No performance finding or break-even region is accepted.
The protocol specified 20 cases, submission suite S and end-to-end suite E, 12 alternating
AB/BA pairs per comparison, and 32 warmup plus 256 measured frames per run. The four
comparisons were each other implemented mode against `direct`, plus `gpu-args` against
`batched`. The [closure report](../research/2026-09-06-gpu-submission-closure.md) owns the
bundle indexes, diagnostic synthesis and reopening conditions.

## Decision

Close M5.6 as **reliability failure / DEFER / NO performance conclusion** under the terminal
disposition explicitly authorized by the user on 2026-09-06. Retain the incumbent production
submission path; introduce no experimental runtime, ICB or public RHI API. No root-cause fix,
performance win or direct-submission superiority is claimed.

The user waived the complete 1,920-pair measurement requirement solely for closure. This accepted
terminal contract supersedes only the former closure prerequisite; the original reliability,
measurement, capture and adoption gates were not passed. The milestone's Implemented status
means completed administrative and evidence closure, not successful validation. Future adoption
still requires full validity and the original gates.

Future adoption requires an E-suite material win on the same metric against both direct and batched
at two adjacent tested N scales, with median improvement at least 15% and a paired 95% interval
above zero. GPU-span and throughput guard intervals must also stay above the -15% loss margin.
Unavailable guards prevent adoption regardless of headline CPU work or throughput.

## Evidence and limits

- Frozen executable SHA-256:
  `7dcc36ffeb60eb469b7b1c1f7175abbc78a30889252e7af54e3421bafa199a42`.
  Pre-collection scored-artifact replay passed all 160 case/suite/mode combinations over 256 frames;
  actual post-retirement parity passed separately from capture inspection. The 900-frame
  E-suite stress passed for all four native modes.
- Historical pre-collection tests reported on 2026-09-06 passed: experiment CPU 21 cases /
  37,011 assertions, experiment GPU 6 / 54,105; production unit 405 / 77,439,
  production GPU 97 / 11,395; unchanged checkpoint A
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
  three-slot measurement scheduling. Later bounded diagnostics reproduced resets and sensitivity
  to validation and argument production; wider dependency masks did not establish a fix.
  Successful individual retirements do not certify reliability. Root cause remains unresolved.

Last recorded Metal environment on 2026-09-06: Apple M3 Max, macOS 26.5.2 (25F84),
Slang 2026.14.1, metal-cpp 26.4 and clang 21.0.0. This is provenance, not certification.

## Consequences and closure

M6 remains unblocked. M7 first establishes a maintained CPU/batched production benchmark and
oracle, then considers only proven GPU work at interface gate B. Neither an ICB implementation
nor an RHI extension follows automatically from this experiment. Any adoption needs bounded
production evidence and the missing gates; frozen experimental code is not promoted.

Historical documents belong to the local frozen evidence tag `m5.6-gpu-submission-evidence`:
`docs/specs/2026-09-06-m5.6-gpu-work-submission-design.md`,
`docs/research/2026-09-06-gpu-submission-evidence.md`, the historical executor plan and all
per-attempt research notes, including `docs/research/2026-09-06-gpu-submission-diagnostic.md`
and the dependency, instrumentation and argument-source diagnostics. These are frozen tag inventory,
not live dependencies on main or claims of a published remote tag.

Keep raw bundles outside the published tree. Reopening follows the closure report: localize the
fault in an isolated session without hiding overlap, validate any proven fix and complete fresh
collection under a new freeze. Do not replace failed attempts. Correctness, reliable retirement,
capture visibility and honest capability coverage remain adoption prerequisites.
[M5.6](../milestones/m5.6.md) is closed under this terminal contract; experimental code stays off main.
Root-cause investigation is separate future work, not an active M5.6 plan or required closure step.
