# UX4 parent gesture validation

**Status**: In progress

This companion preserves the parent observations for the [UX4 validation](ux4-validation.md).
It covers the frozen gesture snapshot through `parent-capture-reveal` (image 0078):
73 recorded observations (68 passed, 5 unverified), including two original System
Settings reads. All 81 referenced parent and supporting images were inspected. Parent results establish
comparison behavior; they do not verify the candidate or close the full
[completion gate](../../roadmap/editor-experience.md#completion-gate).

Evidence lives outside the repository at `../Luminex-evidence/ux4/`. In the tables below,
screenshot paths are relative to `task-12/cua/` within that root. Record IDs retain the
`gestures.json` labels; later rows in that live file are outside this snapshot. No failed or
unverified attempt is replaced by its successful retry.

The frozen parent is `28ab04a`. Its release executable SHA-256 is
`50ff38651a8af579d1e58b2acc4f82ca06414dd3eed379a0a739f58f31020906`.
The external QA wrapper contains that byte-identical executable. The parent uses its original
Dark content palette and Inter type; observed backing scale is 2×. Main-window client sizes are
1280 × 720 pt windowed and 1674 × 1052 pt maximized. These observations use native window controls.

The recorded Compact index-cost failure remains failed under the owner's continuation exception;
see [the cost record](ux4-validation.md#task-12-cost-gate-failure). This companion records no cost
retest, candidate result, 1× typography result or inherited UX1 pass.

## Original settings

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| settings-original-motion | Read Accessibility > Motion before changing settings | Record original Reduce Motion | Reduce Motion is off | passed | 0001-settings-original-motion.png |
| settings-original-appearance | Read Appearance before changing settings | Record original System Appearance | Auto selected; system currently displays Dark | passed | 0002-settings-original-appearance.png |

## Windowed startup, selection and focus

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-windowed-restored | Launch the schema-4 restored workspace with --windowed. | Restore 1280 × 720 pt content and stored docks. | Client is 2560 × 1440 px at 2×; Hierarchy 219 pt, Inspector 359 pt, Performance/Console below; 100% scale. Hierarchy scroll exposes content. | passed | 0003-parent-dark-windowed-restored.png |
| parent-dark-windowed-reset | Window > Reset Default Layout. | Restore default docks, preserve 100% scale and close detached windows. | Inspector and Console become selected; Hierarchy, Viewport, Rendering and Performance summary remain reachable; scale stays 100%. | passed | 0004-parent-dark-windowed-reset.png |
| parent-dark-windowed-sponza-select | Search Crytek, select the source row and press F. | Show source identity and primitive grouping; locate selected geometry. | Search shows 2/24; Inspector names Crytek Sponza controlling 25 material primitives. F changes the lion close-up to a wall; the enclosing-source contour is indistinguishable. | unverified | 0005-parent-dark-windowed-sponza-select.png |
| parent-dark-windowed-sponza-edit | Enter 0.1 in Position X and press Return. | Update the transform and show document dirty state. | X reads 0.100, wall shifts, and the title/root display Sponza*. | passed | 0006-parent-dark-windowed-sponza-edit.png |
| parent-dark-windowed-sponza-reset | Click the subject Reset. | Restore the authored transform and clear dirty state. | X returns to 0.000, title/root lose the star, and other values remain unchanged. | passed | 0007-parent-dark-windowed-sponza-reset.png |
| parent-dark-windowed-text | Type F, Home and C in Hierarchy search, then Cmd+A/C, Right and Cmd+V. | Edit/copy/paste text while suppressing camera, framing and capture commands. | Search reads cCrytekfcCrytekf with 0/24 matches; both panels explain the retained filtered selection. The wall view remains; no capture notice appears. | passed | 0008-parent-dark-windowed-text.png |
| parent-dark-windowed-scale90 | Press Cmd+- outside editing. | Decrease UI scale once. | 90% appears and panels rescale. | passed | 0009-parent-dark-windowed-scale90.png |
| parent-dark-windowed-scale100 | Press Cmd+0 outside editing. | Restore 100% UI scale. | 100% appears and original metrics return. | passed | 0010-parent-dark-windowed-scale100.png |

## Maximized reconstruction and resolution

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-maximized | Use native window Zoom. | Fill usable display bounds with reachable core panels. | Client is 1674 × 1052 pt; swapchain log gives 3348 × 2104 px. Labels remain reachable; Inspector stays 359 pt wide. | passed | 0011-parent-dark-maximized.png |
| parent-dark-max-metalfx | Choose MetalFX Temporal. | Show requested/effective vendor mode and render/output extents. | MetalFX Temporal reads 100%; Render/Output both 2136 × 1528 px. Vendor packing/history logs appear; this supported M3 Max run has no fallback. | passed | 0013-parent-dark-max-metalfx.png |
| parent-dark-max-inputs-off | Turn Temporal inputs off. | Show effective Off, retain the requested algorithm and use full output extent. | Off reads 100%, requested MetalFX Temporal remains, and extent is 2136 × 1528 px. The disabled algorithm tooltip explains full-resolution output. | passed | 0014-parent-dark-max-inputs-off.png |
| parent-dark-max-resolution | Set manual scale toward 0.75, then enable Dynamic resolution. | Show current controller state, requested/effective scale and extents. | The retained screenshot shows Controller Active, manual/effective scale 1.00, and Render/Output 2136 × 1528 px; the retired-input tooltip and scale-change log are visible. Earlier 0.94/0.89 and 1901 × 1360 readings are unverified exact values; see evidence limits. | passed | 0016-parent-dark-max-resolution.png |

## Performance snapshots

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-performance-frozen | Freeze, select the highest Average row and open Metric details. | Identify the highest pass in one frozen snapshot and explain values. | Frozen frame 39602: scene Average 2.084 ms, Latest 0.939 ms; 60/60 samples at 4 Hz. Timed sum 6.532 ms excludes presentation, driver and untimed work. Controller input separately names frame 34445. | passed | 0017-parent-dark-performance-frozen.png |
| parent-dark-performance-expand | Expand the selection stage while frozen. | Show children in the same coherent frame. | Frame 39602 retains scene 2.084/0.939 ms. Average/Latest children: outline 1.295/0.863, visibility 0.195/0.155 and coverage 0.052/0.028 ms. | passed | 0018-parent-dark-performance-expand.png |
| parent-dark-performance-clear | More > Clear history while frozen. | Keep an empty/N/A snapshot until Resume. | Frozen — empty, 0/60 samples, no rows and GPU/controller N/A appear; unavailable data is not shown as zero. | passed | 0019-parent-dark-performance-clear.png |
| parent-dark-performance-resume | Resume metrics. | Replace the empty snapshot with fresh samples. | The frozen label clears; 60/60 fresh samples, Average/Latest rows and a 120-sample interval plot return. Frame 45574 was recorded live; retained image shows 47598. | passed | 0020-parent-dark-performance-resume.png |

## Graph, frozen dump and recovery

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-graph-expand-zoom | Select scene, double-click the light group and wheel down. | Expand the group and zoom; explain selection hidden by expansion. | Four light passes separate and zoom decreases. Header explains that the selected item is no longer visible; group-selection retention is not claimed. | passed | 0021-parent-dark-graph-expand-zoom.png |
| parent-dark-graph-retention-after | Leave the graph live after selection, expansion and 1:1 for at least ten seconds. | Retain selection, group state and navigation across Native TAA history alternation. | After 97016 ms, exposure-seed selection, canvas bounds and expanded light passes remain. Live timing changes; exact recorded/captured values differ as detailed below. Pan is unverified. | passed | 0024-parent-dark-graph-retention-after.png |
| parent-dark-graph-frozen | Freeze the graph, then More > Dump frame. | Latch exact resources/matched timings and export the frozen frame. | Frozen frame 90834: exposure-seed GPU 0.011 ms matches card/detail; r9 version 0 write, StorageWrite→StorageWrite barrier. Dump is invoked; its notice is outside the graph window. | passed | 0025-parent-dark-graph-frozen.png |
| parent-dark-dump-console | Close Graph and read its dump result in Console. | Show the output path and frozen frame identifier. | Console reports frame 90834 at graph-dump-frame-90834.txt. The six-second notice expired before main-window focus; Copy path/Reveal remain unverified for this attempt. | unverified | 0026-parent-dark-dump-console.png |
| parent-dark-dump-reveal | Click Reveal on the dump notice | Finder selects the exported frozen frame | Finder selects graph-dump-frame-90834.txt and previews render-graph frame 90834 | passed | 0028-parent-dark-dump-reveal.png |
| parent-dark-dump-copy | Click Copy path, then paste into Console search. | Copy the exported frame path. | Search contains <parent-checkout>/build/macosx/arm64/release/graph-dump-frame-90834.txt; the screenshot retains the full local path. | passed | 0029-parent-dark-dump-copy.png |
| parent-dark-graph-resume-fit | Resume, then Fit graph. | Return to live matched timing and fit all cards. | Frozen label clears and full graph cards/culled passes fit. The live record gives exposure seed 0.091 ms; retained image gives 0.041 ms. | passed | 0030-parent-dark-graph-resume-fit.png |
| parent-dark-graph-max | Use native window zoom | Graph fills usable bounds and keeps selection/details readable | Graph widens to 3348 pixels; selected exposure seed details and expanded light passes remain visible | passed | 0031-parent-dark-graph-max.png |

## Scene, light and camera edits

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-light-edit | Edit Local Light 1 intensity to 46 | Intensity changes and document becomes dirty | Intensity reads 46.000; Sponza* appears in title and root; relative intensity tooltip remains | passed | 0032-parent-dark-light-edit.png |
| parent-dark-light-reset | Reset Local Light 1 | Authored intensity returns and dirty clears | Intensity returns to 45.000; title and root no longer have a star; reset tooltip names authored scope | passed | 0033-parent-dark-light-reset.png |
| parent-dark-camera-edit | Edit camera Fov Y to 55 degrees | Lens changes without saving the editor camera | Fov Y reads 55.0 and wall framing widens; title stays clean | passed | 0034-parent-dark-camera-edit.png |
| parent-dark-camera-reset | Click camera Reset | Authored pose, lens and fly speed return | Lion view returns; Fov Y 45.0, Near 0.050, fly speed 3.00; camera reset tooltip remains | passed | 0035-parent-dark-camera-reset.png |
| parent-dark-san-miguel-source | File > Open Scene > San Miguel; search Miguel and select the source. | Keep the imported source selectable with transform fields. | SanMiguel controls 281 primitives through one source node; XYZ position, rotation and scale remain visible. | passed | 0036-parent-dark-san-miguel-source.png |
| parent-dark-san-miguel-edit | Set source Position X to 0.1 with text entry | Edit updates the model and dirty indicator | Position X is 0.100; title and hierarchy root have an asterisk; geometry shifts | passed | 0037-parent-dark-san-miguel-edit.png |
| parent-dark-san-miguel-frame | Reset source; press F outside editing | Reset clears dirty state; Frame Selected frames reliable bounds with visible contour | Position X returned to zero; dirty cleared; camera moved to exterior bounds. A reliable contour is not visible for the whole imported source | unverified | 0038-parent-dark-san-miguel-frame.png |
| parent-dark-material-load | Open MaterialLab from File > Open Scene | The material grid and colored axis objects load | Spheres, helmet, and red/green/blue axis geometry render. Console reports the parent helmet mip fallback | passed | 0039-parent-dark-material-load.png |
| parent-dark-material-axis-frame | Select MaterialLab red X shaft; press F | The selected axis frames and keeps its source/status and contour | Red X shaft frames with an orange contour; generated-by/not-saved status and XYZ fields remain visible | passed | 0040-parent-dark-material-axis-frame.png |
| parent-dark-material-axis-edit | Edit generated axis Position Y from -2 to -1.9 | The edit is session-only and leaves the document clean | Y is -1.900; shaft moves and contour follows; generated-by/not-saved status remains; title stays clean | passed | 0041-parent-dark-material-axis-edit.png |

## TemporalLab playback and diagnostics

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-temporal-stopped | Open TemporalLab | Scene opens stopped with a clean preview | TemporalLab title clean; time 0.000; generated objects and Milk Truck hierarchy visible | passed | 0042-parent-dark-temporal-stopped.png |
| parent-dark-temporal-play | Click Play in TemporalLab | Time advances and animated poses change without dirtying the document | Time advanced; yellow sphere and red cube moved; clean title; Pause tooltip describes holding time | passed | 0043-parent-dark-temporal-play.png |
| parent-dark-temporal-pause | Click Pause | Time and poses hold while Step becomes available | Time holds at 5.833 s; Play tooltip replaces Pause; Step enabled | passed | 0044-parent-dark-temporal-pause.png |
| parent-dark-temporal-step | Click Step while paused | Advance 1/60 second and stay paused | Readout changed 5.833 to 5.850 s; Play remains available; clean title | passed | 0045-parent-dark-temporal-step.png |
| parent-dark-temporal-stop | Click Stop after Play/Pause/Step | Restore starting preview, time zero and clean document | Time is 0.000; sphere/cube/emissive preview returns to starting positions and appearance; title remains clean | passed | 0046-parent-dark-temporal-stop.png |
| parent-dark-motion | View > Debug View > Temporal > Motion vectors | Diagnostic output and a readable legend explain units and invalid pixels | Gray zero motion, magenta invalid cube; legend explains red/green axes, UV delta and +/-0.0625 clipping | passed | 0047-parent-dark-motion.png |
| parent-dark-reprojection | Choose Reprojection error in the legend | Legend explains error magnitude and unavailable comparison | Black near-zero output with blue invalid cube; legend defines 4x luminance error, white at 0.25 and blue no comparison | passed | 0048-parent-dark-reprojection.png |
| parent-dark-reprojected | Choose Reprojected history | Color history and invalid-history legend are readable | Color image renders with blue invalid history; legend describes exposure correction, neighborhood clipping and PBR Neutral/sRGB | passed | 0049-parent-dark-reprojected.png |
| parent-dark-rejection | Choose Rejection mask from the viewport legend. | Readable legend distinguishes accepted, outside-history, invalid, disocclusion, reactive and clipping pixels. | Black accepted regions, green neighborhood clipping and a magenta invalid cube match the six-color legend. | passed | 0050-parent-dark-rejection.png |
| parent-dark-blend-weight | Select Blend weight from the legend. | Grayscale alpha range and the pre-reweighting definition remain readable. | The invalid cube is white; stable pixels are dark. The legend identifies black 0 and white 1 before inverse-luminance reweighting. | passed | 0051-parent-dark-blend-weight.png |
| parent-dark-history-age | Select History age from the legend. | The age range and reset semantics remain readable. | Stable pixels are white; the invalid cube is near black. The legend gives floor(age)/16, white at 16 accumulated frames and rejected pixels restarting at 1. | passed | 0052-parent-dark-history-age.png |
| parent-dark-final-close | Close the History age legend. | Return to Final and remove the diagnostic legend. | The color TemporalLab image returns and the legend disappears. | passed | 0053-parent-dark-final-close.png |

## Lighting and HZB diagnostics

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-light-count-unavailable | Choose Lighting > Count in TemporalLab. | Explain a missing enabled-light population without misleading diagnostic pixels. | Count legend shows No enabled local lights. Showing Final. The viewport stays in Final. | passed | 0054-parent-dark-light-count-unavailable.png |
| parent-dark-light-count | Open LightLab and choose Lighting > Count. | Display enabled-light froxel counts with a readable range legend. | Green 5–16 and yellow 17–64 regions appear; the legend lists all count ranges. | passed | 0055-parent-dark-light-count.png |
| parent-dark-light-overflow | Choose Overflow in the lighting legend. | Readable legend explains magenta truncation over a dimmed Final image. | Final appears at 25% brightness without magenta; the legend states unmarked froxels retained all candidates. | passed | 0056-parent-dark-light-overflow.png |
| parent-dark-light-missed | Choose Missed in the lighting legend. | Distinguish list errors, expected truncation loss and no missing contribution. | The image is black and the readable legend defines red list errors, yellow expected loss and black no missing contribution. | passed | 0057-parent-dark-light-missed.png |
| parent-dark-hzb-enable | Choose GPU classifier, then enable Occlusion in Rendering. | Show previous-frame history state and explain the coverage/camera limitation. | Occlusion history is Valid, 0/15 occluded/tested, with readable previous-frame depth tooltip and View > Debug View location. | passed | 0058-parent-dark-hzb-enable.png |
| parent-dark-hzb-zero | Choose Occlusion > HZB level 0. | Show a readable farthest-reversed-depth legend and a mip stepper. | HZB level 0 is selected; the grayscale image is very dark, with black-uncovered explanation and minus/plus controls. | passed | 0059-parent-dark-hzb-zero.png |
| parent-dark-hzb-increment | Click the HZB plus stepper. | The mip index and legend title increment together. | The input and legend both show HZB level 1. | passed | 0060-parent-dark-hzb-increment.png |
| parent-dark-hzb-decrement | Click the HZB minus stepper. | The mip index and title return to zero. | The input and title show HZB level 0; the tooltip explains larger mip regions. | passed | 0061-parent-dark-hzb-decrement.png |
| parent-dark-hzb-close | Close the HZB legend. | The viewport returns to Final. | The colored LightLab scene returns and the HZB legend disappears. | passed | 0062-parent-dark-hzb-close.png |

## Capture and measurement

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-capture-disabled | Click the disabled Debug > Capture Next GPU Frame item. | The disabled item explains how to enable GPU capture. | The tooltip says to relaunch with MTL_CAPTURE_ENABLED=1 xmake run App; no capture starts. | passed | 0063-parent-dark-capture-disabled.png |
| parent-dark-performance-sort | Click the Average cost column. | Rows reverse cost order without losing the selected pass. | The sort arrow points up; visibility is first and scene is last, while scene details remain selected. | passed | 0064-parent-dark-performance-sort.png |
| parent-dark-measure-start | Edit warmup to 0 and measured frames to 2000, then Start measurement. | The run starts, fields and Start disable, and Stop remains available. | Measuring progress is visible; both fields and Start are disabled, with an explanatory tooltip. | passed | 0065-parent-dark-measure-start.png |
| parent-dark-measure-close-late | Close Performance during the 2000-frame run. | The main transport displays ongoing measurement after close. | The main transport is already back to Stopped when observed; the run was too short to observe ongoing progress after close. | unverified | 0066-parent-dark-measure-close-late.png |
| parent-dark-measure-results | Reopen Performance after the run. | Completed results remain available. | Measure retains Complete 2000/2000, mean encode 0.745 ms, timed GPU sum 3.925 ms and Export measurement JSON. | passed | 0067-parent-dark-measure-results.png |
| parent-dark-measure-export | Click Export measurement JSON. | A successful export names its destination. | An Exported line names luminex-interactive-measurement-1790788822482.json. | passed | 0068-parent-dark-measure-export.png |
| parent-dark-measure-closed-active | Start a 10000-frame measurement and close Performance immediately. | Measurement continues in the main transport and disables edits. | The main toolbar shows Measuring progress with only Stop enabled; Hierarchy and Rendering edits are dimmed. | passed | 0069-parent-dark-measure-closed-active.png |
| parent-dark-measure-stop | Click Stop in the main transport while Performance is closed. | The measurement stops without reopening Performance and edits re-enable. | The transport returns to Stopped 0.000 s, edits regain full contrast and Performance remains closed. | passed | 0070-parent-dark-measure-stop.png |

## Windowed docking and persistence

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-dark-splitter-drag-unverified | Drag the Hierarchy splitter right, first from x440 to550, then from x439 to660. | Hierarchy widens and the viewport reflows. | Both drag attempts leave the dock boundary at x440; the native input did not expose an observable resize. | unverified | 0071-parent-dark-splitter-drag-unverified.png |
| parent-dark-windowed-return | Use the native zoom secondary action to restore the main window. | The editor returns to 1280 × 720 client points and reflows its panels. | The log reports swapchain 2560×1440 at 2×; Rendering remains readable and the viewport target becomes 1348×864. | passed | 0072-parent-dark-windowed-return.png |
| parent-dark-close-right-dock | Click the close control on the Inspector/Rendering dock. | The right dock closes and the viewport expands. | Both right-hand tabs disappear; the viewport expands to 2070×864. | passed | 0073-parent-dark-close-right-dock.png |
| parent-dark-hidden-panel-menu | Open Window after closing the right dock. | Inspector and Rendering menu items reflect their hidden state. | Both Inspector and Rendering are unchecked; the other visible dock panels remain checked. | passed | 0074-parent-dark-hidden-panel-menu.png |
| parent-dark-reopen-rendering | Choose Window > Rendering. | Rendering returns to its previous dock. | Rendering returns to the right dock with its collapsed/open topic state retained; Inspector remains hidden. | passed | 0075-parent-dark-reopen-rendering.png |
| parent-dark-panel-persistence | Quit normally and relaunch with GPU capture enabled. | Persist hidden Inspector and visible Rendering state. | Rendering remains in the right dock and Inspector remains hidden; Sponza opens at 1280 × 720 pt. | passed | 0076-parent-dark-panel-persistence.png |

## Enabled GPU capture

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| parent-capture-copy | Choose Debug > Capture Next GPU Frame, press C outside text entry, then Copy path on the second success notice and paste into Console search. | Both requests write a trace; clipboard text matches its exact path. | Console retains both successful start/write pairs. Search contains the full local path ending in task-12/cua/parent-capture.gputrace; the C success notice is retained separately. | passed | 0077-parent-capture-copy.png; parent-capture-shortcut-notice.png |
| parent-capture-reveal | Click Reveal in Finder on a fresh successful capture notice. | Finder selects the written trace at its output path. | Finder selects parent-capture.gputrace in task-12/cua; preview shows GPU trace, 322.9 MB and full path. | passed | 0078-parent-capture-reveal.png; parent-capture-reveal-notice.png |

## Supporting images and evidence limits

These images support state inspection. They do not add an unrecorded gesture pass.

| Image | Retained observation | Limit |
|---|---|---|
| 0012-parent-dark-max-raw.png | Raw at 100%; Render/Output 2136 × 1528 px, temporal inputs and jitter enabled. | No separate action/result row records the Raw switch. |
| 0015-parent-dark-max-scale075.png | Native TAA 74%; manual/effective scale 0.74, Render 1581 × 1131 px / Output 2136 × 1528 px; controller Inactive. | The filename/action intent 0.75 does not establish an exact 0.75 setting. |
| 0016-parent-dark-max-resolution.png | Controller Active at 1.00; Render/Output 2136 × 1528 px. | The original gesture record says requested 0.94, effective 0.89 and Render 1901 × 1360 “at capture.” Those transient values were observed earlier but are not retained; the exact numeric claim is unverified. |
| 0022-parent-dark-graph-retention-before.png; 0023-parent-dark-graph-one-before.png | Fit Selected shows exposure seed; 1:1 provides the retention sequence's starting canvas. | Supporting images do not independently verify pan. |
| 0024-parent-dark-graph-retention-after.png | Exposure seed, expanded light passes and canvas bounds persist; live timing updates. | The original record gives 0.017→0.011 ms; the retained before/after images give 0.006→0.021 ms. No exact matched before/after timing claim passes. |
| 0027-parent-dark-dump-notice.png | Console shows the dump result. | No retained notice in this attempt. |
| 0028-parent-dark-dump-notice.png | Frozen frame 90834 success notice exposes Copy path and Reveal. | Separate recorded reveal/copy gestures establish their results; this supporting image alone does not. |
| 0020-parent-dark-performance-resume.png; 0030-parent-dark-graph-resume-fit.png | Fresh live publications replace frozen data. | Live values advanced between observation and image capture: Performance frame 45574 versus captured 47598; graph 0.091 versus captured 0.041 ms. Numeric capture identity is not claimed. |

Every saved image through 0076 contains JPEG bytes despite its `.png` suffix; none is a renderer
PNG capture or a lossless pixel-comparison artifact. Dimensions include the native title bar.
Settings images are 1446 × 1424 px; windowed main images are 2560 × 1504 px; maximized main/graph
images are 3348 × 2168 px; detached Performance images are 2200 × 1584 px; ordinary detached
Graph images are 2560 × 1664 px; the Finder reveal image is 1840 × 928 px. A main screenshot's
height includes 64 px above the client region.

## Coverage still open at this snapshot

- Maximized restored and Reset Default Layout states are not both recorded; zooming an already
  reset window does not establish both states. Windowed reset and relaunch visibility persistence
  have direct records.
- Sponza/San Miguel duplicate-name disambiguation and a reliable viewport cue for the imported
  selection remain open. Their whole-source Frame Selected attempts stay unverified.
- Raw/Native TAA switches need explicit action/result records. Dynamic-resolution numeric
  settling, disabled diagnostic behavior and capability-fallback handling need direct evidence.
- Graph pan, its ten-second retention and a genuine topology change remain open. Frozen resource,
  timing and same-frame dump records pass only the scoped recorded sequence.
- MaterialLab's red shaft establishes coordinate-axis selection, not the roughness/metallic
  material-grid axes and purpose. The latter still needs a direct record.
- Rendering-topic scoped reset, fly-camera gestures, docking resize/navigation and recoverable
  capture failures remain open. Enabled menu/C requests, Copy path and Reveal now have records.
  Disabled capture has the menu explanation only;
  its C shortcut route is not recorded in this snapshot.
- Window menu coverage for all panels, detached-window focus/reopen and bounds persistence,
  Cmd+plus and the scale-menu route remain incomplete. Hidden Inspector/visible Rendering
  persistence is the narrower observed pass.
- The action coverage is split between windowed and maximized parent sessions. It does not
  establish a repeated full workflow in both geometries or any candidate Dark/Light workflow.
- Gallery 13/16/20 px typography at 1×/2×, default/Compact usability and the conditional body-size
  decision remain outside this parent record.

The roadmap owns the required tasks; the UX4 plan requires those tasks in candidate Dark and
Light, maximized and at 1280 × 720 pt. Resting screenshots at the four sizes/themes cannot substitute
for observable actions. Repeat actions as grouped workflows, retaining their state/result records.
