# ADR 0027: Close scene-pass shader deduplication with adoption deferred

**Status**: Accepted
**Date**: 2026-09-25

## Context

The four `ScenePass` variants (`ScenePass`, `ScenePassAuto`, `ScenePassMask`, `ScenePassAutoMask`)
are separate files of 322 to 347 lines that differ only in exposure source, and in alpha coverage
with two-sided shading. The [shader-style convention](../conventions/shader-style.md#mirrored-exposure-variants)
requires every other edit to be mirrored by hand, because an M5 parity check changed one rounding
bit when the manual shader carried a never-taken exposure branch. R4 tested whether one shared
module with thin entry files could replace the mirrored files without changing compiled layouts or
rendered output.

[R4.1](../milestones/r/r4.1.md) measured two candidates on `exp/` branches against an unchanged
parent, and a [parent-calibrated follow-up](../milestones/r/r4.1-followup.md) re-measured the first.

- **Interface generics (candidate A).** Reflected layouts and entry-point bindings equal the
  parent's, the suites equal the parent's counts, and every policy checker passes. Rendered output
  was unchanged in every case the parent repeated within the same alternating session, except one
  `sponza-auto-taa-1` capture in the follow-up: one of A's 8 captures produced a hash the parent
  never produced in 24 captures, 569 pixels different. In R4.1 it also failed the two San Miguel
  Native TAA cases, which the parent did not repeat in either alternating session; the follow-up's
  leave-one-round-out rule passed A on both, with the parent null control holding and candidate B
  failing.
- **Value parameters (candidate B).** Slang keeps `bool masked` as a runtime parameter with three
  tests in every variant's MSL, including the opaque variants, and 18 of 29 rendered cases change,
  including temporal-Off cases.

Every A failure falls in Native TAA captures. The parent's Native TAA output varies between runs in
some cases and depends on run conditions: `san-miguel-manual-taa-1` repeated in 8 H-only runs and
gave 4 and 8 hashes when builds alternated. In `sponza-auto-taa-1` the parent repeated in all 24 of
its captures, and whether A's single outlier there is a rare parent variant or a change of A's own
is not established. The [M6.5 investigation](../milestones/m6/m6.5.md#open-parity-investigation)
localized a reproduced native-TAA drift to the scene pass's diffuse/gradient path; whether that
explains these cases is not established.

## Decision

The owner closed R4 as **DEFER** on 2026-09-25. The twin files and the convention's twin rule stay
as they are; R4.2 is not opened, and no shared scene-pass module reaches `main`. Implemented status
on the R4 record means completed evidence closure, not successful validation; R4.1 is Implemented
because its exit gate, a complete recorded comparison, held.

The value-parameter approach is rejected: it leaves a runtime coverage branch in every variant,
which R4's boundary forbids regardless of rendered output. The interface-generics candidate is
retained as evidence at tag `r4.1-candidate-a-evidence`, not adopted.

## Reopening conditions

Shared-module deduplication of the scene-pass family may reopen as a new R milestone, with its own
outcome and gates under Part III, when all of these hold:

1. Over 8 rounds of R4.1's 29-case matrix, in a build order that alternates as a parity session
   does, every Native TAA case of the parent repeats; or an owner-accepted rule for non-repeating
   cases exists, fixed before any candidate capture, whose parent classification comes only from
   parent captures in that session.
2. The interface-generics candidate, rebased onto the then-current `main`, passes R4.1's static,
   suite and coverage gates, the strict hash rule on every repeatable case, and condition 1's rule
   on every non-repeatable case. A single unseen hash in a repeatable case, as in
   `sponza-auto-taa-1`, fails unless the owner accepts a rule for it before capture.
3. The owner confirms that MSL differences without arithmetic change do not block adoption.

`Sky`/`SkyAuto` and `ShadowPass`/`ShadowPassMask` stay mirrored until the scene-pass family is
adopted.

## Consequences

Scene-shading edits in M8 and M9 are mirrored across four files by hand, as the convention already
requires. The harness, both candidates, the follow-up evaluator and their evidence stay reachable
through the `r4.1-*` tags and the evidence custody named in the R4.1 records. The evidence is
bounded: one device (Apple M3 Max), R4.1's 696 captures and the follow-up's 928, and a single-capture
outlier whose cause and rate are unknown.
