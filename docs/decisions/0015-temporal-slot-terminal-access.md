# ADR 0015: Temporal slot imports follow the final access

**Status**: Accepted (2026-09-08) · **Roadmap**: ../roadmap.md (M6.2) ·
**Spec**: ../specs/2026-09-07-m6.2-native-taa-exposure-design.md ·
**Milestone**: ../milestones/m6.2.md

## Context

ADR 0014 requires each temporal colour slot to retain its terminal use for the next frame's
import, but its concrete examples name the producing write: `StorageWrite` after a resolve and
`CopyDestination` after a raw commit. A later consumer changes that terminal access. Native TAA's
bloom and display passes sample the resolved slot, and the HistoryAge debug view samples the
current slot in either reconstruction mode. Recording the writer instead misdescribes the final
access and derives an unnecessary write dependency on reuse.

## Decision

This ADR supersedes **only the concrete terminal-use examples** in ADR 0014. Its recorded-use
invariant, reconstruction contract, ping-pong ownership, exposure decisions and other conclusions
remain unchanged. `TemporalResolve::recordFrame` records colour slots as follows:

| Mode and consumer | Current slot | Previous slot |
|---|---|---|
| NativeTaa | `ShaderRead` after bloom/display | `ShaderRead` after the resolve |
| Raw with HistoryAge | `ShaderRead` after the debug view | Retain its recorded use |
| Raw with valid ReprojectionError | `CopyDestination` after the commit | `ShaderRead` after reprojection |
| Other Raw frames | `CopyDestination` after the commit | Retain its recorded use |

An invalid history prevents the Raw reprojection diagnostic from reading the previous slot, so its
record is retained. Both depth slots keep ADR 0014's `ShaderRead` import rule; NativeTaa samples
both, and the existing conservative rule also covers Raw and temporal-off frames.

## Consequences

The next frame imports each colour slot using the access its consumers actually left, including
debug consumers. A new colour-history consumer must update this bookkeeping when it changes the
slot's final access. A regression renders Raw + HistoryAge, advances through the other slot, and
checks that reusing the first slot for a raw commit derives `ShaderRead → CopyDestination`.
Existing GPU overlap coverage exercises both history pairs across frames in flight.

## Cross-references

[ADR 0014](0014-temporal-reconstruction-and-exposure-correction.md) retains the original decision.
[ADR 0013](0013-temporal-motion-and-history-contract.md) establishes persistent-history imports.
[Frame pipeline](../frame-pipeline.md) describes the current consumers.
