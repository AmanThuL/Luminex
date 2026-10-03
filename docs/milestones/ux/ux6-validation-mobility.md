# UX6 — Mobility validation

**Status:** In progress
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
Task 14 is complete; Tasks 15–16 and 18–22 are unstarted. No pose-lock/native mobility or image checkpoint pass is inferred.

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
