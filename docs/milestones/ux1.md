# UX1 — Editor Usability and Diagnostics

**Status**: Implemented — owner accepted integration after manual review; evidence limits retained

All P1–P3 areas and manual-review refinements are implemented. On 2026-09-14 the owner accepted
the result and requested local-main integration, a PR and merge when ready. The executor plan
is closed and removed. The [roadmap](../roadmap/editor-experience.md) owns scope; the
[implemented design](../specs/2026-09-14-ux1-editor-experience-design.md) owns presentation.
Owner acceptance does not certify every automated check below. M7.1 remains inactive.

## Baseline and integration

Work started in a sibling worktree from local main `5dee208`, preserving the rebuilt parent App,
shaders and original workspace outside source before editing. The rendering baseline remains M6.5
plus R1. [Gate B](interface-gate-b.md) and [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md)
remain accepted; editor-local names and indices do not implement GPU scene identity.

The first two local stage commits are `64a9e60` (coherent diagnostics) and `8d57e6f` (subject recovery
and diagnostic workflows). The final stage adds action recovery, a monotonic measurement revision,
queued scene-load feedback and measured layout refinements. Integration follows the owner's manual-review acceptance.
The chronology below preserves original results and limitations rather than implying blanket validation.

## Implemented behavior

### Shared presentation and truthful state

The default main window remains maximized to usable display bounds with a native macOS title bar.
Hierarchy, Viewport, Inspector and bottom Performance/Console tabs dock together; Render Graph is an independent window
that cannot dock. Schema 2 and saved visibility/layout continue to restore; Console is an additive optional visibility key.
Reset Default Layout gives Performance a full-width 300 pt bottom region in the measured large
workspace and 210 pt in compact windows. The larger height exposes multiple pass rows and the
interval plot together. Final native review uses the default fullscreen-windowed size; earlier
1280 × 720 pt evidence is supplemental, following the requested review preference.

Shared field/spacing helpers distinguish controls, telemetry and exceptional states, wrap long
content and reflow narrow rows. Shared delayed hover help explains non-obvious effects, units,
reset scope and disabled controls without replacing visible failure reasons. Rendering Inspector has Exposure, Bloom, Shadows, Reconstruction,
Resolution and Display & Details groups. Camera/object/light fields identify XYZ, angles, world
units and scene-linear RGB. Reset defaults and operator controls are documented in the
[GPU debugging guide](../guides/gpu-debugging.md#restore-editor-settings).

Requested and effective reconstruction are separate. Off retains requests while reporting actual
full-resolution output and inactive dependencies. Vendor fallback retains the request and reason.
Last-reset reason and device frame form a retained event. Live retired-frame timing is distinct
from the controller's last observation and consumed-frame cursor. Unavailable data is N/A or
Waiting; measurements are never fabricated as zero. These observations update while panels hide.

### Coherent metrics and graph inspection

Declared frame metadata travels with its retained record: object/draw counts, viewport logical
size, render/output pixel extents and configuration revision. Revisions increase on every
configuration change, including A→B→A, so delayed frames cannot re-enter a new sampling window.
Performance Freeze retains all summaries, rows and plot together; Clear while frozen is explicitly
empty; Resume accepts fresh retirement. The table sorts numeric costs, offers schedule order and
retains full pass IDs. Plot units, sample/window semantics and the 60 Hz reference are explicit.
Timed pass sum still excludes presentation, driver work and untimed work.

Captured alternating GPU records proved the Graph invalidation cause: physical `sceneDepth0/1`
and `historyColor0/1` names changed the canvas signature. Only these known pairs normalize for
logical identity; each frame retains its exact physical resources. Real topology changes remain
visible. Live Graph now publishes an owned whole record and matched timing set every 0.25 s,
sharing Performance's 4 Hz cadence. Values remain exact-frame latest timings rather than averages;
first data and Resume publish immediately. Surviving pass/sink selection maps by logical identity. Freeze owns a compiled record and
only its matching timing; it survives later frames, scene changes and window close/reopen. Dump
latches the displayed record. Fit graph, Fit selection, 100%, group expansion, long resource
copy/details and narrow detail stacking are available. The maintained node-editor patch exposes
the zoom setter needed by the explicit 100% action.

### Subjects, animation and diagnostics

Hierarchy keeps clearable search/counts above compact indented tree rows: Workspace, the active
scene, Lights, Objects and applicable source-name primitive groups. Grouping is navigation over
the flat scene, not parent-transform semantics. `Hierarchy###Scene` preserves saved window identity.
Catalog loading moves to `File > Open Scene`, including unavailable reasons and failure/Retry.
Rows use authored node/mesh names and material qualifiers for multi-primitive meshes, with local
disambiguation and full-name hover/copy/search. A filtered selection stays explicit in Inspector.
Scene selection first presents Loading, then loads at the next UI boundary. Failure keeps the
current scene, selection and search, with a persistent reason and explicit retry.

Geometry-derived local bounds transform through all eight corners. Frame selected preserves view
orientation/FOV and adapts camera position, near plane and movement speed to the reliable bounds;
the editor composites a soft warm outline along visible selected geometry, without transform
handles. A Viewport toggle hides it. Output-resolution unjittered selected-only R8/D32 coverage and separate scene-depth
visibility use shared alpha-mask coverage. The SDR composite detects the true silhouette and
depth-tests its source and destination; foreground occlusion cuts are not selection edges. Only an enabled, visible object cue adds these measured Graph passes.
Thickness follows the Viewport's own backing scale. Mismatched outline extents are disabled;
a renderer-resize failure exits before declaring a frame against partially replaced resources. Framing does not solve occlusion or split authored
material batches into semantic objects. San Miguel's source-named `CafeChair_Metal`, for example,
is a broad thin batch at ground level; framing its bounds can leave other geometry in front.
The outline never writes scene color/depth, temporal history or ordinary scene-only capture output.

SceneSession retains authored object/light defaults. Object Reset samples an animated object's
track at the current playback time and collapses only its previous transform; it preserves other
edits. Rendering Reset is scoped to one of six groups. Viewport owns Play/Pause, Step 1/60 s, Reset
time, playback/time state and camera-rail control. Text entry suppresses camera/capture input.
Every temporal diagnostic has a shader-derived legend and Return to Final; Raw-specific caveats,
vendor-disabled views, MaterialLab axes and lab-purpose text are explicit.

### Recoverable output actions

Dump and capture share Unavailable/Ready/Pending/Succeeded/Failed results with owned messages and
paths. Capture capability comes from the actual process/device, and menu/C requests coalesce.
The frame loop completes the result after capture closure. Persistent notifications can be
dismissed while preserving the last result. Copy reports clipboard errors. Reveal uses a
shell-free, checked Finder request for the exact file/bundle; a missing output reports a separate
path-action error without rewriting the original capture outcome.

### Console

The bottom Console tab retains existing project logs from startup through a Core-neutral scoped
subscription and App-owned thread-safe store; terminal output remains. Storage is bounded to
2,000 entries / 2 MiB payload / 16 KiB per message, with explicit eviction/truncation counters.
Severity and case-insensitive text filters, UTC timestamps, Freeze/Resume display, Clear,
Copy visible with persistent clipboard feedback and follow-only-at-end scrolling are available.
Frozen display retains its complete snapshot while ingestion continues; Clear empties both views.
Console does not execute commands. `Window > Console` controls its persisted visibility.

## Validation record

Evidence is retained outside published source in the local `ux1` evidence bundle. Filenames below
are relative to that bundle. Original failed runs remain available. The host is Apple M3 Max,
macOS 26.5.2 (25F84), with SDK 26.5 and Retina scale 2. Main-window final review uses a 1674 × 1052 pt maximized client area; screenshots are
downscaled to 1187 × 768 by the UI tool. The Inspector and metadata distinguish
logical viewport points from render/output pixels. No UI capture is a performance benchmark.

| Area | Observed evidence | Result and limit |
|---|---|---|
| Build and policy | `stage1-*`, `stage2-*`, `stage3-*` logs | Each source stage builds, passes the full CPU unit suite, format and all six direct policy checkers, including link/header/comment/layout checks |
| Native layout | `baseline/`, `ui-15`, `ui-18`, `ui-25`–`ui-27` | Original/reset/compact and maximized layouts inspected; final 300 pt Performance exposes multiple rows; compact evidence predates the maximized-only review preference |
| Requested/effective state | `ui-19`–`ui-24`, AppTemporalEditorState/DynamicResolution tests | Real Raw, Native and MetalFX; vendor-disabled view tooltip; temporal Off; manual scale and active/inactive dynamic controller. Unsupported-device/fallback presentation is covered by CPU fixtures, not unsupported hardware |
| Metrics | `ui-39`–`ui-41`, AppPerformanceModel/FrameRecordRing tests | Freeze, step-independent snapshot, frozen Clear/N/A and Resume observed; revision re-entry and metadata pairing covered by regressions |
| Graph identity and ownership | `graph-before/`, `graph-after/`, `ui-04`–`ui-07`, `ui-43`–`ui-49` | Before regression fails, after passes. Native selection retained over 44 seconds; matched frozen frames 16915 and 49902 dumped; topology removal/resume preserves surviving scene selection; Fit/100%/wheel zoom/group expansion observed |
| Subject recovery | `ui-11`–`ui-13`, `ui-29`–`ui-38`, `ui-42`, `ui-55`–`ui-59` | All six installed scenes visited; search/filter explanation, source names, bounds and framing; malformed local asset failure/retry; light and animated transform edit/reset at paused 18.050 s observed |
| Diagnostics/playback | `ui-max-native-*`, `ui-diagnostic-*-raw`, `ui-33`, `ui-36`–`ui-41` | All six native overlays plus Final, Raw caveats, MaterialLab axes, pause/step/reset; independent visual review in `visual-qa-inspector.md` |
| Output actions | `ui-03`, `ui-16`–`ui-17`, `ui-26`–`ui-28`, `ui-46`, `ui-50`–`ui-51`, `ui-54` | Disabled startup, enabled real capture, safe file-parent failure/retry, exact dump frame, copied path, Finder-selected trace, missing-output error and menu/C parity |
| Workspace | `ui-hidden-*`, `ui-47`, `ui-52`–`ui-54` | All four menu visibility toggles, detached Graph close/reopen and hidden-Inspector persistence across relaunch observed; Reset restores all panels |
| Input gestures | Native CUA attempts | Text-entry ownership observed again in maximized `ui-61`. Continuous RMB+WASD/QE, docking/node dragging and graph panning remain unverified: the tool lacks held mouse input and its drag attempts did not produce an observed move |

### GPU validation limitation

The required full `MTL_DEBUG_LAYER=1` GPU run fails in eight MetalFX-encoding cases on both the
preserved parent and this branch. LLDB reports the same Objective-C exception:
`-[MTL4DebugComputeCommandEncoder globalTraceObjectID]: unrecognized selector`, through
`MFXDevice4::emitSignPostForComputeEncoder` and `_M4FXTemporalScalingEffectV4`.
No private-selector shim, renderer workaround or suppression was added.

The full validation-off suite passes 173 GPU cases / 252443 assertions. Validation-on excluding
MetalFX encode passes 164 cases / 252105 assertions; the separately validated vendor-pack case
brings coverage to all 165 non-MetalFX-encode GPU cases. Capture-enabled tests also cover real
trace creation and safe failure, and capture-disabled tests cover process capability. Exact
commands, binary hashes, baseline comparisons and logs are in `gpu-validation-matrix.md`.
This is an existing host/runtime limitation, not a full validated MetalFX pass.

### Fixed-camera image evidence

The saved parent and final App render all six scenes at 1280 × 720 pixels, Native TAA, 32 frames,
with validation enabled. This scene-only image oracle is independent of the native window-size
preference. Milk Truck, MaterialLab and TemporalLab match byte-for-byte initially. Sponza and
Helmet initially differ in 67 and 11 pixels respectively; repeated old/current runs both match
the original baseline hashes, demonstrating within-binary drift. San Miguel's preserved baseline
itself changes 645 pixels (maximum 8/255) on repeat; exact cause remains unproven. Runtime MSL is
identical after removing absolute `#line` source paths. See `matched-images/comparison-findings.md`
and its original/repeat BMPs, manifests and pixel summaries. No blanket exact-hash pass or visual
quality/performance improvement is inferred. Historical M6/R1 image exceptions remain open.

## Integration acceptance and retained limits

The owner accepted the visible result after iterative manual review and authorized integration
and PR merge on 2026-09-14. The completed executor plan is removed; M7 is not started here.
Held RMB+WASD/QE, docking/node dragging and graph panning remain unverified by the automated
UI tool. The owner's general acceptance is not a specific scripted gesture result. CPU fixtures
still do not constitute native unsupported-vendor tests. The reproduced MetalFX debug-runtime
exception and San Miguel image drift remain unresolved with original evidence preserved.

Full picking/gizmos, general Undo, asset tooling, broad themes/layout presets, HDR/EDR and renderer
feature expansion remain outside UX1. Existing module, motion, color and capture contracts remain
unchanged. Earlier open-acceptance notes below describe their review stage; this section owns
final integration disposition without upgrading those historical results.

## Manual review revision evidence

The design, roadmap and executor plan were edited before source for the user's five requests.
The revision implements actual-object outlines, Hierarchy/menu loading, hover help, Graph 4 Hz
publication and Console. Original evidence above remains historical; new artifacts live under
`manual-revision/` in the same external evidence bundle.

- Build, full CPU unit suite, formatting and all direct policy checks pass. Console tests cover
  entry/byte limits, UTF-8 truncation, concurrent ingestion, filters/copy, snapshot freeze/clear
  and subscription lifetime; schema-two tests preserve old visibility without rebuilding docks.
  Graph regressions cover whole-frame publication, timing matching, retention and Freeze/Resume.
- Metal validation passes 169 non-MetalFX-encode GPU cases / 252190 assertions, including the
  vendor-pack case and four new outline cases (68 assertions). The complete validation-off
  suite passes 177 cases / 252511 assertions. The known MetalFX validation exception remains.
  Outline oracles cover visible
  borders, unchanged interiors, full/partial occlusion, alpha-factor/cutoff/UV/back-face coverage,
  backing-scale thickness and byte-identical source display/HDR/TAA history with the overlay.
- Maximized native `ui-01`–`ui-11` show Sponza fabric and Helmet contours, outline off, hierarchical
  source rows, File catalog, actual malformed-asset failure and successful Retry, persistent
  Console errors, startup logs, frozen logs surviving a scene load, filtered copy success,
  frozen Clear and default layout. The original asset was restored and its SHA-256 verified.
  `ui-06` and `graph-dump-frame-29921.txt` agree on frozen frame 29921; Resume retains the scene
  node selection through ongoing Native TAA frames at the labeled 4 Hz cadence (`ui-07`).
  Final-build `ui-12`–`ui-14` repeat outline, resize/default Performance tab and Hierarchy Down-key
  selection under Metal validation with no validation errors. The original workspace is restored
  before reopening the validation-off build for the user (so MetalFX remains usable).
- New 32-frame scene-only images match the original parent byte-for-byte for Sponza, Helmet,
  Milk Truck, MaterialLab and TemporalLab. San Miguel's first comparison differs in 807 pixels,
  max 44/255. Independent paired repeats show baseline-vs-baseline 1024 pixels/max 6, current-
  vs-current 542/max 7, cross-build 474–1057/max 6–7. This does not explain or close the initial
  outlier or historical image drift; all original/repeat outputs and comparisons are retained.

The user's next manual review and previously unverified held-input/docking/panning gestures remain
part of UX1 acceptance. This revision does not pass the complete UX1 gate or start M7.

### Selection toolbar stability follow-up

Completed: manual review found a selection-dependent Viewport resize caused by inserting
Frame selected / Selection outline only for Objects. After updating the design, the implementation
keeps both controls present and disables them for other subjects. Missing bounds are explained
on hover without inserting another row; allocation-failure feedback is independent of selection.

Build, full CPU suite, formatting and all six direct policy checks pass. Native maximized-window
review switched Camera → Object → Light → Rendering → Object → Camera: image edges remained
unchanged and Performance reported 2196 × 1144 output throughout. Disabled Frame selected had
no effect and displayed its explanation; outline off/on, explicit framing and Reset camera
worked with unchanged image extents. This follow-up changes only App UI composition, with no
renderer/shader changes; the earlier GPU evidence remains scoped to that unchanged path.
Evidence: `toolbar-stability/` in the external UX1 evidence bundle, screenshots 01–08
and build/test/policy results. Latest build remains open maximized for the user. Overall UX1
acceptance and its earlier evidence limits remain open; M7 is inactive.

### Foreground occluder outline follow-up

Completed: manual review exposed a false selection border at foreground rods. After the design
update, selected-only R8/D32 coverage was separated from full-scene D32 visibility. The composite
uses the object's own geometry/alpha silhouette and rejects hidden source samples plus expanded
pixels on closer foreground surfaces. Occlusion cuts no longer generate internal lines. Both
depth outputs explicitly store their contents before sampling; the initial development run
caught an omitted Store declaration, corrected before the passing runs below.

Validation: build and full CPU suite pass, as do format and all six policy checks. Metal
validation passes six outline cases / 113 assertions and the broader 171-case applicable GPU
set / 252235 assertions (`[gpu]~[vendor],[gpu][pack]`). Focused probes cover thin opaque rods
through the interior and natural silhouette, MASK foreground cutouts, 1×/2× thickness, full
occlusion and unchanged ordinary display/HDR/temporal history. The pre-existing MetalFX
validation exception remains outside this applicable set.

Maximized native review with Metal validation enabled reproduced the Temporal Lab emissive
sign behind five poles. At time zero, camera (0, 2.5, 2), yaw/pitch zero and 30-degree FOV,
only its external silhouette highlights; pole surfaces and interior cuts remain unhighlighted.
Playback also preserved this behavior as poles moved across the edges. Viewport output was
2106 × 1014 pixels; selection did not change its extent. The paused close-up remains open for
user review. No Metal validation errors were observed. Evidence is under `outline-occlusion/`
in the external UX1 bundle, including screenshots, logs and source/binary hashes.

The cue now has three measured passes and one additional selected draw plus a full-resolution
D32 transient versus the previous implementation; no formal performance comparison is claimed.
Ordinary capture and rendering paths remain unchanged. Overall UX1 acceptance remains open
for its earlier recorded limits, and M7 is inactive.

### Global UI zoom follow-up

Completed: after updating the design, the editor gained main-bar minus/current-percent/plus
buttons (click the percentage to reset), Layout > UI Scale presets and Cmd+- / Cmd++ or Cmd+= /
Cmd+0 shortcuts. Presets span 75–150%, default 100%. Physical Cmd maps to ImGui's logical Ctrl
under its macOS behavior; native review caught and corrected that mapping before acceptance.
Text entry, active widgets, popups and camera look guard the shortcuts.

Fonts, style padding and responsive panel measurements share the scale, regenerated from an
unscaled base before NewFrame to avoid cumulative rounding. Graph cards remeasure/reflow once
on change without losing selected identity, frozen frame or independent canvas navigation.
Optional schema-2 UiScalePercent restores without redocking; missing or malformed/out-of-range
values fall back to 100%. Layout reset keeps the preference. UI scaling may change the image's
available extent through the existing safe resize path but never changes the camera or
render-scale setting.

Build, full CPU suite, formatting and all six policy checks pass. Five added CPU cases cover
scale bounds, stepping, malformed input, legacy decisions and persistence/round-trip contracts.
Maximized native review with Metal validation enabled covered the scale range, top-bar buttons,
Cmd shortcut variants/reset, search-field and open-menu suppression, and the detached graph at
75% and 150% retaining selected scene pass and frozen frame 13724. Repeated scaling back restored
the compact graph arrangement. No Metal validation errors were observed.

Restart restored 75% plus the same dock node topology. Reset Default Layout retained 75%, then
the user's pre-reset docking (Performance and Console beside each other) was restored for final
handoff. At that layout the Viewport output changed from 2106 × 1144 at 100% to 2130 × 1258 at
75%; the extra area came from compact UI chrome. Font/control behavior was checked on the same
maximized Retina display, not a second monitor. This App/UI-only change does not rerun rendering
image oracles or claim a renderer performance improvement.

Evidence: `ui-zoom/` in the external UX1 bundle, including screenshots 01–13, native logs, saved
workspace samples and test/check results. The final app remains open at 75% for user review.
Earlier UX1-wide acceptance limits remain open and M7 remains inactive.

### Editor typography follow-up — completed

Inter Regular at 16 logical points replaces the embedded monospace; digits share a 9 pt advance.
Pinned setup fresh-download/repeat and App font/license staging pass; missing-font startup falls back with a recovery warning.
Build, full CPU suite, format and all six policies pass. Maximized Retina 75/100/150% and detached Graph were inspected under Metal validation without errors; selected scene/frozen frame 8871 survive scaling.
Evidence: `typography/` screenshots/logs in the external UX1 bundle. Original docks and 75% are restored for handoff; overall UX1 acceptance remains open and M7 inactive.
