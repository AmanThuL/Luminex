# UX6 — Gizmo validation

**Status:** Frozen — non-normative; a task-time snapshot. Current results are in [final measurements](ux6-validation-final.md) and the [milestone review](ux6-review-validation.md)

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


## Task 19 — Tools, menu and guarded keys

View > Gizmo offers View Q, Move W, Rotate E, Scale R, Transform Y and one World/Local X toggle,
with checked current choices. Native and non-Apple dispatch consult the shared keyboard ownership
rule; text input, right-mouse look, popups and detached focus leave tools unchanged. Measurement
permits harmless tool selection. The Shell retains transient Move/World state without serialization.
Toolbar choices follow Step and disappear before activity verb, time or zoom when width is tight.
Original zero-tool-width fits and right-mouse W/A/S/D/Q/E camera input are unchanged.

Tests-first RED: missing Gizmo menu row, 1 case/1 assertion failed; then missing
`EditorShortcut::Gizmo` compile failure. First GREEN passed **27,294 assertions in 34 cases**.
Review found the reused Movable glyph's missing-font fallback would say Movable instead of Move.
An optional label override now supplies Move to both width and drawing; existing defaults stay
unchanged. Formatted, rebuilt final and independent runs each passed **27,306 assertions in
34 cases**. Tests exhaust 16 focus flag combinations, all five tools/two spaces during measurement,
menu identity/checks, glyph codes and exact toolbar drop boundaries. App compilation includes native
menu and toolbar wiring; native gestures and the non-Apple branch runtime remain unverified.

Approved file-list additions: `EditorTransport.cpp`, the actual toolbar caller; `AppMenuBarFitTests.cpp`
for drop-order evidence; `EditorStyle.h/.cpp` for the explicit fallback-label override. The other
17 files are planned. Seven glyphs resolve in the pinned Codicons font, independently inspected.
Original incidental failures remain: one guessed Codicons CSS URL 404 (pinned npm package worked),
one rejected multi-target xmake invocation (separate builds passed), and two guessed nonexistent
source paths. No issue reached three corrective failures; no assertion or tolerance was loosened.
The existing-shortcut inventory still checks every prior binding; the new six are checked separately.

Evidence: `task19/task19-freeze-20261003-01.json`, all original/first/final logs in `task19/`,
`task19-review-receipt-20261003-01.json`, and `task19-root-review.json`. Source freeze covers 21
files; no Core/Render/RHI/shader or workspace/capture/measurement schema changed.
`task19-final-gates.json` records **12/12 passing commands**. No remaining source review finding.


## Task 20 — Viewport wrapper

A fresh strongest-model implementation agent wrote an evidence-only headless ImGui harness before
production code. It compiles the actual viewport wrapper with the pinned ImGui/ImGuizmo archives;
Tests does not gain an AppShell or ImGui dependency. Missing-wrapper RED was recorded, then the
compiled no-op wrapper failed **12/12 behavioral scenarios**. One runner setup error attempted
to link the header-only spdlog archive; removing that lookup corrected setup. Original logs remain.

ROOT approved explicit shell-owned `ViewportGizmoLifecycle` storage and an optional lifecycle
pointer appended to the required context, plus `EditorShell.h` wiring and the
`EditorThemeApply.h` adapter declaration. A missing owner fails closed.
This keeps suppression after cancellation until mouse release without a hidden global drag registry.
Evidence is under `task20/`. Final formatted actual-wrapper tests passed **17/17 scenarios**;
App build, rebuilt model tests (**628 assertions, 10 cases**) and format check passed.
Independent actual-wrapper verification passed **17/17 scenarios** and independently rebuilt
focused tests passed **27,934 assertions in 44 cases**. `task20-final-gates.json` records
**13/13 passing commands**, including the Python suite. ROOT and separate source reviews passed.

The literal planned `--session` with `--screenshot` combination is rejected by the existing CLI.
Verification used editor-session `capture.screenshot` with the selected helmet and compared
actual output bytes. Native GPU capture includes the UI pass; its scene-encoder scope remains
unverified. Neither source inspection nor file existence certifies a
scene-only trace. No Render or capture-schema change is authorized to force this gate.

Pinned-library defects exposed by the actual harness were corrected in the wrapper: BeginFrame
resets accumulated handle hits; the returned matrix, rather than the vendor bool alone, detects
repeated equal drag deltas; finite inverse and finite positive squared-axis norms prevent nonfinite
UI vertices from singular or extreme authored scales. No vendor patch or Core tolerance changed.
Combined uses SCALEU so World Move/Rotate stays World. Scaling itself uses local TRS axes in this
pin; no World Scale proof is claimed. For a 45-degree Z object, measured translations were World
`(0.288675,0,0)` and Local `(0.288676,0.288676,0)`.

Original failures remain in `task20/`: nine initial activation failures, repeated-delta failures,
singular/extreme UI-vertex failures, an added test click outside measured handle bounds and a
missing algorithm include. The calibrated Combined and extreme-scale RED source/harness snapshots
are retained; unchanged assertions passed after fixes. Each implementation issue had one successful
correction; no issue reached three failed corrective attempts. Tests do not certify native gestures.
`task20/source-freeze.json` freezes nine source/data files, harness/runner, archives and App; ROOT
verified those source hashes and saved a physical App/runtime/document freeze separately.

Native QA used physical App SHA-256 `426391e96a0e7419805129b033322538117bd1661ab219ab588756fcce9a6d7f`
and separate copied documents. Helmet Move/World handles were observed; Q selected View and hid
them with unchanged Inspector pose. Both operator-approved 32-frame session PNGs were exactly
**469,607 bytes**, SHA-256 `c5543f0a10a7304e05631dcfcd1e98c22d2a79aa77adc56dec3c4232ec8e228d`.
`task20-native/screenshot-comparison.json` records the actual pair. This passes the screenshot
isolation check; scene-only GPU capture remains unverified. Other native gestures remain Task 22.
The first bundled launch omitted Resources font/icon links; the corrected launch loaded all faces.
That setup error and one capture approval timeout are retained, without renderer-failure claims.
Independent receipts live under `task20/independent-review-20261003-174050-5331bd60/`.
