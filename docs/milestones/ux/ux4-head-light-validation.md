# UX4 head Light gesture validation

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

This companion retains actual Light observations for [UX4 validation](ux4-validation.md).
The live snapshot contains 151 rows through 0156. The audited snapshot adds the performed
0152 action whose live JSON row was omitted, preserving that omission explicitly. This page
retains twenty Light actions (eighteen passed, two unverified) and two passed settings reads.
All twenty-two associated screenshots were inspected; raw live JSON remains unchanged.

Evidence is outside the repository at `../Luminex-evidence/ux4/`; screenshot paths below are
relative to `task-12/cua/` within that root. The QA wrapper contains the clean radius-0 App,
SHA-256 `3749a58a714e4b574be10212e904c7ad82f82f9a99df512fafcd4d50596f0900`.
Original settings, INI backups and earlier frozen snapshots remain preserved externally.

All images below contain JPEG data named `.png`, with 2× backing scale and 100% UI scale.
Windowed images 0135 through 0142 are 2560 × 1504 px, including the 64 px native title region;
the client is 1280 × 720 points. Maximized main-window images 0143 through 0153 are 3348 × 2168 px including that
title region: total window bounds are 1674 × 1084 points and client size is 1674 × 1052 points.
The recorded zoom expands the existing workspace; launch/relaunch restoration is not verified
by that resize alone. These rows contain no explicit Light density selection.

## Windowed appearance and local-light recovery

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-light-windowed-appearance | Choose View > Appearance > Light. | Editor and native window use Light with settings retained. | VisibilityLab remains visible; native title, menu row, Hierarchy, Rendering and Console use Light, with 100% retained. | passed | 0135-head-light-windowed-appearance.png |
| head-light-windowed-light-edit | Edit Local Light 1 intensity from 45 to 46. | Field changes and document dirty state appears. | Intensity is 46.000, title is Sponza* and root has a blue dirty dot; position, color and range remain visible. | passed | 0136-head-light-windowed-light-edit.png |
| head-light-windowed-light-reset | Click Local Light 1 Reset. | Restore authored intensity and clean document. | Intensity returns to 45.000, range is 6.000 and XYZ are -12.000/2.500/-3.500; title is clean and dirty dot disappears. | passed | 0137-head-light-windowed-light-reset.png |

## Windowed camera and source recovery

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-light-windowed-camera-edit | Edit camera FOV from 45 to 55. | Viewport changes without making the document dirty. | FOV reads 55, lion framing widens and title stays clean. | passed | 0138-head-light-windowed-camera-edit.png |
| head-light-windowed-camera-reset | Click Editor Camera Reset. | Restore authored lens and pose. | FOV returns to 45.0, near is 0.050, XYZ are 8.500/1.600/0.600 and fly speed is 3.00; original lion framing and clean title return. | passed | 0139-head-light-windowed-camera-reset.png |
| head-light-windowed-sponza-search-fit | Search Crytek, select its source and press F. | Inspector identifies source and camera frames selection. | Search shows 2/24; full source identity and 25-primitive scope are readable. F changes to a wall view. The yellow bounds and blue rectangle are explicitly HZB diagnostics; a whole-source selection outline cannot be assessed. | unverified | 0140-head-light-windowed-sponza-search-fit.png |
| head-light-windowed-sponza-transform | Edit source Position X from 0 to 0.1. | Transform and dirty state update. | X reads 0.100 and the wall shifts; title gains * and the root gains a blue dirty dot. | passed | 0141-head-light-windowed-sponza-transform.png |
| head-light-windowed-sponza-reset | Click source Reset, then Home. | Authored transform and camera return with clean title. | XYZ return to 0.000, scale stays 1.000, original lion framing and clean title return. Source stays selectable; Inspector reports Culled: frustum and Outside camera frustum. | passed | 0142-head-light-windowed-sponza-reset.png |

## Maximized workspace and default layout

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-light-maximized-restored | Zoom the native main window to usable bounds. | Existing workspace expands with readable controls. | Native Light title, Hierarchy, Inspector, Console and transport remain visible at 100%; source XYZ and expanded Diagnostics are readable. | passed | 0143-head-light-maximized-restored.png |
| head-light-maximized-reset | Choose Window > Reset Default Layout. | Default docks rebuild and preserve scale/theme. | Hierarchy widens; Inspector and Console are selected, Rendering and Performance tabs remain available; Light and 100% remain. | passed | 0144-head-light-maximized-reset.png |

## Maximized reconstruction and disabled views

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-light-max-native | Open Rendering > Reconstruction after Reset Default Layout. | Native TAA, inputs/jitter on and matching readable extents. | Native TAA 100%, inputs and jitter checked, Render/Output both 2050 × 1344 px. HZB diagnostic bounds remain enabled from the earlier session. | passed | 0145-head-light-max-native.png |
| head-light-max-raw | Choose Raw from Algorithm. | Raw becomes effective with current extents. | Raw 100%, Render/Output both 2050 × 1344 px; temporal inputs and jitter remain on. | passed | 0146-head-light-max-raw.png |
| head-light-max-metalfx | Choose MetalFX Temporal. | Requested/effective MetalFX agree on supported hardware. | Selector and effective label show MetalFX Temporal 100%, Render/Output both 2050 × 1344 px; scene remains visible. | passed | 0147-head-light-max-metalfx.png |
| head-light-max-inputs-off | Disable temporal inputs and expand Diagnostics. | Effective Off retains request; unavailable history readings use N/A. | Off 100%, equal 2050 × 1344 px extents and Requested: MetalFX Temporal. History, Warmup, age, jitter index and vendor generation are N/A; capability is MetalFX Temporal with range 0.33 to 1.00. | passed | 0148-head-light-max-inputs-off.png |
| head-light-max-debug-disabled | Open View > Debug View > Temporal and click disabled Motion vectors with inputs off. | Disabled action keeps Final and explains recovery. | All six temporal items are gray, Final stays checked and Motion vectors tooltip says to enable temporal inputs in Rendering > Reconstruction. | passed | 0149-head-light-max-debug-disabled.png |

## Maximized resolution and secondary-window input

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-light-max-reconstruction-reset | Click Reconstruction Reset and collapse Diagnostics. | Restore Native defaults without changing camera or other topics. | Native TAA 100%, inputs/jitter on and equal 2050 × 1344 px extents; lion pose and HZB bounds remain. | passed | 0150-head-light-max-reconstruction-reset.png |
| head-light-max-manual-scale | Enter 0.75 and expand Resolution Diagnostics. | Smaller render extent, effective 0.75 and Controller Inactive. | Native TAA 75%, Render 1538 × 1008 / Output 2050 × 1344 px; effective 0.75, Inactive. Controller sample 39033 is separate from HZB source 569382. Original live JSON's 567315 is corrected in the audit. | passed | 0151-head-light-max-manual-scale.png |
| head-light-max-dynamic-resolution-audited | Enable Dynamic resolution at 16 ms after manual 0.75; expand Diagnostics. | Controller Active with coherent sample, extents and updated scale. | Active 1.00, budget 16.00 ms, Render/Output 2050 × 1344 px, sample frame 584896. Root observed transient 0.85, which is not retained. Root confirms the performed action; its row was omitted from live JSON and is added only to the audited snapshot. | passed | 0152-head-light-max-dynamic-active.png |
| head-light-max-resolution-reset | Click Resolution Reset and collapse Diagnostics. | Restore 1.00, DR off and Inactive without changing camera/other topics. | Native TAA 100%, equal 2050 × 1344 px; DR unchecked, Inactive; lion pose and HZB bounds remain. | passed | 0153-head-light-max-resolution-reset.png |
| head-light-max-performance-input-unverified | Sort Average descending, read definitions, then click Freeze before/after binding refresh. | Highest pass and timing scope readable; Freeze latches snapshot and exposes Resume. | HZB Average 1.548/Latest 0.143 ms leads; Average sum 6.428 versus Latest sum 2.512 at frame 635159, 60/60 samples and 4 updates/s. Controller input 587519 is separate. Freeze did not latch; frame advanced and final click collapsed Selection instead. Freeze/Clear/Resume remain unverified. | unverified | 0154-head-light-max-performance-input-unverified.png |

Performance image 0154 is 2200 × 1584 px. Its live readings establish the captured endpoint;
coordinate delivery failed to establish a frozen or cleared snapshot.

## Settings and cleanup

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| system-auto-retained | Read System Settings Appearance after Task 12. | Original Auto retained. | Auto remains selected; Task 12 did not change it. | passed | 0155-system-auto-retained.png |
| system-reduce-motion-retained | Read Reduce Motion after Task 12. | Original off retained. | Reduce Motion remains off; Task 12 did not change it. | passed | 0156-system-reduce-motion-retained.png |

Both settings images are 1446 × 1424 px JPEG data. Editor binding returned `timeoutReached`
three times: AX read after Cmd+Q, fresh app binding after restart, then runtime reset and fresh
binding. System Settings AX still worked. No editor action after restart was observed.
`cua/native-binding-failure.json` retains the original errors; its original chronology differs
from root's corrected sequence, preserved in `supporting-audit/native-binding-chronology-audit-0156.json`.
The owned restarted QA process ended through handled SIGTERM with exit 0 (21266 presented,
0 skipped). This termination supplies no native Quit evidence. Both original INIs were restored
byte-exact after process exit, as recorded in `cua/post-binding-failure-workspace-restoration.json`.

## Retained limits

- Whole-source selection cues remain unverified, including this Light Sponza attempt. HZB
  diagnostic bounds cannot establish the editor selection outline.
- Light windowed source, camera and local-light edits/reset have scoped records. The remaining
  reconstruction, Performance, Graph, labs, output/recovery and workspace workflows still need
  actions in this context. Maximized Light has reconstruction, manual/dynamic resolution and
  scoped resets; Performance Freeze/Clear/Resume and the remaining workflows are unverified.
- Unsupported-device fallback remains unverified on this supported M3 Max. Off's N/A readings
  establish their scoped unavailable state without testing unsupported hardware.
- Actual 1× typography, held RMB fly-camera input and Graph pan remain unverified. The
  [Dark companion](ux4-head-dark-validation.md#gallery-type-samples-on-2) retains the full Gallery
  digits in Dark/Light sample palettes at 2×. Body 16 stays; no captured clipping, overlap or
  stacking justifies 17.
- Compact's 1.169919× [cost result](ux4-validation.md#task-12-cost-gate-failure) remains failed
  against 1.15×. The owner waived the stop limit and authorized continued work.
- The [roadmap completion gate](../../roadmap/editor-experience.md#completion-gate) and UX4 exit
  gate require tasks in both themes at both sizes. Task 12 is FAILED / INCOMPLETE. The owner
  authorized continued implementation while retaining failures and deferred verification in the
  [pending coverage](ux4-deferred-validation.md); review and commit precede Task 13.
