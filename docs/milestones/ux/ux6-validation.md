# UX6 — Execution validation

**Status:** In progress
**Date:** 2026-10-03. The milestone remains `Accepted`; no owner acceptance or merge occurred.

Tasks 1–16 are complete; Task 8 is `c888541` and tag `ux6-exporter`.
Task 17 is also complete after isolated and combined gates plus separate review. Task 12 automatic
checks and images ran. After the required image/native stop, the owner accepted the five
MaterialLab reference updates and authorized native retries. Sphere edit/Save/relaunch and generic
glTF import now pass as observed below. Task 13 adds document mobility; Task 14 adds shared pose locks; Task 15 adds Inspector/Hierarchy presentation; Task 18 is complete after model tests, all gates and separate/ROOT review; Tasks 19–22 remain. The owner requested parallel work and explicitly approved reusing the idle
Task 1 reviewer as Task 17's implementer after the agent interface refused a fresh thread.
Task 9 and Task 17 have independent checkouts and reviewers. Other tasks use fresh agents.

## Source and evidence

- Candidate: sibling `Luminex-ux6`, branch `feat/ux6-scene-authoring`, from local design commit
  `21a704fc6bc5f07e63e832320458dcd7ef12fcb1`.
- Frozen parent: sibling `Luminex-ux6-parent`, `80fa7162aa0ff9b2f97d220927a9a4aca02c4544`.
  App SHA-256: `4fa704c3029c4c1e97ac76509fbba91471dbe8eac4eeba8cf2450b36b63ec352`.
  Task 8 rechecked all 190 App/runtime/source shader hashes against `parent-freeze.json`;
  all matched, and the parent was not rebuilt.
- Raw evidence and ledger: `../Luminex-evidence/ux6/`, relative to the candidate checkout.
- [Exporter validation](ux6-validation-exporter.md) preserves the full Task 1–8 measurements,
  failures, causal evidence, representation limits and owner-authorized resumptions. Its
  original text is also retained in commit `c888541`.

## Per-commit checks

These check build, CPU tests, formatting, regenerated compilation commands and the direct root
policy checkers. Tools changes also run the Python suite. Every xmake command uses `-P .`.
They do not certify App image parity, the complete GPU suite or native gestures. The outside
runner now overlaps independent read-only checkers after build/test/format/database generation;
commands and assertions are unchanged, with distinct logs and no previous-evidence overwrite.

| Task | Commit | Final checks | Evidence |
|---|---|---|---|
| 1 | `7953f4d` | 12/12 passed | `task1-gates.json` |
| 2 | `42cdc696` | 13/13 passed | `task2-gates.json` |
| 3 | `621aebbd` | 13/13 passed | `task3-final-gates.json` |
| 4 | `781539ab` | 13/13 passed | `task4-final-gates.json` |
| 5 | `5e87a1d6` | 12/12 passed | `task5-gates.json` |
| 6 | `3dc475da` | 13/13 passed | `task6-gates.json` |
| 7 | `11264c55` | 12/12 passed | `task7-final-gates.json` |
| 8 | `ux6-exporter` | 13/13 passed; 348 Python tests | `task8-sidecar-final-gates.json` |
| 9 | `8dd7563` | 13/13 passed; 348 Python tests | `task9-final2-gates.json` |
| 10 | `6914efb` | 12/12 passed | `task10-final2-gates.json` |
| 11 | `d66e2fe` | 13/13 passed; 356 Python tests | `task11-integration-gates.json` |
| 17 | `6a7a338` | 13/13 passed; 359 Python tests | `task17-integration-gates.json` |
| 12 | `c9cd9b6` | 13/13 passed; 363 Python tests | `task12-accepted-gates.json` |
| 13 | `1343471` | 12/12 passed | `task13-final2-gates.json` |
| 14 | `9dc7f6e` | 12/12 composite; initial 11/12 retained | `task14-final-composite-gates.json` |
| 15 | `a6dac95` | 12/12 passed; initial cancelled run retained | `task15-final2-gates.json` |
| 16 | `80e20d2` | 12/12 passed; 15/15 exact images; native checkpoint | `task16-final-gates.json` |
| 18 | this commit | 12/12 passed; 628 focused assertions | `task18-final-gates.json` |

## Retained failures and resumptions

Task 8 stopped immediately on its first actual exactness run: TemporalLab's rotating cube
could not encode Euler `[-0, +0, +0]` (bits `80000000,00000000,00000000`) exactly. Numeric
magnitude was zero with a different sign bit. MaterialLab passed with only six axis objects
omitted; LightLab was not run. The owner authorized repair. The signed-zero repair passed the
renewed run: **4,405,255 assertions, zero failures** across all three labs and 1,441 sample times.
LightLab used the disclosed temporary lights-only test route; production retirement is Task 9.
Both MaterialLab paths used the same unavailable-Helmet-mips fallback. This is generator versus
captured-document equality, not parent App image parity. First and renewed logs remain intact.

Three subsequent full CPU gate attempts failed on historical catalog assumptions and triggered
the required stop. The third diagnostic ended at 723 cases (721 passed, two failed), 1,215,768
assertions (1,215,766 passed, two failed), process exit 139, and did not complete. The failures
were a 19-versus-two object fixture and dereferencing node zero's absent generator. The owner
then instructed: "Let's note this somewhere and continue all remainings." Reviewed repairs
preserved historical schema 1/pin assertions and used independent fixtures or unique identities.
The renewed full run found a Session sidecar hash incompatibility; strict schema 2 identity
support was added with current-catalog tests. All historical failures stay recorded. No test,
tolerance, comparison or image reference was loosened.

## Scope additions

Task 8 includes the object-only signed-zero encoder/decoder repair, immutable historical catalog
fixtures, all-six-document directional grouping, and screenshot/Session selftest integration.
The Session client now matches JSON plus animation identity; geometry is excluded as in C++.
These expand the Task 8 file list and are separately reviewed. [Exporter validation](ux6-validation-exporter.md)
records earlier helper-file additions and schema 2 representation limits. No RojoRHI, pass,
shader, instance ABI, MotionClass or workspace/capture/measurement schema changed.

## Gates and gestures still unverified

The unchanged pinned glTF validator passed 107 documents after export (six installed catalog
and 101 writer fixtures). This does not certify full animation-pointer extension semantics.
Task 12's measurements below supersede the formerly unrun image/GPU gate status.
Tasks 16 and 22, completion-gate tasks and gizmo screenshot/GPU capture checks remain unverified.
The scoped MaterialLab reference acceptance is recorded below; original failed gates remain failed.

Task 12 exercised a saved sphere pose edit, Save, fresh launch/reload and Blender glTF import.
Static fields/reasons/Enabled persistence, each gizmo tool and space, Inspector synchronization,
Escape and drag-state transitions, point/spot light operations, shortcuts, RMB flight and final
persistence remain unverified.
Document mobility is implemented; pose locks, mobility UI and the gizmo remain unimplemented. No integration tag, push or PR exists.

## Task 9 — Generator retirement

MaterialLab and TemporalLab generators, the one-time exporter and its temporary build permissions
are removed. LightLab now appends lights, pile and orbits around its 17 saved objects; VisibilityLab
keeps its populations. Retired generator/axisStation documents fail with the named schema 2 reason.
Focused Metal checks passed 13,371 assertions in three retirement cases, 142,864 in 81 mixed cases,
and 6,425 in ten actual population/Temporal cases; the initial `[visibility-lab]` filter matched
no case and the correct filter was run separately. These are not the complete GPU gate.

Early test-fixture errors (nonexistent members/types, missing mesh identity and finalized-scene
append guards) and the first GREEN's unsampled orbit expectation are retained in `task9-notes.json`.
The finalized-scene append issue failed twice, not three times; root and separate reviewer audited
the chronology and actual assertion sites. The first full CPU run failed (exit 255); its direct
diagnostic exited 139 after two fixtures assumed LightLab still generated objects. Reviewed
VisibilityLab spawners preserved LightLab light/pile and imported indices plus every assertion;
the two affected tests then passed 78 assertions. The fresh full run passed all 13 commands.
Extra test routes, temporary-module cleanup and obsolete source-comment corrections are recorded
in `task9-notes.json`. Detailed Task 1–8 history was split into the linked exporter page to keep
each documentation page within 300 lines. Image, full GPU and native gates remain Task 12.

## Task 10 — Saved and generated rows

Generated rows use the shared `Generated by <name>` label and saved-parameter tooltip; the
unsaved suffix remains for CLI masks. Generator groups start collapsed through SceneTreeState,
while explicit expansion, scene-key migration and filtered matches remain. Saved mesh nodes
now select their actual bound object instead of appearing as groups; a pose edit/save/reload
test covers a nonidentity node/object index. The additional SceneTreeState files are required
by the planned collapse behavior; saved-mesh classification fulfills the record's ordinary
saved-content outcome. The saved Enabled baseline correction remains Task 15.

RED: 27 cases/150 assertions, six cases/nine assertions failed. One earlier test-macro compile
error and two first-GREEN fixture expectations are preserved. The reviewed fixes preserve
all prior assertions: loader object order differs from root order, and an enabled-fingerprint
test explicitly opens its newly default-collapsed generator. Final focused and independent
runs passed 27 cases/188 assertions. The first full run failed policy on four test tag/path
narration tokens; only those tokens changed. Its exact process tree was cancelled before
refreezing sources. The fresh full run passed all 12 commands. Evidence: `task10-final-summary.json`.

## Task 11 — Immutable content hashes

The validation tool checks every catalog geometry/image reference and declared content hash,
with URI-specific failure paths and a validator-free temporary-fixture selftest. Legacy/default
schema 1 keeps its safe single animation buffer under arbitrary relative `.bin` names; schema 2
excludes only its named animation buffer. Khronos invocation, report parsing and discovery are
unchanged. Tests-first evidence and two separate reviews cover the final two Python files.
The isolated and combined integration runs each passed all 13 commands; the combined Tools
suite passed 356 tests. Final sources match `task11-legacy-fixed-evidence.json`.

The isolated validator initially reported three IO errors because an existing negative native
test deletes a shared writer fixture's immutable companions. The existing positive round-trip
writer case refreshed that fixture (66 assertions, one case); the unchanged pinned validator
then passed all 97 isolated documents. The combined run rebuilt Tests and explicitly repeated
that positive writer case before validation: all 107 documents passed (six catalog, 101 writer).
No discovery, assertion or hash check was weakened. Original failure and refresh logs remain
in the evidence directory. Writer output stability after arbitrary CPU test ordering is unverified.

## Task 17 — ImGuizmo dependency

ImGuizmo is fetched at `18cef5e031d8c6973d80284c67f60549fafd78c1`, with its upstream
MIT license and provenance. The planned static target builds against the pinned Dear ImGui
without a patch. App is its only consumer; no Engine, Render, AppModel or Tests include/link
permission was added. The isolated setup/build and all 13 gates passed, followed by separate
review. After integration all 13 combined commands passed, including 359 Python tests; the
root `--link` dependency check also passed. All seven reviewed file hashes still match.
Evidence: `task17-dependency.json`, `task17-integration-boundary.json` and integration logs.
The owner's approved agent reuse is recorded above. Gizmo drawing and gestures remain unverified.

## Task 12 — Checkpoint measurements and required stops

The standing eight-round AB/BA run completed all 240 captures. Exact parent-observed hash
comparison passed **10/15 cases** and failed all five MaterialLab cases; the gate as a whole
failed. MaterialLab has one stable hash per side in each mode. TemporalLab passed all five
modes in all eight paired rounds. Sponza passed the five-case parent-hash comparison, but the
full-scale MetalFX pair differed in round 4: the parent produced a second hash while the
candidate stayed stable. Sponza paired equality is **39/40**, TemporalLab **40/40**. The parent
matched the historical baseline 15/15 in seven rounds and 14/15 in round 4; the candidate
matched 10/15 in every round. No tolerance or acceptance is inferred from these controls.

| Case | Differing RGB pixels | Pixels >8 | Max channel delta | Mean absolute RGB delta |
|---|---:|---:|---:|---:|
| MaterialLab Off | 1,802 | 1,802 | 141 | 0.172081 |
| MaterialLab Native TAA 1 | 1,885 | 1,847 | 141 | 0.163685 |
| MaterialLab Native TAA 0.5 | 2,894 | 2,207 | 145 | 0.165203 |
| MaterialLab MetalFX 1 | 39,552 | 1,910 | 141 | 0.187946 |
| MaterialLab MetalFX 0.5 | 35,272 | 2,176 | 141 | 0.189113 |
| Sponza MetalFX 1, round 4 | 235,376 | 0 | 2 | 0.097531 |

Every other standing pair has zero RGB difference and identical BMP bytes. MaterialLab rows
above repeat identically in all eight pairs. Both sides' MaterialLab logs report the unavailable
Helmet offline mip chain and load-time fallback; the comparison does not certify the baked path.
Original parent/candidate BMPs, all paired metrics and six before/after/difference visuals remain
in `task12-standing-rounds/` and `task12-image-review/metrics.json`. Difference visuals magnify
absolute RGB deltas eightfold; they do not change the captures or the equality gate.

LightLab direct `parity.py` runs passed **5/5 modes**, **10/10 paired images**, in two AB/BA
pairs. A separately retained parent pilot establishes only an external measured parent baseline;
its initial zero-placeholder reference matches 0/5 by construction, and is not a renderer gate.
The original live standing reference was unchanged during these captures. Full `MTL_DEBUG_LAYER=1 xmake test -P . Tests/gpu`
passed (48.977 s). The pinned validator passed all **107 documents** after the explicitly recorded
positive writer fixture refresh (66 assertions). Task 12 tool checks passed **13/13**, including
363 Python tests; separate review passed 18 targeted tests, 10/9 selftests and four CLI probes.
All App/runtime-shader/document freeze hashes matched their pins after captures. The old
reference and side-specific capture references are retained unchanged outside the checkout.

The original shared document-pin preflight passed for the parent and refused the candidate's
new Sponza JSON hash before rendering. Four reviewed tool files add side-specific frozen document
pins, with every image hash and other reference field required to match the common anchor, and
an explicit five-mode LightLab option. Default fifteen-case checks, existing test assertions,
Khronos validation, comparison rules and capture controls are preserved. These additions expand
the Task 12 file list. Tests-first API failures and a macOS `/var` versus `/private/var` fixture
path correction remain in `task12-tool-red.log`, `task12-tool-green.log` and `green2.log`.

**Retained required stop:** native automation failed to attach to the copied candidate QA bundle three
times: two `timeoutReached` failures, then `AXError.cannotComplete` after correcting its executable
registration. Execution stopped before further input. At that stop the real App had
38 MaterialLab objects, but native authoring and Blender import had not been exercised. Evidence:
`task12-native/attachment-attempts.json`, `native-app.log`, `task12-checkpoint-state.json`.

The owner replied "yes accept" to the five MaterialLab reference updates and native retry request.
Only those five image hashes and three current document pins changed in `reference.json`, with
scoped acceptance metadata. Sponza/TemporalLab image hashes, exact comparisons and tolerances
are unchanged. `task12-owner-reference-acceptance.json` preserves before/after hashes. The original
10/15 failed gate and Sponza parent-repeat discrepancy remain measured failures/differences.
Retained candidate captures match the approved reference **15/15 in each of eight rounds**.
This is hash reconciliation of existing captures, not a new capture run or parent parity pass;
`task12-accepted-reference-reconciliation.json` preserves that distinction.

After authorization, CUA attached by the QA App's display name. The prior attachment failure's
cause remains unproven. In a separate document copy, `material-lab sphere r0c0` changed from
`[-3,-3,0]` to `[-2.5,-3,0]` in Inspector; Cmd+S cleared the dirty title. Only that JSON changed.
The original process exited; fresh PID 25640 loaded the copy and Inspector showed the saved
pose with a clean title. `task12-native/saved-pose.json` records file hash, PIDs and observations.

Blender 5.2.1 LTS imported the frozen unedited document through File > Import > glTF 2.0.
The sphere grid, probe geometry and Lights appeared; the selected first sphere showed Blender's
converted location `[-3,0,-3]`. An initial empty-filename attempt reported "Please select a file";
entering the explicit filename succeeded. This verifies generic glTF opening, not external
LMX asset loading or full rendered material appearance. `task12-native/generic-viewer.json`
records the route and limits. Original native screenshots are displayed in the conversation;
no local screenshot file was saved. Later gizmo/completion gates remain unverified. No milestone
owner acceptance, integration tag, push, PR or merge has occurred.

The final accepted-reference build/CPU/format/direct-policy run passed **13/13 commands**,
including 363 Python tests. Separate final review independently verified 306 evidence checks
and all 18 targeted tests. Task 12 is complete; original failed measurements remain above.

## Mobility validation

[Mobility validation](ux6-validation-mobility.md) preserves Task 13–16 reader/writer/migration, pose-lock and presentation
measurements, failed attempts, scope additions and independent review. Task 16 passed all 12 gates,
880 bridge assertions, 15/15 actual byte comparisons against Task 12, and the native Static/Enabled/Save
checkpoint. Static-tooltip hover and fresh reload were not exercised; Task 22 remains pending.


## Gizmo validation

[Gizmo validation](ux6-validation-gizmo.md) preserves Task 18 model measurements, original failures,
activation-generation scope addition, independent tests and ROOT design review. Native gizmo and
completion gates remain unverified until Tasks 20/22.
