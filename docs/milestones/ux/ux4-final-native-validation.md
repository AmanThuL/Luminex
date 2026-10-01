# UX4 final native validation inventory

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

The locked-Mac inventory below describes the historical final checkpoint. Physical access
returned for the [2026-10-01 native follow-up](ux4-native-followup-validation.md), which records
performed gestures, Gallery comparisons and remaining context gaps. This table is retained as
the required action inventory; its U cells are the historical checkpoint, not a claim that no
new endpoint has since been observed.

At the historical final checkpoint, all native actions below were **UNVERIFIED** on the final restored head `874867dd…`
at `e8625de`, compared with frozen parent `28ab04a` / App `50ff3865…`.
Computer Use reported the locked Mac in the [marks checkpoint](ux4-marks-validation.md).
At that checkpoint the physical-access/manual-unlock request was unanswered; no successful
rebind or human unlock was recorded. No Task 14–19 OS preference change occurred. This inventory records
missing actions, not performed gestures. Screenshot paths were unavailable in every historical inventory cell.
Return to [combined gate results](ux4-final-validation.md) or [validation navigation](ux4-validation.md).

## Completion contexts

`U` means UNVERIFIED: not performed/observed on the final revision, screenshot unavailable.
Every action in G1–G8 below requires each head context. Parent comparison requires both sizes
under both system appearances; the parent keeps its original Dark content, without UX4 theme,
Gallery or native menu features. A changed OS title bar never establishes parent Light content.
All four current parent baseline contexts are U. Density is checked separately for cost and
readability; the binding plan does not require every workflow twice per density.

| Context | Required client / content | Per-context action list | Observation / screenshot |
|---|---|---|---|
| HDM | Head Dark, maximized usable bounds | All G1–G8 actions; A1–A9; every marks/menu action | U / unavailable |
| HDW | Head Dark, 1280 × 720 pt | All G1–G8 actions; A1–A9; every marks/menu action | U / unavailable |
| HLM | Head Light, maximized usable bounds | All G1–G8 actions; A1–A9; every marks/menu action | U / unavailable |
| HLW | Head Light, 1280 × 720 pt | All G1–G8 actions; A1–A9; every marks/menu action | U / unavailable |
| PDM | Parent baseline, maximized, system Dark | All G1–G8 baseline actions and status/chrome comparisons | U / unavailable |
| PDW | Parent baseline, 1280 × 720, system Dark | All G1–G8 baseline actions and status/chrome comparisons | U / unavailable |
| PLM | Parent baseline, maximized, system Light | All G1–G8 baseline actions and status/chrome comparisons | U / unavailable |
| PLW | Parent baseline, 1280 × 720, system Light | All G1–G8 baseline actions and status/chrome comparisons | U / unavailable |

The required [UX1 completion gate](../../roadmap/editor-experience.md#completion-gate) is fully
listed below, including actions historically performed in another revision/context.
Each `Parent` cell denotes all four parent contexts. No old screenshot supplies a current pass.

| Area | Action | Expected | HDM | HDW | HLM | HLW | Parent | Observed / screenshot |
|---|---|---|---|---|---|---|---|---|
| G1 | Launch with restored workspace, then Reset Default Layout | Native title, correct usable/windowed geometry, correct default tabs and closed detached tools; scale/appearance/density preserved | U | U | U | U | U | U / unavailable |
| G1 | Read every panel at default widths and narrow Comfortable/Compact layouts | Essential labels, values and actions reflow/scroll deliberately; no silent clipping or stacked labels | U | U | U | U | U | U / unavailable |
| G2 | Open Sponza and installed San Miguel; search subjects and duplicates, select each | Imported/source identity disambiguates names; filtering retains selection and explains Inspector content | U | U | U | U | U | U / unavailable |
| G2 | Locate viewport selection and Frame Selected, including reliable imported bounds | Identifiable visible selection cue and correct camera fit, not an inferred HZB outline | U | U | U | U | U | U / unavailable |
| G2 | Edit transforms and authored light fields, then reset | Viewport/Inspector update, dirty state accurate, authored recovery and unrelated settings retained | U | U | U | U | U | U / unavailable |
| G2 | Exercise missing optional scene/load failure and retry | Actionable unavailable/retry state with retained scene identity | U | U | U | U | U | U / unavailable |
| G3 | Switch Raw, Native TAA, MetalFX and temporal inputs | Requested/effective reconstruction and enabled state agree with render/output extents | U | U | U | U | U | U / unavailable |
| G3 | Change manual render scale; enable/disable dynamic resolution; reset | Controller input, effective scale, extents and scoped defaults accurate | U | U | U | U | U | U / unavailable |
| G3 | Read timing freshness and active-reconstruction disabled reasons | Retired/controller samples distinct from snapshot; unavailable values use N/A and explain recovery | U | U | U | U | U | U / unavailable |
| G3 | Inspect actual unsupported-device/vendor fallback | Effective fallback and actionable capability reason; supported-device probe is insufficient | U | U | U | U | U | U / unavailable |
| G4 | Open Performance details; sort costs and expand stages/pass rows/definitions | Most expensive pass, Latest/Average, samples/frame freshness and timed-sum scope explicit | U | U | U | U | U | U / unavailable |
| G4 | Freeze metrics, Clear history while frozen, Resume | Coherent held frame, empty N/A/Waiting state and fresh refill; no mixed snapshots | U | U | U | U | U | U / unavailable |
| G4 | Start stopped Measure; close Performance during run; reopen, Stop/export | Run survives close, progress/actions retained, final results/export reachable | U | U | U | U | U | U / unavailable |
| G5 | Under default TAA select graph node, expand/collapse groups, pan and zoom ≥10 s | Surviving selection/group/navigation stable across history-slot alternation | U | U | U | U | U | U / unavailable |
| G5 | Freeze graph; inspect resources and matched timings; Dump displayed frame | Header/cards/details/export share the exact owned frame, independent of metric/playback freeze | U | U | U | U | U | U / unavailable |
| G5 | Copy/Reveal dump path, Resume, cause genuine topology change | Correct output path; live publication/topology changes visible with surviving navigation | U | U | U | U | U | U / unavailable |
| G6 | Open every supported temporal/lighting/HZB view; read legend/extents/mip; Close | View-specific readings/disabled reasons legible; Close and invalidation return Final | U | U | U | U | U | U / unavailable |
| G6 | Read MaterialLab roughness/metallic axes and comparison purpose | Actual axes/purpose identifiable; generated transform edit does not prove them | U | U | U | U | U | U / unavailable |
| G6 | Load TemporalLab; Play, Pause, Step, Stop; inspect rail follow | Stopped startup, paused state, 1/60 s step and restored preview observable | U | U | U | U | U | U / unavailable |
| G6 | Change camera/light/rendering settings and scoped Reset | Authored/default values recover while unrelated settings remain | U | U | U | U | U | U / unavailable |
| G7 | Exercise capture-disabled startup, menu and C | Shared capability reason and actionable relaunch guidance | U | U | U | U | U | U / unavailable |
| G7 | Exercise capture-enabled menu and C, pending and success | One shared pending/result state and visible output path; no duplicate request | U | U | U | U | U | U / unavailable |
| G7 | Copy and Reveal successful capture output | Clipboard path matches output and Finder selects the correct capture | U | U | U | U | U | U / unavailable |
| G7 | Cause safe real dump/capture failure, then retry a valid destination | Persistent actionable reason and successful recovery | U | U | U | U | U | U / unavailable |
| G8 | Exercise menus and all panel toggles | Checked states match visibility and closed surfaces remain reachable | U | U | U | U | U | U / unavailable |
| G8 | Normal Quit/relaunch, workspace reset, detached graph/performance close/reopen/focus/bounds | Persistence/reset semantics and native access correct | U | U | U | U | U | U / unavailable |
| G8 | Use scale menu and zoom shortcuts; restore 100% | Presets/routes apply once; essential actions remain reachable in main/detached tools | U | U | U | U | U | U / unavailable |
| G8 | Focus text, type F, Home and C, copy/paste; release focus and use commands | Editing retains keys; unfocused camera/capture commands run correctly | U | U | U | U | U | U / unavailable |
| G8 | Hold RMB with WASD/QE, release; suppress keys during popup/look | Actual camera movement and stable release; original guards retained | U | U | U | U | U | U / unavailable |
| G8 | Change dock-tab membership/splitters; pan graph with held input; restore layout | Actual native delivery visibly changes geometry/navigation and restores correctly | U | U | U | U | U | U / unavailable |
| G8 | Search/filter Console, freeze via scroll, Copy visible, Clear and resume | Held matching view/counts and bounded log behavior remain coherent | U | U | U | U | U | U / unavailable |

## Appearance and restyle actions

Task 8's earlier observations at `4d193e3`, Task 10's corrected outline/dirty-marker gestures,
and Task 12's typography images remain historical. The final revision needs these actions in
HDM/HDW/HLM/HLW, with corresponding parent chrome/status/Figma baseline comparisons.
Automated resource/migration results in [final validation](ux4-final-validation.md) do not fill them.

| ID | Action | Expected | Observed / status | Screenshot |
|---|---|---|---|---|
| A1 | Read current original System Appearance and Reduce Motion before changing either | Current originals recorded, rather than assumed from Task 12 | Not read while locked; UNVERIFIED | Unavailable |
| A2 | Choose Auto; change system Light/Dark with main, Graph and Performance open | All current content/native titles follow live; Auto clears forced appearance | Not performed; UNVERIFIED | Unavailable |
| A3 | Force Light under system Dark; change system twice; open Graph later | Main/existing/new detached windows retain Light/Aqua | Not performed; UNVERIFIED | Unavailable |
| A4 | Force Dark, then Auto; inspect native menu content and OS strip | Correct override/nil behavior; OS owns strip appearance | Not performed; UNVERIFIED | Unavailable |
| A5 | Switch theme and immediately adjust scale/density within 160 ms | Exact target palette settles at new metrics; no stale colors | Not performed; UNVERIFIED | Unavailable |
| A6 | Toggle Reduce Motion and switch themes | Actual first transition frame snaps; resting endpoint is insufficient | Not performed; UNVERIFIED | Unavailable |
| A7 | Observe migration/layout and compare both themes against Figma page 05 | Readability/docks/statuses retained; actual visible deviations recorded | Not performed; UNVERIFIED | Unavailable |
| A8 | Toggle Selection Outline; edit/reset document at default widths in both themes | One blue #4CABFD perimeter, dirty title/root cue and authored reset | Not performed; UNVERIFIED | Unavailable |
| A9 | Restore both OS originals and saved editor/workspace state even after failure | Current restoration observed and original INI bytes proven separately | No OS changes made; current OS restore UNVERIFIED | Unavailable |

Earlier Task 6/8/10/12 actual OS restorations remain revision-specific. At that checkpoint, original settings,
read and restoration were explicitly UNVERIFIED; no restore pass followed from untouched
preferences or byte-exact INI restoration. The dated follow-up records the later return of physical access.

## Marks, menu and Gallery inventories

The [marks checklist](ux4-marks-validation.md) retains all 13 action/expected/observed/screenshot
rows, including the failed binding and each dirty/generated/CLI/controller source, camera/rail,
Measure Stop, capture/document activity, toolbar contraction, reserved specimens and status comparison.
Every row is U in all four head contexts; applicable parent baseline/status comparisons are U.
CPU tooltip target/placement checks do not prove native hover visibility or source readability.

The [native-menu checklist](ux4-native-menu-validation.md) retains all 127 action rows for menu
opening, commands, catalog retry, debug views/mips, appearance/density/scale, shortcuts/aliases,
focused text and Cocoa editing, disabled reasons, pending capture, detached access and docking.
Each is U in HDM/HDW/HLM/HLW; all applicable parent baseline comparisons are U.
Invisible AppKit dispatch/focus probes do not prove real-editor single dispatch, physical keys,
native tooltip visibility, Command-C/V editing, F, Home and C suppression or held RMB guards.

The [Gallery checklist](ux4-gallery-validation.md#final-capture-contexts) separately requires:

| Context | Action list | Expected | Observed / status | Screenshot |
|---|---|---|---|---|
| D100 | Capture all 21 sets/states and type ramp, Dark at 100%; compare every export | Geist ink/digits/marks/actions readable with recorded density, backing, geometry, scroll and revision | All 21 cells UNVERIFIED | Unavailable |
| D150 | Capture all 21 sets/states and type ramp, Dark at 150%; compare every export | Same complete comparison at actual native scale | All 21 cells UNVERIFIED | Unavailable |
| L100 | Capture all 21 sets/states and type ramp, Light at 100%; compare every export | Same complete comparison in Light | All 21 cells UNVERIFIED | Unavailable |
| L150 | Capture all 21 sets/states and type ramp, Light at 150%; compare every export | Same complete comparison in Light at actual native scale | All 21 cells UNVERIFIED | Unavailable |

At that checkpoint all 84 cells and four capture contexts were U. Actual 1× and final 2×
typography at 13/16/20 were U; CPU font-scale probes do not establish native zoom/backing or displayed glyph overlap.
Historical 0133/0134 at 2×, 100% and 780-point width support the retained body-16 decision
only on that revision. No observed final defect justifies body 17. The source/export difference
table remains an inspection record, not final visible deviations or a Figma pass.

## Historical gesture coverage

The [deferred inventory](ux4-deferred-validation.md) retains 151 live JSON rows and 152 audited
records through 0156, including performed Dynamic Resolution 0152 omitted from live JSON.
[Parent](ux4-parent-validation.md), [head Dark](ux4-head-dark-validation.md) and
[head Light](ux4-head-light-validation.md) retain every original action/expected/observed/screenshot
row and cutoff. The immutable audited snapshot remains unchanged. Earlier successful actions,
whole-source selection-cue limits, failed splitter/Freeze attempts, expired notices, premature
Measure-close attempts and three binding timeouts remain tied to those revisions and contexts.
No historic pass, screenshot, CPU probe or AppKit test fills a current missing gesture.
