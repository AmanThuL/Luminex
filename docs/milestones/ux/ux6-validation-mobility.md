# UX6 — Mobility validation

**Status:** Frozen — non-normative; a task-time snapshot. Current results are in [final measurements](ux6-validation-final.md) and the [milestone review](ux6-review-validation.md)
No milestone owner acceptance or merge.

[Execution validation](ux6-validation.md) holds the parent freeze, image checkpoint and overall gates.
Raw evidence remains in `../Luminex-evidence/ux6/` relative to the candidate checkout.

## Task 13 — Authored mobility

The document model carries Static/Movable mobility and nearest imported-source override inheritance.
All six catalogs change only mobility: Helmet and every authored light are movable; other subjects
remain static. Binding vectors preserve object and authored-light order; export preserves mobility.
Schema 1 reads lights as movable and saves as schema 2. The canonical identity serializer retains
legacy bytes for dirty/watch comparisons; a new save-form helper upgrades the actual saved payload.
Engine and App save files are added to scope so disk bytes, receipt hashes and adopted scenes agree.

Initial RED: two cases, 33 assertions, five failures. Two new test compile errors (a macro ternary and missing GraphTestSupport include) were
corrected before final GREEN. The first broader CPU diagnostic failed 15 cases/15 assertions out of
1,583,065 assertions on catalog punctuation and old disk-canonical assumptions. Review then found
migration could erase invalid model mobility before validation. Five new refusal sections failed
before repair; save now validates both original and migrated models and preserves files on refusal.
The next focused run failed four cases/assertions (59/63 cases, 83,659/83,663 assertions): a shared
fixture migration caused schema 1 arbitrary-URI tests to hit schema 2 naming checks first. Restoring
legacy reader fixtures and migrating only save-comparison setups preserved every old assertion and
golden file. The repaired focused run passed **83,988 assertions in 63 cases**. Original logs remain
under `task13-*.log`; separate review audited 1,031 original assertion invocations unchanged.
The new refusal test initially left its protected invalid sentinel in validator discovery. Cleanup
now removes it after all preservation assertions. The obsolete first gate run was cancelled at
11/12 passed, with layout uncompleted, and its exact process records/logs retained. The rebuilt
final and independent focused runs each passed **316 assertions in ten cases**; the sentinel is
absent afterwards. Fresh full per-commit gates passed **12/12**. The pinned validator passed
**92 documents** (six catalogs plus 86 discovered writer fixtures), after the unchanged positive
writer test refreshed its shared output (66 assertions, one case). This is this run's inventory,
not Task 12's 107-document count; arbitrary test-order output stability remains unverified.
Evidence: `task13-notes.json`, `task13-review.json` and final gate/validator logs. Task 13 is complete;
Tasks 14–15 are complete; Tasks 16 and 18–22 are unstarted. No pose-lock/native mobility or image checkpoint pass is inferred.

## Task 14 — Shared pose locks

The exact PoseLock API and five reasons now govern object, authored-light and generated-light
edits. Missing mobility fails closed; saved meshes have storage capability independently of
permission. Imported objects preflight every material primitive before mutation. Static lights
accept non-pose changes while preserving their position/direction bytes, including signed zero.
Generated objects and animation-owned transforms refuse edit/reset without dirty notifications.

Tests first failed to compile against the missing API (`task14-red-build.log`). The first focused
run failed one invalid Spot fixture (79 assertions, one failure); its explicit outer cone repaired
the fixture. Initial full CPU diagnostics failed **13 cases/30 assertions** out of 1,687,313.
Local Movable fixtures now express the intended editable cases; generated/animated success tests
become stronger named-refusal and no-change assertions while retaining sampling/reset coverage.
Six additional affected test files join the planned scope; shared fixtures, golden files and
tolerances are unchanged. The second CPU diagnostic failed one assertion: a point-light fixture
had inadvertently become Movable, contradicting its preserved Static assertion. Restoring that
light's authored value repaired the fixture; the third full CPU run passed (11.300 s).

Review found mixed Session proposals could mutate an earlier movable object before refusing a
later locked object or light. Separate runtime REDs each failed three no-change assertions. Batch
permission preflight now includes imported sibling primitives before any write. A hidden-primitive
regression also failed three assertions in a controlled replay with only that preflight removed,
after the test and initial fix already existed; the exact fix was restored before GREEN. This
chronology is retained rather than presented as an earlier tests-first run. Final focused tests
passed **220 assertions in five cases**; affected CPU tests passed **2,779 assertions in 101 cases**.
The four affected GPU cases passed **182 assertions** under Metal API validation. Independent
review rebuilt Tests and confirmed the same **220 assertions/five cases**. The full gate run
passed **11/12 commands**; policy rejected ROOT's new document status line containing extra
prose. Moving the prose to a separate line repaired that documentation error. Final repaired
policy is recorded separately; build/CPU/format/database and all other checks remain from the
unchanged reviewed source boundary. This composite passes **12/12 checks** without erasing
the original failed run. `task14-final-composite-gates.json` preserves both receipts. No Tools
change required the Python suite. ROOT and the separate reviewer approved the final boundary.
Logs and notes remain under `task14-*`. Field-qualified bridge
preview reasons, disabled Inspector presentation and native mobility checks remain Tasks 15–16.

## Task 15 — Inspector and Hierarchy

Inspector displays each authored subject's disabled Static checkbox, including inherited mobility
on animated imported objects, independently of temporary pose locks. Locked pose fields have the
exact shared reason; Enabled and light non-pose fields remain available. Locked object header
Reset restores Enabled without attempting a refused pose reset. Generated subjects have no
file-authored mobility checkbox. Hierarchy marks movable exceptions and edited saved values
through the existing provenance path; generated children have neither row flag. Explicit saved
camera changes and Environment look edits receive marks; ordinary camera navigation stays clean.
The edited-subject helper moves into AppModel for shared, tested use without ImGui dependencies.

Runtime RED reproduced the saved-disabled mesh baseline bug (**seven assertions, two failures**):
Reset used generatedObjectEnabled=true and enabled the object. The fix reads the document mesh's
own Enabled baseline. A separate API RED failed compilation on the new Movable icon/accessors.
The first GREEN failed one camera request because the new fixture's lens was zero; an explicit
valid authored lens repaired that fixture. Six new cases then passed **80 assertions**. ROOT's
preliminary inherited-light concern was disproved by activation's existing own-flag normalization;
a new initially-disabled-parent regression passed **11 assertions** without production changes.
The first formatted build command incorrectly requested multiple xmake targets and was refused
before compilation; separate App and Tests builds passed. The initial focused Inspector/Hierarchy/
icon suite passed **367 assertions in 30 cases**. Final review found camera own Enabled changes
were absent from the edited mark: a new runtime RED failed **two of nine assertions**. The helper
now compares that own flag against the loaded camera node independently of pose/lens. Repaired
focused tests passed **376 assertions in 31 cases** (eight new cases, 100 assertions). The first
full gate run completed 11 commands: ten passed, policy rejected seven task-number test tags.
Those tags were renamed to describe mobility, with assertions unchanged. The remaining layout
check was cancelled using its recorded own process tree when source freeze was revoked; it was
not counted as passed. New formatted source, App and Tests rebuilds passed before fresh gates. No assertion, golden file, tolerance or image
reference changed. Fresh full per-commit gates passed **12/12**, and independent review rebuilt
Tests and passed the same **376 assertions in 31 cases**. All 15 source/test hashes match the
formatted freeze. Evidence: `task15-final2-gates.json`, `task15/` and `task15-review.json`.
ROOT and the separate reviewer approved the final boundary. Native mobility presentation and
image parity remain the Task 16 checkpoint.


## Task 16 — Named session refusals and checkpoint

The bridge preflights locked object Position/Rotation/Scale and light Position/Direction with
`<subject>/<field>: ` plus the shared pose-lock reason. Mobility requests say that mobility is
authored in the scene file. Invalid/stale local-light identities retain their existing refusal;
measurement and generated-storage rules retain their priority. Atomic apply preflight is unchanged.
Only the two planned source/test files and the net-zero, 300-line agent-session guide changed.

Initial RED: 3 cases, 201 assertions, 34 failed. The first GREEN attempt had six failures from a
new fixture's duplicate `node:1` group/object identity; the fixture was corrected. Independent
review found stale light identities getting a lock reason: additional actual RED was 1 case,
58 assertions, 12 failed. Guarding invalid identities repaired it. Final and independently rebuilt
`[session-edits]` runs each passed **880 assertions in 21 cases**. No assertion or tolerance loosened.
Corrected source hashes are in `task16/task16-verified-notes-4322cdfe-688f-44a8-a880-fe675bc86465.json`;
initial pre-fix notes remain evidence and do not identify the final source.

`task16-final-gates.json` records **12/12 passing commands**. The physical App SHA-256 is
`1528064f53ac57ed93feca3c559752e55c8879a7a5c2cbc925023eed10c27801`; its 126 runtime shaders
and 63 source shaders were frozen. Actual captures matched Task 12's original candidate BMPs
**15/15 byte for byte**, independently audited (`task16-task12-image-exactness.json`). An external
frozen reference updates document pins only for the mobility-authored files; every approved image
hash and other reference field stays identical. `Tools/Screenshots/reference.json` was not changed.

CUA selected LightLab Pillar 0: Static was checked/disabled, pose fields gray, and the exact reason
visible. Clicking Static and dragging Position left the pose and clean title unchanged. Enabled
made the row `[off]`, with dirty title and operator marks; Cmd+S reported Scene saved and cleared
them. Saved JSON changed only `/nodes/1/extensions/LMX_scene/enabled` from true to false; all other
copied files were unchanged (`task16-native/saved-enabled.json`). The first attachment timed out
while prior PID 2836 was absent, cause unproven; detached PID 3892 attached successfully. Cmd+Q
exited PID 3892; AX observation after exit timed out, with exit independently confirmed. Screenshots
were shown in the conversation, not saved locally. Static-checkbox tooltip hover and fresh reload
were not exercised here; final native/gizmo/completion checks remain for Task 22.
