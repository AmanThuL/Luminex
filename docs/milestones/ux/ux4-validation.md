# UX4 validation

**Status**: In progress

The Task 19 combined run is **FAILED / INCOMPLETE** on the restored production App
`874867dd…` at `e8625de`: six BMPs pass 6/6, the eight-round matrix passes 11/15 and fails the gate,
both Compact cost cells fail 1.15×, and both resource runs fail constant allocated bytes.
The original final native checkpoint was **UNVERIFIED** while the Mac was locked.
The [2026-10-01 unlocked-Mac follow-up](ux4-native-followup-validation.md) now records native
endpoints and gestures, all Gallery families/states with visible deviations, and the remaining
required contexts. Completed run execution
is separate from a passing gate or acceptance. [Final results and identity](ux4-final-validation.md)
lead the evidence; [required native contexts](ux4-final-native-validation.md) list every required
completion action. ADR 0029 stays Proposed; the milestone and retained plan stay In progress.
The work is published as [draft PR #64](https://github.com/AmanThuL/Luminex/pull/64).
Milestone review remains separate from publication; no owner acceptance or merge is recorded.

Historical Task 7/12 continuation exceptions waive their stop limits only. Their failures remain.
Tasks 13–18 implementation/documentation records below are complete within their stated scope.
The [publication runtime follow-up](ux4-final-validation.md#publication-runtime-follow-up)
retains the changed shader inventory and its new 6/6 static, 11/15 matrix and GPU PASS results.
Earlier capture outcomes remain tied to their original complete runtime inventories.

## Builds and automated gates

- Main and parent remain at `28ab04a`; implementation branch is `feat/ux4-design-system` in `../Luminex-ux4`.
- Tasks 1–4 are committed: `da05479`, `873b41b`, `3f0eb3`, `ea5169c`. Each passed the release build, unit tests, format check, worktree-root checkers and applicable token freshness check.
- Task 1 retains a failed debug unit gate: MilkTruck baked-key hash was `dc545f78c9574809c6102cf9f411bdcde3cf5eff18c54c6d02d28bec5341129e`, expected `602b44cf9bffe5e549fae7979de90f667fed0db71c5fbe350ce04c5a4314a1d3`. The source pin matched. Release passed; the exact arithmetic cause remains unknown. No hash, tolerance or assertion changed.
- Task 5 implementation and its validation are committed together. Release build, unit tests, format, direct root checkers and token freshness pass. The original fresh review found no production code defect. Fresh retry review confirms the stated Task 5 gesture gate passes in the manually operated instrumented editor. Duplicate clean-build menu verification is additional evidence, not a separate requirement in that task.
- The evidence-only twenty-switch probe passed: all 21 samples retained font atlas ID 2, one platform texture and 384811008 allocated GPU bytes. The instrumentation patch was reversed from production. This does not verify real menu gestures.
- Task 8 fresh parent/head captures failed the six-scene exact gate (5/6) and eight-round exact gate (9/15); complete capture execution and full failure evidence are recorded below. Task 10's corrected full GPU suite passes. Task 19's restored-head GPU group passes; its separate final-check phase is described in [final validation](ux4-final-validation.md#controller-final-check-and-publication-phase).

## System settings and display

Historical Tasks 5/6 settings were observed with Computer Use: Appearance Auto and Reduce Motion off. Task 6 switched Appearance to Dark and back to Auto, and Reduce Motion on and back off. Both restored settings were observed on those revisions. The original final checkpoint could not read current originals while locked; no Task 14–19 OS change occurred. The dated follow-up reads original Auto/Reduce Motion off, visibly restores both and proves both original workspace INIs restored byte-exact; its cleanup record retains process/runtime integrity. Original records exist in `system/` and `task-6/`; restored captures are listed below.

Both observed displays use 2× backing scale. No 1× gesture or typography capture has been verified. Parent release maximized client bounds were 1674 × 1052 points; windowed startup was 1280 × 720 points.

QA wrappers live only in the evidence directory and contain byte-identical executable copies; recorded hashes identify parent release, clean head and instrumented head. Task 5 runs exited normally. Task 6 clean head, motion probe and parent exited with code 0 after validation.

## Decisions

- Selection outline keeps the plan default: one encoded `#4CABFD` constant for both themes. Task 10 applied that constant. Real Light and Dark editor gestures show the same blue perimeter, removed and restored through Selection Outline; the parent retains amber under both system appearances. This closes Task 8's forward outline observation.
- Body size remains 16. Actual Dark/Light Gallery samples at 13/16/20 px, 100% UI scale and default 780-point width on a 2× display show complete digits without overlap, clipping or stacking (Task 12 images 0133/0134). The dated follow-up adds all 13/16/20 specimens at 100/150% in both themes at Comfortable/2× without internal clipping, overlap or a demonstrated need for 17. Actual 1× captures remain unverified.

## Computer Use record

[Original record, unchanged](ux4-history-validation.md#computer-use-record).

### Parent debug baseline

[Original record, unchanged](ux4-history-validation.md#parent-debug-baseline).

### Parent release baseline

[Original record, unchanged](ux4-history-validation.md#parent-release-baseline).

### Instrumented Task 5 head

[Original record, unchanged](ux4-history-validation.md#instrumented-task-5-head).

### Blocked Task 5 gesture

[Original record, unchanged](ux4-history-validation.md#blocked-task-5-gesture).

### Authorized Task 5 retry

[Original record, unchanged](ux4-history-validation.md#authorized-task-5-retry).

### Task 6 system appearance and native windows

[Original record, unchanged](ux4-history-validation.md#task-6-system-appearance-and-native-windows).

### Task 7 stopped after three failed reviews

[Original record, unchanged](ux4-history-validation.md#task-7-stopped-after-three-failed-reviews).

### Authorized Task 7 retry

[Original record, unchanged](ux4-history-validation.md#authorized-task-7-retry).

### Task 8 current-head checkpoint

[Original record and all eight visible Figma deviations, unchanged](ux4-history-validation.md#task-8-current-head-checkpoint).

### Task 8 exact-image results

[Original record, unchanged](ux4-history-validation.md#task-8-exact-image-results).

### Task 9 typography verification

[Original record, unchanged](ux4-history-validation.md#task-9-typography-verification).

### Task 10 shape and density verification

[Original record, unchanged](ux4-history-validation.md#task-10-shape-and-density-verification).

### Task 11 Gallery verification

Commit `c3c95a2` adds the eighteen initial Figma sets with named renderers and all specified states. Test-first catalog compilation failed on the missing header, then passed with unique exact Figma names. All seventeen global checks pass, including 45 literal-checker cases and 301 Python cases. The initial build used two nonexistent role names; corrected compilation passed. Earlier formatted semantic-label and local-class separator failures remain in `task-11/pre-format-allowance-correction/` and `pre-layout-correction/`. The policy allowance covers only the exact actor-chip UI label in its Gallery expression; comment and raw-string narration remain rejected.

The real ImGui CPU probe passes 144 component/theme/density/width combinations and six parent/preview combinations. It verifies all 63 selected slots, sampled parent semantic/color restoration, opaque child backgrounds, responsive grids, font/variable stack restoration, detached flags and no Gallery INI entry. Its initial zero-stack assertion failed because ImGui owns one default-font entry until Render; before/after identity and size evidence shows the Gallery preserves it. No production change was needed. A header overlay adding an unrendered enum value fails actual compilation with `-Werror,-Wswitch`; unchanged production compiles and source hashes remain frozen. These probes establish no native capture or typography gesture. Evidence: `task-11/gates.json`, `gallery-probe.log`, `exhaustive-proof.json` and source-freeze records. The first fresh review requested checkbox fills, selected-tab/topic-header Medium, four state accent cues, a trailing topic Reset and US spelling. An extended RED probe reproduces the item-color/font/row defects in both themes. The corrected GREEN verifies 32 item-bound color-membership checks, 12 Medium identity checks and eight header/reset bounds checks at narrow and default widths; all seventeen corrected gates, broad Gallery and enum probes pass. A retained aggregate probe failure came from Light SurfaceActive and BorderSubtle sharing `#DEE0E2`; strict per-item fills remain checked in both themes. Original results are archived under `pre-review-correction/`; Fresh corrected review passes with no blocking findings. Its independent triangle-interior probe verifies 16/16 checkbox fills; an external wrong-Hover mutation fails all four Hover cases, including Dark where hover and border share a color. The earlier membership checks alone cannot distinguish those fills. Native captures, visual correspondence and cost remain unverified until their checkpoints.

### Task 12 cost gate failure

The prescribed control-radius sequence 3, 2, 0 did not pass the cost gate. Initial frame-1 default-layout resets left Performance summary selected and Console hidden in both builds. At matching 1674 × 1052 points and 2× backing scale, radius 3 produced 16179 indices and radius 2 produced 16233 against parent 7239; both fail 1.15×. Other initial theme/density runs changed native window width by one point and remain geometry-mismatch evidence. These initial summary-selected runs do not establish the intended Console-selected default gate.

The final radius-0 protocol applies the same predetermined original rectangle (54, 65, 1674, 1052) in both builds and resets docks after geometry settles. All five runs match that rectangle, 1674 × 1052 display size, 2× backing scale and the intended Console-selected default. The replacement parent count is 9993. Dark and Light Comfortable each produce 11187 (1.119484×, passed); Dark and Light Compact each produce 11691 (1.169919×, failed), exceeding the unchanged limit 11491.95. Original protocols and every failed/mismatched run are retained separately under `task-12/metrics/`; no best-count selection or tolerance change was used.

The third candidate triggered the stop limit; the owner waived the cost stop and requested the remaining tasks before the later native binding timeouts. Compact stays failed, and the gate is **FAILED / INCOMPLETE**. The uncommitted production change sets control radius to 0; no other cost optimization or retest followed. All seventeen resumed global checks pass in `task-12/globals-resumed/gates.json`, with 951 source files unchanged in its freeze verification. These automated checks establish no missing native gesture.

Real parent, head Dark windowed and partial head Light windowed/maximized workflows are recorded below. Dark maximized workflows remain unverified. Gallery images 0133/0134 support body 16 at actual 2×; 1× remains unverified. The per-area pending matrix retains whole-source cues, Graph pan, held fly-camera input, unsupported fallback and all missing workflow cells without inferred passes.

Editor input/binding then returned `timeoutReached` three times: AX read after Cmd+Q, fresh app binding after restart, and runtime reset followed by fresh binding. Original errors remain in `task-12/cua/native-binding-failure.json`; corrected chronology is in `supporting-audit/native-binding-chronology-audit-0156.json` under Task 12. No editor action after relaunch was observed. The restarted owned QA process ended through handled SIGTERM with exit 0; this supplies no native Quit evidence. Images 0155/0156 verify Auto and Reduce Motion off retained, with System Settings AX still available.

The automated checkpoint's original source/binary reversal proof remains in `task-12/metrics/restoration-proof.json`. After native verification and process exit, both original INIs were restored byte-exact in `task-12/cua/post-binding-failure-workspace-restoration.json` (head `1f1e1a2780df02bb3a0e47f0687e8236407e74fdaeaeaa8b1100c223955e6134`, parent `dcc831fbaf29b07a596e55c49e36bc2eba508ec58ef264d49910c111591e2b6d`). Main and parent are clean; the radius-0 head App is uninstrumented. No QA App remains running. This historical checkpoint preceded Tasks 13–19; later task records below retain the current execution state.

Recorded Task 12 actions, expected results, observations and screenshot paths are retained in the [parent](ux4-parent-validation.md), [head Dark](ux4-head-dark-validation.md) and [head Light](ux4-head-light-validation.md) gesture companions. Their stated cutoffs and unverified rows preserve partial completion-gate coverage. The [deferred coverage matrix](ux4-deferred-validation.md) lists missing actions and their required contexts. The immutable `task-12/supporting-audit/gestures-doc-snapshot-0156.json` preserves original observations, audited corrections and the omitted-but-performed 0152 record without changing live JSON.

### Task 13 model implementation

The pure provenance and activity models are implemented with owned source strings and the existing measurement phases. Both test files failed compilation on missing model headers before implementation; afterward the production AppModel build and 17 focused cases (140 assertions) passed. Changed-source formatting passes. Two Tests target build attempts stopped on sandbox-denied Metal compiler cache writes; focused GREEN uses a CPU-only executable linked to the freshly built AppModel archive. Exact commands, logs and test snapshots are in `task-13/report.md` and its companion evidence files. The first root build and unit suite passed, then policy rejected the required Actor enum value and three comments. The first recognizer was rejected on macro/comment bypasses; the corrected lexical handling preserves the exact declaration and path restriction, with 26 targeted policy tests (`task-13/policy-lexical-fix/`). A second root run found two comment-envelope/attachment defects, corrected without behavior changes and verified against Clang (`task-13/cpp-doc-focused-verification.json`). Earlier failed runs and reviews are retained. The final candidate passed fresh independent review and all 17 global checks, with 961 frozen files unchanged; it was committed as `e9de25b` (`task-13/review-final-candidate.md`, `task-13/globals-final/`). This establishes no UI gate or acceptance.

### Task 14 marks and activity implementation

Actor/provenance helpers now mark subject and field baselines, generated children, actual CLI rig overrides, declared effective controller scale and editor status reports. Transport activity uses existing measurement/capture/document state and observed controller changes, with the original Stop path and readout retained; fit tests cover verb/readout/zoom contraction. Gallery contains exactly 21 sets, including the seven reserved software lifecycle fixtures. Compiler RED precedes fit implementation; focused production-based GREEN covers 398 assertions in 25 CPU cases, 27 ImGui component checks and 29 lexical policy tests. Changed sources compile and format; comment/layout checks pass. Exact commands, failures and source snapshots are retained in `task-14/report.md`. Synchronous document execution cannot display an intermediate running frame; only existing queued/dialog/confirmation states are presented. Unfollowed camera-rail poses omit actor attribution when existing state cannot establish their origin. The first independent review found field-mark clipping at supported widths. Reserved label space and an explicit stacked-column weight correct it; an independent ImGui probe using shipped density metrics passes 640 placements at 75/100/125/150% scale. The original review and 47 valid RED counterexamples remain in `task-14/mark-fix/`; the first draft probe incorrectly inspected value controls in wide checkbox rows and is excluded from the RED claim. A second fresh review found the separate subject-header path still clipped long asset names. Reserved header space, wrapping and an explicit column weight correct it; 288 fresh/settled draw-data checks pass in `task-14/header-fix/`. The implementation checkbox is complete. The final fresh review passes, including 4,192 independent header target/tooltip checks, and all 17 global checks pass with 962 frozen files unchanged (`task-14/review-final-candidate.md`, `task-14/globals-final/`). The implementation was committed as `3606003`; native verification was unverified at that checkpoint. The [Task 14 native checkpoint](ux4-marks-validation.md) records the locked Mac and every deferred gesture as unverified; no native result is inferred from CPU probes.

### Task 15 shared menu implementation

The owned menu tree now captures existing actions, state, reasons, catalog retry and passive Controls help; ImGui and keyboard intents use one named route. An empty-tree production stub failed nine behavior cases before implementation. Fresh AppModel-linked GREEN passes 24,637 assertions in ten cases; the CPU event probe reproduces then corrects four missed physical Command chords and passes 98 cases. Supporting zoom routing retains its original aliases and suppression; unavailable C still restores the existing recovery notice. Frame Selected retains the current renderer aspect without stored renderer references. Evidence and retained failures are in `task-15/report.md`. The first fresh review passes its functional audit and independent 1,092,592 model assertions and 104 keyboard checks, with one obsolete-menu cleanup finding. The unused scene-menu API and renderer were removed; the final fresh review passes and all 17 global checks pass with 965 frozen files unchanged (`task-15/review-final-candidate.md`, `task-15/globals-final/`). The implementation was committed as `36f9f7d`. These CPU checks establish no native pass.

### Task 16 native menu implementation

The AppKit adapter projects the owned menu model into Luminex/File/Edit/View/Window/Debug/Help and drains each command through the shared route. Native polling ownership includes the separate zoom path; actual InputText focus controls Edit delivery. Invisible-window probes pass 229 checks, focused production CPU suites pass 24,742 assertions in 25 cases and six shell translation units compile with production flags. Retained RED cases exposed whole-menu fallback, SDL translator focus and window responder defects; their corrected probes pass (`task-16/report.md`). These checks establish no native gesture. The [native menu checkpoint](ux4-native-menu-validation.md) records all required commands, focus cases, appearance/settings, detached access and resize contexts as unverified while locked. The final fresh implementation review passes and all 17 global checks pass with 968 frozen files unchanged (`task-16/review-final-candidate.md`, `task-16/globals-final/`). Two earlier FAIL reviews and the public-comment/layout gate failures remain retained. The corrected production App passes an automated four-frame Metal-debug smoke with zero skips and exact original INI restoration (`task-16/editor-smoke-final/`); this establishes no native gesture or actual skipped-acquisition result. The implementation is committed; native verification was unverified at that checkpoint.

Fresh Task 16 review found stale pre-NewFrame focus and reversed F→Home camera effects. Corrections bind native intents to consumed ImGui event IDs and modifiers, resolve ownership after all panels, and consume framing in command order with the current renderer. CPU UI processing now completes before drawable acquisition so skipped drawables cannot stall focus resolution or document intents. The public-field comment attachment and checklist's 80% preset are corrected. Focused evidence passes 55 event checks, 291 AppKit/SDL checks, eight camera-order cases, the existing 25-case CPU suite and six shell object compiles (`task-16/fix-report.md`). The next review retained two P2 findings: literal delegate-policy refusal and incomplete multi-viewport lifecycle on skipped drawables. Controller probes also found stale availability dropping F and orphan helper separators. The final correction separates policy refusal from raw-intent observation, rechecks current eligibility after panels, completes platform lifecycle on skips, restores renderer NewFrame order and compiles portable menu helpers. Fresh evidence passes 59 focus checks, 575 adapter/policy/availability checks (including the prior 291), 24 field/selection checks, 17 skipped-frame/workflow checks, eight camera cases, 25,138 assertions in 39 CPU cases and six production-flag object compiles; assertion-enabled pinned ImGui passes two consecutive skips and a control. The first extended lifecycle probe failed identical candidate/control teardown assertions; its missing platform cleanup was corrected in a new harness without changing production. Exact RED/GREEN results and limits are in `task-16/final-fix/report.md`. Original failures remain; fresh independent review and all 17 global checks pass (`task-16/review-final-candidate.md`, `task-16/globals-final/`); every native gesture was unverified at that checkpoint.

### Task 17 Gallery completion

The catalog matches exactly 21 Figma names and now verifies each name’s renderer identity. Source/export inspection found missing Capture pending and Applied proposal/Revert fixtures, reproduced by a calibrated 16-of-64 failure before the panel correction. Current focused catalog checks pass 106 assertions; the state probe passes 64 checks in both palettes at 1.0/1.5 font scales. The initial two probe failures were harness errors and remain separately retained. The [Gallery checkpoint](ux4-gallery-validation.md) records each component, source/export observations and all four native capture contexts as unverified at that checkpoint; these CPU scales establish no native zoom or Geist ink result. The pre-review policy failure and correction preserve the existing narrow fixture allowance without changing its checker (`task-17/controller-policy-fix/`). Fresh review passes with 892 independent CPU checks; all 17 global gates pass. The metadata correction passes fresh review and all 17 final checks before commit `03050d5` (`task-17/globals-final/`).

### Task 18 operator documentation

Commit `e8625de` records the source contract in [Proposed ADR 0029](../../decisions/0029-design-system-token-contract.md),
[App design-system architecture](../../architecture/app-design-system.md) and
[editor workspace guide](../../guides/editor-workspace.md). README/AGENTS describe current features
and operation. Documentation supplies no missing native pass or acceptance.

## Remaining verification

The combined final run is recorded [FAILED / INCOMPLETE](ux4-final-validation.md).
Historical Task 8 retains 5/6 BMP and 9/15 matrix failures; historical Task 12 retains
1.169919× Compact failure and [152 audited gesture records](ux4-deferred-validation.md).
No old-revision pass transfers to the final head. The current inventories retain the
[marks checkpoint](ux4-marks-validation.md), [127 native-menu actions](ux4-native-menu-validation.md),
[84 Gallery cells in four contexts](ux4-gallery-validation.md#per-component-native-checkpoint),
and [historical deferred actions](ux4-deferred-validation.md).

Fresh independent review, all 17 checks, the serial documentation commit and push/PR are the
controller's final-check/publication phase. Their frozen results must accompany publication;
this record does not predict them. The controller must preserve the validated App identity or
identify and revalidate a changed binary. No acceptance, plan closure or merge is authorized.
