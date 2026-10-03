# UX6 — Task 22 final measurements

**Status:** Implemented — owner authorized integration on 2026-10-04 after the [milestone review](ux6-review-validation.md), which fixed the open P2 below; failed and incomplete gates retained as measured.
All 22 tasks have an implementation/validation record; incomplete native gates remain explicit.
Measurements cover `afc3bf4` plus the two reviewed source corrections below, retained by Task 22.
Publication follows final independent review. No owner acceptance or merge is claimed.
The [execution validation](ux6-validation.md) retains earlier failures and scoped owner decisions.

## Frozen inputs

Evidence is under `../Luminex-evidence/ux6/`, relative to the candidate checkout.
Final App SHA-256: `6f5c9115c0d098bfe8deed572648e718cc9b6a107fd46ac55a688ad6eb4963dd`.
`task22-final-freeze.json` pins that executable, 135 runtime shader/font/icon files, 63 source
shaders and 18 document/companion copies. `task22-final-app/` is the final frozen runtime;
the earlier `task22-frozen-app/` is retained and was not used for the final image runs.
The frozen parent remains `80fa716`, App SHA
`4fa704c3029c4c1e97ac76509fbba91471dbe8eac4eeba8cf2450b36b63ec352`;
its 190 pinned executable/runtime/source hashes still match. It was never rebuilt.

The live accepted reference remains SHA
`c89e2c6f434a87f4b282bc822fa4490424f5def5c64d2c43186cb86a0f8ef24c`.
Side-specific references change only frozen document pins. No image hash, tolerance or comparison
rule changed in Task 22. Native edits use separate document copies, outside these capture inputs.

## Automatic gates

| Gate | Measured result | Evidence |
|---|---|---|
| Build, CPU, format, compile database, direct root policy checks, Python | 13/13 commands passed; 363 Python tests, suite time 11.145 s | `task22-final-gates.json`, its 13 logs |
| Full Metal validation GPU suite | Passed; xmake reports 48.350 s, outer command elapsed 59.884 s | `task22-gpu-final.json`, `.log` |
| Positive writer refresh | 66 assertions, one case passed | `task22-positive-writer.json`, `.log` |
| Pinned document validator | 91/91 passed: six catalog, 85 discovered writer outputs | `task22-validator.json`, `.log` |
| Linked module dependency check | Passed: 631 files, ten units, one external | `task22-extra-checks.json` |
| Generated theme freshness | Passed | `task22-extra-checks.json` |
| Remote parent identity | Remote main matches frozen parent | `task22-extra-checks.json` |

The first full GPU run failed. Its direct diagnostic passed 226/227 cases and
1,181,831/1,181,832 assertions. The sole failure was the missing-directional-role fixture:
converting lights into inert groups left `Movable` on groups, which the mobility contract forbids.
The fixture now sets those groups `Static` and explicitly requires model validation before Save.
Every original assertion remains. The focused repair passed 13 assertions in one case, followed
by the passing complete GPU run. The initial failure and diagnostic are retained, not overwritten.
The Inspector reset tooltip also now describes its existing pose-lock restriction accurately.
These are Task 22’s only two source corrections; no runtime validation was weakened.

The CPU/GPU command drivers overlapped briefly; these are correctness results, with no performance
adoption. The validator count describes this actual run, not the earlier 107 or 92 outputs.

## Final image gates

Eight alternating AB/BA rounds completed 240 standing and 80 LightLab captures. All captures
completed, their source/runtime/document pins matched, and each case had one stable hash per side.

| Comparison | Measured result |
|---|---|
| Standing against parent-observed hashes | **10/15 passed; gate failed**. Five MaterialLab cases differ |
| Standing paired exactness | 80/120 pairs equal: Sponza 40/40, TemporalLab 40/40, MaterialLab 0/40 |
| Candidate against owner-accepted reference | **15/15 each round; 120/120 captures passed** |
| Parent against that accepted reference | 10/15 each round; 80/120 captures passed |
| LightLab parent/candidate exactness | **5/5 modes, 40/40 pairs equal**; all 80 captures match the retained anchor |

`task22-standing/summary.json` and `task22-light/summary.json` retain every round and report.
The standing driver's exit 1 represents its measured comparison failure, not a capture error.
The Task 12 Sponza MetalFX parent-repeat outlier did not recur in this final run.
The original standing gate remains failed despite the earlier scoped MaterialLab reference acceptance.

| MaterialLab mode | Differing RGB pixels | Pixels >8 | Maximum channel delta | Mean absolute RGB delta |
|---|---:|---:|---:|---:|
| Off | 1,802 | 1,802 | 141 | 0.1720811631944 |
| Native TAA 1 | 1,885 | 1,847 | 141 | 0.1636848958333 |
| Native TAA 0.5 | 2,894 | 2,207 | 145 | 0.1652025462963 |
| MetalFX 1 | 39,552 | 1,910 | 141 | 0.1879459635417 |
| MetalFX 0.5 | 35,272 | 2,176 | 141 | 0.1891131365741 |

All other standing pairs have zero RGB difference. These MaterialLab differences repeat the
Task 12 measurements. `task22-standing/rgb-differences.json` holds actual pixel measurements;
>8 is descriptive and creates no tolerance. Both sides retain the unavailable-Helmet-mips
load-time fallback; the image gate does not certify its baked texture path.

## Native authoring checks

The private QA bundle runs the same frozen executable, with copied documents and isolated runtime
state. Two initial attachment timeouts were followed by a successful detached-process attachment;
the first short-lived process and broker-launched process are documented separately. Capture-disabled
PID 70604 exited through Cmd+Q. Fresh capture-enabled PID 72164 reloaded the saved Helmet override.
`task22-native/observations.json`, launch logs and private saved JSON retain the observations.

- Restored 1280×720 pt client layout, native title bar and reachable main panels were observed.
  Reset Default Layout hid Session and selected Console. Native zoom produced a 3348×2104 px
  swapchain and 2132×1524 px viewport.
- Q/W/E/R/Y/X typed into focused Hierarchy search appended literal text and did not change tools.
  Helmet filtering, source selection and F framing worked. Outside text input, View hid handles;
  Move, Rotate, Scale and Combined displayed their handles in World/Local; X toggled space.
- Inspector numeric entry changed Helmet X from 6.2 to 6.5 and moved its gizmo pivot. Dirty title,
  root, subject and field marks appeared. Saved scale was actually `[1,1.0000001,1.0000001]` after
  decomposition, rather than exact `[1,1,1]`. Cmd+S succeeded and a fresh process showed X 6.500
  with a clean title. This certifies numeric entry and persistence, not native held field dragging.
- Play made Helmet handles gray; Stop restored its saved pose. Disabled Helmet geometry disappeared
  while gray handles remained; re-enabled and saved. MaterialLab's transport time stayed 0.000 s;
  no animated playback conclusion follows from that check.
- LightLab Pillar 1 showed disabled pose fields, checked Static, gray handles and the exact hover
  reason `Static: mobility is authored in the scene file`. Clicking Static left it unchanged.
  Enabled false hid the pillar, retained its row and saved successfully in the private document.
  Fresh LightLab reload is unverified.
- Generated LightLab rows were initially collapsed. Expansion showed generator labels/tooltips.
  Point light 0 accepted Inspector X/Z = 0 and displayed only movement handles with Combined.
  Spot light 1 accepted the same session pose entry; E displayed rotation rings and W movement.
  Native light dragging remains unverified; these generated-light edits are session-only.
- An accidental Viewport close was recovered through Window > Viewport, restoring the spot rings.
- After owner-authorized retry, the display-name binding recovered input. TemporalLab rotating-cube
  selection/F showed disabled fields, `Animation owns this transform` and gray handles. Play at
  1.200 s showed Y 108.000°; Pause at 18.867 s showed Y 102.000°, Step at 18.883 s showed
  Y 100.500°, and Stop restored 0.000 s, Y 0.000°, position `[-3,1,0]` and unit scale.
  The title stayed clean. TemporalLab’s stopped imported Wheels/Wheels.001 rows still displayed
  `not saved`; the record’s absolute no-catalog-row wording is not satisfied by that observation.
  Separate review retains this as a P2 product/contract finding. The animation-preview provenance
  path can append this suffix; the exact native cause remains unproven. It needs a follow-up fix
  or owner decision before that outcome can be claimed met.

No successful held gizmo manipulation is certified. Task 20's two atomic drag attempts highlighted
handles without changing the pose. The documented input API provides atomic drag but no held-button
sequence for Escape/selection/playback changes mid-drag or W/E with RMB held. These native gestures,
all tool/space drag combinations, Inspector live-follow during drags and held field dragging remain
unverified; passing model/wrapper tests do not substitute for them.

## Completion-gate inventory

Each task below maps to the roadmap's eight completion tasks. Partial observations are explicitly
separate from unexercised parts; the entire native completion gate is **incomplete**.

| Task | Observed | Unverified |
|---|---|---|
| 1 Layout/readability | Restored/reset 1280×720 pt and maximized main panels, native title bar | Full narrow/reflow inventory for every surface |
| 2 Find/select/edit/restore | Helmet search/F/Inspector entry/Save/relaunch | Sponza/San Miguel duplicate-name search and complete edit/restore gestures |
| 3 Reconstruction/resolution | Raw/Native TAA/MetalFX, temporal inputs Off/on, actual extents; dynamic resolution Active at scale 1 then Inactive; reset Native TAA | Budget settling, lower-scale native status, unsupported-device fallback |
| 4 Performance | Average/latest stage rows; Freeze at frame 9140 with 60/60 samples, Clear shows Frozen-empty/waiting, Resume repopulates | Most expensive individual pass identification and full sort inventory |
| 5 Render Graph | Scene-node selection retained >10 s and physical slot alternation; matched card/details timings; Freeze frame 17999, dump that exact frame, Resume frame 19498 | Pan/zoom, group expansion and changed-topology recovery; dump notice not observed |
| 6 Debug/playback/recovery | Helmet tool/space choices, disabled/static/animated handles, Home camera reset, reconstruction reset, TemporalLab Play/Pause/Step/Stop | All debug modes/legends and full camera/light/render recovery sequence |
| 7 Dump/capture | Actual frozen graph dump; C explains disabled capture, relaunch with capability makes capture succeed with Copy/Reveal notice | Complete scene-only draw inventory, induced capture failure/recovery, Copy/Reveal gestures |
| 8 Menus/workspace/focus | Layout reset, Save/quit/relaunch, Performance/Graph close, Viewport close/reopen, text-focus shortcut suppression | Dock/undock, detached Graph reopen geometry/focus and full workspace/menu persistence inventory |

Render Graph's scene card/details both showed 0.448 ms initially and 1.306 ms when frozen;
`graph-dump-frame-17999.txt` begins `render-graph frame 17999`. Resume showed both 0.296 ms.
These are displayed samples, not performance comparisons. The enabled native capture produced
`luminex-frame.gputrace`, 492,563,095 bytes. Task 20's retained Xcode replay observed scene,
temporal, selection, display and UI encoders but did not certify the complete scene-only draw list;
its decoder error/warnings and runtime issue remain in the linked gizmo validation.

Actual native screenshots were shown in the tool transcript; local before/after screenshot files
were not saved. The Task 20 scene-only screenshot pair remains byte-identical at 469,607 bytes each;
its operator-approved Session route was used because the combined CLI flags are rejected.
Generic Blender import remains the scoped Task 12 result, with full material/asset appearance unverified.

## Review and deviations

`task22-whole-branch-review.json` reviews the branch and the two source corrections, excluding
its reviewer's own Task 20 implementation, which `task20-review.json` separately reviews.
The reviewer independently rehashed all 320 final BMPs, read all 32 reports, recomputed RGB metrics,
verified frozen runtime/source corrections and checked automatic-gate logs. No discrepancy was found.
Final validation documentation and status changes receive a separate review before the Task 22 commit.

Owner-approved agent reuse, source/helper scope additions, signed-zero repair and resumed scale-domain
restriction remain in the linked execution/exporter/mobility/gizmo pages. Review one tier above Astra
is unavailable; highest-available independent Astra review plus ROOT design review was used.
No renderer/RHI/shader/instance ABI/MotionClass/workspace/capture/measurement schema changed.
The first final-document policy check rejected two status spellings; they were corrected to the
existing allowed `Implemented` prefix without changing the policy or any gate.

## Retained stops and owner-authorized continuation

The File menu initially produced two invalid-AX-element clicks; its documented Cancel action
recovered the menu and the native LightLab chooser then succeeded. Later, the TemporalLab
Go To Folder chooser failed three path-entry approaches: `typeText` produced `/s/...`, `setValue`
left `/`, and Cmd+A with `paste` left `/` unchanged. Execution stopped immediately after the
third attempt. `task22-required-stop.json` preserves the exact approaches and observed AX values.
No further UI workaround, relaunch, implementation or publication followed before owner direction.
The owner subsequently instructed "resume". After that authorization, Escape dismissed the path
sheet; the ordinary Open panel already had TemporalLab selected. Open succeeded, and its native
scene, hierarchy and camera Inspector were observed. This does not prove the earlier failure cause.

On the next resumed turn, native input failed three times with `noWindowsAvailable`: the existing
binding, a fresh bundle-ID binding after inventory/screenshot, and rebind plus click in the same
invocation. Read-only binding and screenshot still showed `TemporalLab — Luminex`; no rotating-cube
selection, F action or animation gesture is certified. `task22-input-required-stop.json` retains
the attempts. Execution stopped again immediately, with no further UI, implementation or publication.

The owner then requested one retry and explicitly authorized recording a remaining input failure
and continuing the remaining work. Display-name binding recovered keyboard and coordinate input;
the TemporalLab observations above followed. The earlier failures remain retained and their cause
is unproven. Unexercised native gestures stay unverified, rather than being reported as passed.

Task 22 records the incomplete completion gate, failed original standing gate and all deviations.
After final independent review, its planned publication is one commit, `ux6-integration-chain`,
the feature branch push and one PR. The plan stays present until separate milestone closure;
implementation status is not owner acceptance, and no merge is authorized.
