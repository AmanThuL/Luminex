# UX6 — Gizmo validation

**Status:** In progress

## Task 18 — Pure model

The model exposes View/Move/Rotate/Scale/Combined and World/Local, with Move/World defaults.
Objects offer all transforms; point lights Move; spots Move/Rotate, with local -Z along their
world direction. Shared pose locks, effective Disabled and playback produce inert handles.
The caller supplies resolved selection and stopped playback; unsupported selections return none.
Apply uses the unchanged Core decomposition, rejects nonfinite/invalid matrices, clamps object
scale to 0.01–100 and commits through the same session routes as Inspector. Spot direction is
normalized; point direction is preserved. Drag begin captures raw pose before edits. Cancellation
restores pose bits, including signed zero and nonunit captured light direction, while unrelated
light edits survive. A stale/foreign/locked capture ends without applying to other content.

ROOT approved one scope addition: seven lines in `SceneSession.h/.cpp` expose/advance activation
generation on activation or active invalidation. Drag matches compares that generation and every
selection field, detecting replacement, away/back and reused scene storage; inactive invalidation
keeps the current drag. Existing module-contract directory entries cover the new public files.
No Core, Render, shader, RHI, schema or dependency changes occurred.

Tests first produced a missing-header RED, then a runtime scaffold RED: **10/10 cases failed,
81 failed of 225 assertions**. A Catch2 INFO ternary compile defect was corrected. First implemented
run passed 592/594 assertions, 9/10 cases; both failures were disk-save fixture directories missing
under the existing save contract. Creating those parents corrected setup without loosening checks.
Formatted, rebuilt final focused and independently rebuilt runs each passed **628 assertions in
10 cases**. Zero/negative/extreme scales, NaN/infinity/shear/projective refusal, ±90° pitch, shared
imported primitives, bitwise cancellation, object/spot Inspector/export/save companion bytes and
36 world points over three viewport sizes at the fixed **0.01 px** projection limit are covered.

Evidence: `task18/20261003-161855-161938a7/`, with original RED source/logs, every build/runtime
attempt, formatted source freeze and implementer notes; `task18-review-build-20261003.log`,
`task18-review-focused-20261003.log` and `task18-root-review.json`. Initial evidence went to the
wrong non-project sibling; the unique directory moved intact to the required project sibling,
with a correction note. Native gizmo gestures remain for Tasks 20/22. `task18-final-gates.json` records **12/12 passing
commands**; ROOT source review and separate source review found no remaining issue.
