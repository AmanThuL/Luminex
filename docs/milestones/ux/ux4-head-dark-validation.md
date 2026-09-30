# UX4 head Dark gesture validation

**Status**: In progress

This companion retains actual head Dark observations for [UX4 validation](ux4-validation.md).
Its snapshot contains 130 total `gestures.json` rows and ends at
`head-gallery-light-13-16-20` (0134). The 57 head rows below retain 55 passed
results and two unverified results. All 57 associated screenshots were inspected. Later live
rows are outside this snapshot; this is incomplete completion-gate coverage.

Evidence is outside the repository at `../Luminex-evidence/ux4/`; screenshot paths below are
relative to `task-12/cua/` within that root. The external QA wrapper is a byte-identical copy of
the clean radius-0 candidate: SHA-256
`3749a58a714e4b574be10212e904c7ad82f82f9a99df512fafcd4d50596f0900`.
The schema-4 fixture is installed only for the recorded QA session.

These rows cover Dark at 1280 × 720 client points, with native dark title chrome, 100% scale and
2× backing scale. Comfortable startup and Compact density/reset are observed. The remaining
actions use the restored 100% geometry; a screenshot does not prove a theme/density menu choice
unless its gesture row records that choice. Every saved image below is JPEG data named `.png`.
Main-window images are 2560 × 1504 px including the 64 px native title region; Performance
images 0093–0096 are 2200 × 1452 px, Graph images are 2560 × 1664 px and Finder 0103 is
1840 × 928 px. Gallery 0133/0134 images are 1560 × 1440 px. Detached-window dimensions do
not change the main client's windowed context.

The [parent companion](ux4-parent-validation.md) preserves comparison behavior and unverified
attempts. Compact's [failed cost gate](ux4-validation.md#task-12-cost-gate-failure) stays failed
under the owner's continuation exception. Gallery rows cover Dark/Light sample content on 2×;
they do not establish Light application workflows, maximized completion tasks or 1× typography.
Body 16 remains unchanged.

## Windowed layout, selection and keyboard focus

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-restored-windowed | Launch the head build with --windowed and the schema-4 fixture. | A 1280 × 720 client restores existing docks under Auto/Comfortable and uses the native title bar. | The 2560 × 1504 capture shows native dark title chrome, Hierarchy, Inspector/Rendering and Performance/Console docks; Geist labels and values remain readable at 100%. Inspector uses vertical XYZ fields. | passed | 0079-head-dark-restored-windowed.png |
| head-dark-compact-windowed | Choose View > Density > Compact at 100% in the windowed editor. | Density reduces spacing while all default-width labels and digits remain readable. | Rows become tighter; Inspector numeric fields show complete values and vertical XYZ, transport and summary remain readable. | passed | 0080-head-dark-compact-windowed.png |
| head-dark-reset-windowed-compact | Choose Window > Reset Default Layout at 1280 × 720 in Compact. | Default docks rebuild, Inspector/Console select, detached windows close and scale remains 100%. | Inspector and Console are selected, hierarchy/viewport/right and bottom docks remain reachable and values readable at 100%; no detached window opens. | passed | 0081-head-dark-reset-windowed-compact.png |
| head-dark-windowed-sponza-frame | Search Crytek, select Crytek Sponza and press F. | Filtered selection identifies its source and locates the whole source object. | Search returns 2/24 with Crytek Sponza (25 primitives); Inspector explains one source node controls all 25 primitives. F changes the view to a wall, but a whole-source outline/location is not visible. | unverified | 0082-head-dark-windowed-sponza-frame.png |
| head-dark-windowed-sponza-edit | Edit Crytek Sponza Position X from 0 to 0.1. | Transform changes and document gains visible unsaved state. | Position X is 0.100, wall moves, title is Sponza* and the root shows a blue unsaved dot; unrelated Y/Z, rotation and scale stay unchanged. | passed | 0083-head-dark-windowed-sponza-edit.png |
| head-dark-windowed-sponza-reset | Click the subject Reset and press Home outside text editing. | Authored transform and camera return without dirty state. | Position XYZ return to 0, scale stays 1, title loses * and viewport returns to the lion wall authored camera. | passed | 0084-head-dark-windowed-sponza-reset.png |
| head-dark-windowed-text-shortcuts | Double-click Search, enter fc, press Home/F/C, then Cmd+A/C, Right and Cmd+V. | Shortcut letters edit text and Copy/Paste works; hidden selection is explained. | Search reads fcfcfcfc, 0/24; Hierarchy and Inspector explicitly explain retained hidden selection and offer Clear filter. Camera stays at the authored lion view. The existing unavailable capture notice originated in the prior unfocused attempt; no new log appeared. | passed | 0085-head-dark-windowed-text-shortcuts.png |

## Reconstruction and resolution

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-raw | Choose Reconstruction Algorithm > Raw. | The effective label identifies Raw and full render/output extents remain readable. | Raw · 100%, render/output 1344 × 860; temporal inputs and jitter remain enabled. | passed | 0086-head-dark-windowed-raw.png |
| head-dark-windowed-metalfx | Choose MetalFX Temporal from Algorithm. | Requested and effective MetalFX and dimensions remain visible on supported hardware. | MetalFX Temporal · 100%, render/output 1344 × 860, and vendor pack/history libraries load. Unsupported-hardware fallback cannot be observed on this M3 Max. | passed | 0087-head-dark-windowed-metalfx.png |
| head-dark-windowed-capability | Expand Reconstruction Diagnostics and scroll to the vendor rows. | Capability and valid ranges are readable and unavailable capability is not presented as zero. | Vendor capability is MetalFX Temporal, scale range 0.33–1.00, generation 1; long diagnostics labels wrap deliberately and remain readable. | passed | 0088-head-dark-windowed-capability.png |
| head-dark-windowed-inputs-off | Disable Temporal inputs with MetalFX requested. | Effective reconstruction becomes Off, requested choice is retained and inapplicable readings use N/A. | Off · 100%, render/output 1344 × 860; Requested: MetalFX Temporal; algorithm/jitter disabled with retained settings, History/Warmup/age/index show N/A. | passed | 0089-head-dark-windowed-inputs-off.png |
| head-dark-windowed-native-reset | Click Reconstruction Reset from inputs Off with MetalFX retained. | Native TAA with inputs and jitter on returns; scene transform stays clean. | Native TAA · 100%, inputs/jitter checked, valid warmed history and TemporalEnabled reset event; title stays clean. | passed | 0090-head-dark-windowed-native-reset.png |
| head-dark-windowed-manual-scale | Interact with Render scale, attempting text entry 0.75. | Manual scaling shows its actual effective scale and distinct render/output extents. | The gesture sets 0.74 (not the attempted 0.75), Native TAA · 74%, render 995 × 636 versus output 1344 × 860; Controller Inactive. | passed | 0091-head-dark-windowed-manual-scale.png |
| head-dark-windowed-dynamic-live | Enable Dynamic resolution from manual scale 0.74 and observe controller settling. | Active controller publishes current scale, budget and extents and updates over time. | Retained image shows Dynamic resolution enabled, budget 16 ms, Native TAA 100%, render/output 1344 × 860 and Console steps 0.74 through 1.00. | passed | head-dark-windowed-dynamic-live.png |
| head-dark-windowed-resolution-reset | Click Resolution Reset after enabling dynamic resolution. | Manual scale returns to 1.00, controller off; Reconstruction and scene remain unchanged. | Scale 1.00, Dynamic resolution unchecked, Controller Inactive, Native TAA with inputs and jitter on, title clean. | passed | 0092-head-dark-windowed-resolution-reset.png |

## Performance snapshots

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-performance-freeze | Open Performance Details and Freeze its Live snapshot. | Average and Latest remain distinct and the highest average stage can be identified. | Frozen snapshot shows Scene highest at 2.331 ms average, 1.863 ms Latest, timed pass sum 7.477 ms; render/output 1344 × 860. | passed | 0093-head-dark-windowed-performance-freeze.png |
| head-dark-windowed-performance-definitions | Expand Metric definitions and exact memory while Frozen. | Snapshot freshness, samples and exclusions explain Average versus Latest and FPS. | Frame 83646, 60/60 samples, 4 updates/s; Latest compatible pass sum 7.080 ms differs from average 7.477. Text excludes present, driver and untimed work; geometry remains readable. | passed | 0094-head-dark-windowed-performance-definitions.png |
| head-dark-windowed-performance-clear | Choose More > Clear history while Frozen. | Frozen data empties and does not refill until Resume. | Frozen — empty, Frame 0, 0/60 samples, N/A costs and extents; table and chart clear. | passed | 0095-head-dark-windowed-performance-clear.png |
| head-dark-windowed-performance-resume | Resume Live metrics after Clear. | Fresh retired samples repopulate table and chart. | Frozen label disappears; new nonzero frame and sample counts, costs and chart repopulate. | passed | 0096-head-dark-windowed-performance-resume.png |

## Graph selection, frozen dump and reopen

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-graph-expand | Double-click the collapsed Light stage. | Its four passes appear; removed stage selection is explained. | Reset, Count, Scan and Fill cards replace the Light group; selected-item-no-longer-visible explanation appears. | passed | 0097-head-dark-windowed-graph-expand.png |
| head-dark-windowed-graph-fit-selection | Scroll to zoom out, select Exposure seed, then Fit selection. | Zoom and fitting keep selected pass details and matched GPU time. | Exposure seed is centered and enlarged; resource r9/version0/write and StorageWrite barrier remain visible; card and Details times match in retained frame. | passed | 0098-head-dark-windowed-graph-fit-selection.png |
| head-dark-windowed-graph-one-to-one-before | Set 1:1 and retain Exposure seed selection under Native TAA. | Selection and navigation stay stable through history alternation. | Exposure seed card stays at the center with Details for r9 version0/write; live exact timing updates. | passed | 0099-head-dark-windowed-graph-one-to-one-before.png |
| head-dark-windowed-graph-one-to-one-after | Observe the selected live graph more than 10 seconds after the 1:1 baseline. | Same selection and canvas placement survive ongoing Native TAA frames. | Exposure seed card bounds and r9/version0/write Details persist; live timing changes without losing selection or zoom. | passed | 0100-head-dark-windowed-graph-one-to-one-after.png |
| head-dark-windowed-graph-freeze | Freeze Render Graph and inspect pass resources and timing. | Displayed frame and exact matched details latch independently of Performance/playback. | Frozen frame 116476; Exposure seed GPU 0.076 ms matches card, r9/version0/write and StorageWrite barrier stay visible. | passed | 0101-head-dark-windowed-graph-freeze.png |
| head-dark-windowed-graph-copy | Dump frozen graph frame, switch to main editor, click Copy path and paste into Console search. | The copied path names the displayed frozen frame. | Console search contains the head build graph-dump-frame-116476.txt path; three retained logs name the same frozen frame. | passed | 0102-head-dark-windowed-graph-copy.png |
| head-dark-windowed-graph-reveal-attempt | Click Reveal in Finder on a fresh frozen-graph dump notice. | Finder selects graph-dump-frame-116476.txt. | Finder selects graph-dump-frame-116476.txt in the head release directory; preview names render-graph frame 116476 and an 8 KB text document. Initial transient interpretation was wrong; see correction audit. | passed | 0103-head-dark-windowed-graph-reveal-attempt.png |
| head-dark-windowed-graph-reopen-resume | Close and reopen Render Graph using Window, then Resume. | Reopen retains geometry and frozen selection; Resume publishes a new exact frame. | Reopened graph retains 1:1 Exposure seed selection and Frozen116476; Resume removes Frozen and updates timing while selection remains. | passed | 0104-head-dark-windowed-graph-reopen-resume.png |

## San Miguel source edits and framing

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-san-miguel-source | Load San Miguel, search Miguel and select its source node. | Catalog scene and source identities stay clear; Inspector names source scope. | San Miguel title, filtered 2/7 rows and SanMiguel source controlling 281 primitives; XYZ0 and scale1 are readable. | passed | 0105-head-dark-windowed-san-miguel-source.png |
| head-dark-windowed-san-miguel-edit | Edit SanMiguel source X to 0.100. | The source moves and dirty state appears. | X reads 0.100, courtyard shifts, title gains *, and root shows a blue dirty dot. | passed | 0106-head-dark-windowed-san-miguel-edit.png |
| head-dark-windowed-san-miguel-reset | Use source Reset. | Authored transform and clean document return. | X returns to 0.000, courtyard returns, dirty title and blue dot clear. | passed | 0107-head-dark-windowed-san-miguel-reset.png |
| head-dark-windowed-san-miguel-frame | Press F with the complete source selected. | Fit the complete source and show a legible selection cue. | Camera changes to an exterior wall/underside view; the complete-source selection cue cannot be assessed. | unverified | 0108-head-dark-windowed-san-miguel-frame.png |

## Generated MaterialLab subjects

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-generated-shaft | Select and frame the first +X red axis row. | Inspector distinguishes generated object and selection outline is visible. | Inspector names +X red shaft, shows Generated by material-lab · not saved, scale .900/.070/.070 and blue outline. | passed | 0109-head-dark-windowed-generated-shaft.png |
| head-dark-windowed-generated-tip | Select and frame the second visually similar +X row. | Select a different subject without identity collision. | Inspector names +X red tip with X7.200 and scale .160 on all axes, and outline follows the tip. | passed | 0110-head-dark-windowed-generated-tip.png |
| head-dark-windowed-generated-edit | Edit generated red tip Y to -1.900. | Object changes while generated session-only status stays and document remains clean. | Y reads -1.900, blue outlined tip moves, Generated by material-lab · not saved remains, title has no *. | passed | 0111-head-dark-windowed-generated-edit.png |
| head-dark-windowed-generated-reset | Reset generated tip and press Home. | Restore generated transform and authored camera. | Y returns to -2.000, full sphere grid and helmet return, title stays clean. | passed | 0112-head-dark-windowed-generated-reset.png |

## TemporalLab playback

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-temporal-stopped | Load TemporalLab. | Scene starts stopped with readable camera and transport. | Transport reads0.000s, Play/Stop/Step accessible, authored camera XYZ0/3/10 and FOV45 displayed. | passed | 0113-head-dark-windowed-temporal-stopped.png |
| head-dark-windowed-temporal-play | Click Play. | Time advances and animated objects move. | Time advances above zero, Pause tooltip explains holding state, rotating box and orbit sphere move. | passed | 0114-head-dark-windowed-temporal-play.png |
| head-dark-windowed-temporal-pause | Click Pause. | Time and animation hold, Step becomes available. | Retained screenshot shows paused time 0.883 s with Play tooltip; objects remain at paused positions. Original 10.883 s was a transcription error; see correction audit. | passed | 0115-head-dark-windowed-temporal-pause.png |
| head-dark-windowed-temporal-step | Click Step once while paused. | Time advances1/60second and remains paused. | Displayed time changes from0.883s to0.900s; Step tooltip states1/60second, objects stay paused. | passed | 0116-head-dark-windowed-temporal-step.png |
| head-dark-windowed-temporal-stop | Click Stop. | Time and animation-owned preview restore. | Time returns to 0.000 s; the original box and sphere poses return and the document stays clean. Stop's tooltip describes restoring the starting scene and camera state. | passed | 0117-head-dark-windowed-temporal-stop.png |

## Diagnostic legends and Final

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-motion-legend | Select Motion vectors. | Legend defines channels and invalid pixels. | Gray zero motion and a magenta invalid box are visible; the legend maps red horizontal, green vertical, channel 0.5 + 8 × UV delta and clipping beyond ±0.0625 UV. | passed | 0118-head-dark-windowed-motion-legend.png |
| head-dark-windowed-reprojection-legend | Select Reprojection error. | Legend explains error magnitude and unavailable comparisons. | Mostly black error and a blue invalid box; the legend maps 4 × pre-exposed linear-color luminance difference, white at 0.25 or more and blue for unavailable comparison. | passed | 0119-head-dark-windowed-reprojection-legend.png |
| head-dark-windowed-reprojected-legend | Select Reprojected history. | Explain color history separately from error magnitude. | Color history and a blue invalid box are visible; the legend explains exposure correction before clipping, PBR Neutral/sRGB and invalid history blue. | passed | 0120-head-dark-windowed-reprojected-legend.png |
| head-dark-windowed-rejection-legend | Select Rejection mask. | Legend maps rejection causes and clipping. | Black accepted pixels, a magenta invalid box and green neighborhood clipping are visible; red disocclusion, yellow reactive and blue outside history are explained. | passed | 0121-head-dark-windowed-rejection-legend.png |
| head-dark-windowed-weight-legend | Select Blend weight. | Explain blend alpha without implying final normalized contribution. | The invalid box is white and stable surfaces dark; black 0/white 1 and the value before inverse-luminance reweighting are readable. | passed | 0122-head-dark-windowed-weight-legend.png |
| head-dark-windowed-age-legend | Select History age. | Explain accumulation and invalid history. | Stable pixels are white and the invalid box dark; floor(age)/16, white at 16 or more and rejected pixels restarting at one frame are readable. | passed | 0123-head-dark-windowed-age-legend.png |
| head-dark-windowed-final | Close the diagnostic legend. | Return to Final without changing rendering settings. | The colored Final scene returns and the legend disappears. | passed | 0124-head-dark-windowed-final.png |
| head-dark-windowed-count-legend | Select Lighting Count. | Legend maps stored light counts for visible surface froxels. | Green 5–16 and yellow 17–64 regions are visible; the legend also maps black 0, blue 1–4 and red 65–128. | passed | 0125-head-dark-windowed-count-legend.png |
| head-dark-windowed-overflow-legend | Select Overflow. | Explain magenta truncation over dimmed Final. | Final appears dimmed without magenta regions; the legend specifies 25% brightness and explains retained candidates in unmarked froxels. | passed | 0126-head-dark-windowed-overflow-legend.png |
| head-dark-windowed-missed-legend | Select Missed. | Distinguish list errors from expected truncation loss. | The viewport is black with no missing contribution; red untruncated list error and yellow truncated expected loss are explained. | passed | 0127-head-dark-windowed-missed-legend.png |

## GPU occlusion and HZB controls

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-dark-windowed-gpu-occlusion | Choose GPU classifier, then enable Occlusion. | GPU enables occlusion and exposes readable history and counts. | Occlusion is checked; history Valid, 401 kept/4096 candidates, 3695 culled, 7/408 occluded/tested and zero history-invalid retained. Narrow reading labels wrap without overlap. Earlier live counts differ; see correction audit. | passed | 0128-head-dark-windowed-gpu-occlusion.png |
| head-dark-windowed-hzb-zero | Select View > Debug View > Occlusion > HZB level 0. | Level-zero depth has a readable legend and mip stepper. | Title HZB level 0, value 0, minus/plus and Close are visible; the legend explains farthest reversed depth and black uncovered pixels. | passed | 0129-head-dark-windowed-hzb-zero.png |
| head-dark-windowed-hzb-one | Click HZB mip plus. | Mip becomes 1 and legend matches. | Title/value become HZB level 1/1; tooltip explains that higher levels summarize a larger source region. | passed | 0130-head-dark-windowed-hzb-one.png |
| head-dark-windowed-hzb-back-zero | Click HZB mip minus. | Mip returns to 0. | Title/value return to HZB level 0/0 and the depth legend remains readable. | passed | 0131-head-dark-windowed-hzb-back-zero.png |
| head-dark-windowed-hzb-close | Click HZB legend Close. | Return to Final. | Colored VisibilityLab Final returns and the legend disappears; Occlusion remains checked. | passed | 0132-head-dark-windowed-hzb-close.png |

## Gallery type samples on 2×

| Record | Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|---|
| head-gallery-dark-13-16-20 | Open Style Gallery at 100% on the 2× display. | Digit strings at 13, 16 and 20 px remain separated and complete. | Current Dark palette shows Regular 13/16, Medium 16/20 and Mono 13/16 digits fully visible at the 780-point Gallery width; no overlap or clipping. | passed | 0133-head-gallery-dark-13-16-20.png |
| head-gallery-light-13-16-20 | Choose Light Gallery palette at 100% on the 2× display. | Digit samples remain complete without overlap. | Light sample content shows all six type rows separated and readable; outer Gallery chrome stays Dark. No overlap, clipping or stacking supports changing body 16. | passed | 0134-head-gallery-light-13-16-20.png |

## Retained-image limits

- The manual-scale attempt is 0.74/74%, Render 995 × 636 px versus Output 1344 × 860 px.
  The attempted 0.75 is not a passed exact numeric setting.
- The dynamic image shows its checkbox enabled, budget 16.00 ms, Native TAA 100% and
  Render/Output 1344 × 860 px. Console retains scale steps 0.74→0.79→0.84→0.89→0.94→0.99→1.00.
  The Controller status row is below the captured area; its exact label and retired timing
  freshness are not independently established by that image.
- The text-shortcut row records an earlier unfocused attempt that produced an unavailable
  capture notice. The retained focused test shows fcfcfcfc, filtered-selection explanations and
  the old notice. It does not turn that earlier input attempt into a suppression pass.
- Sponza and San Miguel whole-source Frame Selected cues stay unverified; changed framing alone does not
  establish a clearly located imported selection.
- Supported MetalFX capability and its 0.33–1.00 range are visible. Unsupported-device fallback
  remains unverified on this supported M3 Max.
- Reconstruction Reset returns Native TAA with valid warmed history and a clean title.
  Resolution Reset disables the controller and retains Native TAA. These scoped observations
  do not prove recovery for every Rendering topic.
- Performance highest Average, definitions and freeze/clear/resume have direct windowed records.
  The definitions distinguish frame 83646's Latest sum 7.080 ms from Average 7.477 ms and
  controller input frame 39033 at 9.054 ms. They do not claim the controller input is current.
- Graph wheel zoom, Fit selection, 1:1 selection retention, frozen matched timing, same-frame
  Dump/Copy/Reveal and close/reopen/Resume have scoped records. Pan and a genuine topology
  change remain open; the 1:1 comparison does not verify pan.
- Generated MaterialLab shaft/tip full identities and blue selection cues pass their scoped
  comparison; generated tip edit/reset stays session-only. Roughness/metallic grid-axis and
  lab-purpose explanation remains open.
- TemporalLab Play/Pause/Step and Stop/starting-preview restoration are recorded.
  Play image 0114 reads 20.017 s, Pause 0115 reads 0.883 s and Step 0116
  reads 0.900 s. The endpoints do not establish a continuous 20.017→0.883 playback sequence.
- All temporal and lighting legends, HZB 0→1→0 and Close to Final have scoped records.
  Actual disabled-view reasons, light recovery, capture outcomes, remaining menu/visibility
  persistence, fly-camera and actual docking remain open.
- Gallery 13/16/20 samples pass their scoped Dark/Light content check at 2× and 100%.
  A Light sample palette does not verify the Light application theme. No actual 1× capture is
  retained; no captured overlap, clipping or stacking justifies a 17 px body change.

## Observation correction audit

The original 0103 observation said Finder still showed the prior parent trace and marked Reveal
unverified. Root and the supporting agent inspected that same retained image: Finder selects
`graph-dump-frame-116476.txt`, its preview names frame 116476 and shows 8 KB. Root confirmed the
initial interpretation was wrong and no image overwrite occurred in this resumed turn. The same
gesture is corrected to passed; no retry is invented. The external correction audit preserves
the original observation/status and corrected observation while Computer Use appends continue.

Parent 0016's retained values are Active at 1.00 with Render/Output 2136 × 1528 px. Its external
correction audit preserves the earlier 0.94/0.89 and 1901 × 1360 claim as unverified transient values,
matching the existing parent companion. Neither correction changes the failed Compact cost gate.

Pause 0115 originally transcribed 10.883 s. The retained screenshot reads 0.883 s; Step 0116
reads 0.900 s with a 1/60-second tooltip. Root identified the typo and independent image
inspection confirmed it. The corrected row preserves the original claim in external
`task-12/supporting-audit/observation-corrections.json`. Live JSON correction is deferred while
Computer Use appends continue; the audit keeps earlier DR and Reveal corrections as well.

Occlusion 0128 originally recorded live counts of 402 kept and 6/408 occluded/tested. Its saved
image shows 401 kept and 7/408, with 3695 culled. Later HZB frames visibly alternate these
counts. The frozen corrected snapshot and external audit retain the original observation and
use the saved frame's exact values; the enabled/history/readability result remains passed.

The [roadmap completion gate](../../roadmap/editor-experience.md#completion-gate) and UX4 exit gate
require observable tasks in both candidate themes at both window sizes. These partial windowed
records do not substitute for actions in the other three cells.
