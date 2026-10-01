# UX4 unlocked-Mac native follow-up validation

**Status**: In progress

The 2026-10-01 follow-up obtained native editor observations after physical access returned.
Coverage remains partial. It does not pass the complete native gate, change the failed
image/cost/resource gates, accept UX4, or close its plan. The original locked-Mac records remain
historical checkpoints; their blanket missing coverage is no longer the current access state.

Final gesture cutoff is 0104: 101 retained ledger records (IDs 0003–0104 with the original gaps).
The ledger records 88 PASS, 12 UNVERIFIED and one OBSERVED result, each scoped to its own action.
The original System Appearance/Reduce Motion and both original INIs were restored; the cleanup
proof below distinguishes observed OS settings from byte equality and process/runtime integrity.

## Identity and evidence

Source revision is `5105fa18e0af9d41d86ad3103d447352155faadc` on `feat/ux4-design-system`.
Production App SHA-256 is `874867dd4156eead419a04881216473a799de8da780467b99a584812d88f05f7`.
Frozen parent App SHA-256 is `50ff38651a8af579d1e58b2acc4f82ca06414dd3eed379a0a739f58f31020906`.
The full `runtime-before.json` records the shader inventory; App identity alone does not identify
the complete runtime. These observations supplement the [publication runtime results](ux4-final-validation.md#publication-runtime-follow-up).

Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`.
`E/gestures.json` is the controller's action/expected/observed/result/screenshot ledger.
[Part 1](ux4-native-followup-gestures-1.md), [part 2](ux4-native-followup-gestures-2.md),
[part 3](ux4-native-followup-gestures-3.md), [part 4](ux4-native-followup-gestures-4.md)
and [part 5](ux4-native-followup-gestures-5.md)
retain each performed gesture without replacing earlier failed or unverified attempts.
PASS applies only to the recorded action and context; composite gate rows still require their
missing steps and required contexts. Context labels preserve the ledger's actual observations.

The screenshots are original CUA JPEG bytes with corrected `.jpg` extensions.
`E/screenshot-format.json` records the format; the files retain the native editor and
window chrome. Gallery input and export hashes are retained in the independent review JSONs.
An initial QA launch lacked Fonts/Icons and was excluded; `E/setup-limitations.json` records
normal Quit, byte-identical production resource staging and original INI recovery before valid
validation. This packaging correction is not an implementation change.

## Scoped observations and retained failures

| Area | Newly observed endpoint/actions | Remaining limits |
|---|---|---|
| Appearance | Original OS Auto/Reduce Motion off read through System Settings; actual Light/Dark changes retint existing Auto Graph, Gallery, Performance and main content | Forced modes/new detached behavior, native appearance leaves, first transition frame and simultaneous change within 160 ms remain unverified |
| Scale/type | Cmd-minus reaches 90%; Cmd-0 restores 100% from Gallery; Cmd-plus reaches 150%; all 13/16/20 type specimens captured in both themes at 100/150%, Comfortable, 2× | Native scale leaves/presets/aliases, actual 1×, Compact and whole-editor narrow readability remain unverified |
| Focused text | HDW Hierarchy search retains F, Home and C; physical Cmd-A/C/X/V select/copy/cut/paste; Clear restores rows/selection | Edit menu routes, Cocoa chooser editing, other fields/contexts and held-look guards remain unverified |
| Performance | HDW Auto/Dark Details/definitions, stage expansion, Latest/Min/Max/Samples sorting, Freeze/Clear/Resume observed | Equal sample counts do not prove unequal sorting; Light functional sequence and other required contexts remain missing |
| Measure | HDW 6000-frame run survives close/reopen, completes and exports; second run stops via strip and reports canceled at 1790/6000 | 0049 cancellation remains unverified because its run completed; other themes/sizes remain missing |
| Graph | 0017 retains frozen Dark frame 16967/scene 0.546 ms; 0061 fullscreen Light frame 230585 matches scene card/Details 1.336 ms and resource rows | Neither evidence fills required normal-window held navigation, owned Dump, Copy/Reveal, topology change or complete Freeze/resource checks in all contexts |
| Dump failure/retry | 0028 actionable Cannot open output guidance; final external graph-dump-frame-16967.txt exists with recorded hash | 0060 retry captures fullscreen Graph, not success; owned-frame content agreement, notice/Copy/Reveal remain unverified |
| Capture | 0063 bare C in HLW without capture enable shows persistent relaunch guidance | Capture-enabled menu/C/pending/success/copy/reveal and duplicate dispatch remain unverified; an automated gputrace is separate artifact evidence |
| Documents/marks | HLW authored light clean/enable dirty marks; external Save As/menu Save and Revert Cancel/Discard; chooser opens MaterialLab; generated source hover names generator/not-saved; generated disabling stays clean/session-only | Cmd-S at 0080 did not save; three intensity-edit routes at 0067 did not change 45.000; numeric Edited/reset/tooltip and CLI source remain unverified; generated hover duplicates its sentence |
| Reconstruction | HLW Raw and supported MetalFX requested/effective endpoints; topic Reset returns Native TAA; manual scale 0.52 shows 699×447/1344×860; dynamic controller settles Active/1.00/budget 16 ms | Gear tooltip at 0073 unverified; unsupported/fallback, view matrix, full freshness/reset semantics and other contexts remain missing |
| Console | HLW scroll freezes rows, Copy visible reports 48 matches, frozen Clear empties counts, ↓0 new resumes | Clipboard payload and nonzero arrival/loss behavior remain unverified; other required contexts missing |
| Camera/transport | HLW Editor Camera endpoint after activation; Step changes visible 0.000→0.017 s while paused; Stop restores 0.000/camera while keeping clean/session-only state | 0091 Play/Pause does not establish advancing time; animated pose/rail restoration and remaining contexts remain unverified |
| Window geometry/input | HLM Auto/Light fills 3348×2168 pixels including title at 2×; HLW returns to 2560×1504 including 64-pixel title | These endpoints do not prove docking/reset/persistence; three splitter drags at 0078 leave boundaries unchanged |

0080's Cmd-S remains UNVERIFIED after the title stayed dirty; 0081's menu Save succeeds through
its separate route. Generated sphere source tooltip at 0088 repeats the provenance sentence
twice. Its visible blue outline is observed without an exact encoded-RGB claim. 0089 disabling
a generated sphere preserves clean document state; its unobserved Editor Camera click is separate
from 0090's later endpoint after main-window activation.

0014's original Freeze/scene-resource screenshot was overwritten by the later paste screenshot.
`E/gesture-ledger-correction.json` preserves the loss; 0014 remains UNVERIFIED, and 0017 is later
frozen-theme evidence only. 0060 was corrected to UNVERIFIED: the selected native control entered
full screen. 0062 then failed to leave Graph fullscreen through three approaches; independent
checks continued after restarting the owned QA process. No successful Dump notice follows.
0033 changed the viewport on F but did not establish reliable imported-bounds fitting or a visible
selection cue. 0049 does not become a cancellation pass because the later second run succeeded.

`E/native-submenu-blocker.json` records three unsuccessful control routes. Nested Appearance,
Density, UI Scale, Debug View and catalog leaves remain unverified under that automation blocker;
no product submenu failure is confirmed. An endpoint or CLI override cannot fill a menu gesture.

## Three-attempt stop limits

The following native control issues reached three failed approaches. Their missing outcomes
remain unverified; restarting for independent checks did not convert them to passes.

| Issue | Retained evidence | Unverified outcome |
|---|---|---|
| Nested native menu leaves | `E/native-submenu-blocker.json` | Appearance/Density/UI Scale/Debug View/catalog leaf selection; no product failure established |
| Leave Graph fullscreen | 0062, `E/gesture-0062.jpg` | Three approaches did not close fullscreen; earlier owned session ended by SIGINT |
| Authored light intensity | 0067, `E/gesture-0067.jpg` | Double-click/text/drag left 45.000 unchanged; numeric edit/source/reset sequence |
| Dock splitter movement | 0078, `E/gesture-0078.jpg` | Three drags left boundaries unchanged; dock geometry/restoration |
| Parent search coordinate input | 0098, `E/gesture-0098.jpg` | Three clicks returned windowNotFoundAtPosition; focused-text comparison |

## Gallery comparisons and body size

Independent [Dark comparisons](ux4-native-followup-gallery-dark.md) and
[Light comparisons](ux4-native-followup-gallery-light.md) list every component's expected states,
actual screenshot paths and every recorded visible deviation. Sources are
`E/native-gallery-dark-review.{md,json}` and `E/native-gallery-light-review.{md,json}`.

All 21 families and every labeled native state are OBSERVED_WITH_DEVIATIONS in D100/D150/L100/L150,
at Comfortable density and 2× backing. The Light review enumerates 86/86 native states per scale.
Larger 780×720-point captures and targeted 0018/0021–0026 resolve the identified digit, overline and
Failure-text occlusions. The earlier 702×648-point D150 series remains only 9/21 fully captured
rows; its omissions and pointer occlusions remain historical, with no retroactive pixel claim.
The first unfocused `gallery-D100-*.jpg` bulk series is excluded from Gallery conclusions.

Checked Menu item visibly renders `? Auto (system)` instead of a checkmark in both themes/scales.
Pending/Applied Proposal cards each advertise two changes while displaying one numeric diff.
These are visible defects/content mismatches, not missing capture cells. Full-width stacked
fixtures, square controls, mark placement, text, composite structure and each additional deviation
remain individually listed in the comparison companions. No Figma parity pass follows.
Specimen appearance does not establish actual Hover/Active, native menus, clipboard or live action
behavior. Light L100 fixture tooltips were displayed; that does not verify NativeMenu tooltips.

Body 16 remains. At 100%, all six ramp samples (13 Regular, 16 Regular, 16 Medium, 20 Medium,
13 Mono, 16 Mono) are complete without overlapping digits. At the reviewed 150% width, longer
16/20 samples wrap cleanly onto separated lines; clean targeted Light 0021 and Dark overlap-a-00
show the complete 20 Medium sequences. Fully visible specimens show no internal clipping,
label collision or digit overlap that supplies a need for 17. No body-17 remedy was tested.
Actual 1× backing, Compact density and whole-editor panel fit remain unverified.
The selection outline retains the binding one encoded `#4CABFD` default in both themes; these
Gallery captures do not establish a new outline gesture or exact JPEG token equality.

## Whole-editor comparison

The independent [editor/Figma comparison](ux4-native-followup-editor-comparison.md) records
all 16 findings with individual screenshot paths. Its input ledger fingerprint precedes the
controller's transcription corrections; reviewed image bytes are unchanged. Sources are `E/native-editor-figma-review.md`
and `.json`, covering 25 native images and four default-workspace exports. Geometry, selected
subjects, focus, scene topology and camera differ; the full context comparison remains incomplete.

The generated-subject tooltip visibly repeats its source sentence. Light local-light color
fields have colored edge strips but no visible R/G/B letters (0064/0079/0081), a label concern
against the stated vector contract. Light viewport panel padding is white (0057/0058/0087);
its relationship to the dark `surface/viewport` surround requirement needs scope review.
No code cause or repair is claimed. Other individually recorded differences include vertical
XYZ rows, retained dock affordances, Console's six square filters/table, documented square
controls, wider digit advances, provenance placement and transport order.

0058's image is 3348×2168 pixels including its 64-pixel title, giving a 3348×2104 client.
The original ledger transcription of 2176 is preserved separately by the controller's correction.
0047's action observation is 1694/6000; the screenshot reads 2322/6000 during the live run.
Neither reading establishes cancellation, which is separately verified by 0052–0054.

## Remaining required coverage

The [original native inventory](ux4-final-native-validation.md) continues to own the full
G1–G8/A1–A9 checklist and HDM/HDW/HLM/HLW with applicable PDM/PDW/PLM/PLW parent comparisons.
This follow-up fills only the named actions/contexts above. Parent 0094–0097 observes windowed
launch under OS Light, detached dark Graph with light native title, Freeze frame 1677, Fit and
Close to the old editor menu/Inter/tab controls. This is partial PLW baseline evidence only; the
remaining parent chrome/status/workflows and contexts stay unverified. Parent content stays its
original Dark UI under either OS appearance. 0099 observes its OS Dark title endpoint; 0100
observes maximized 3348×2168 pixels including title, with a 3348×2104 swapchain. Three parent
search-coordinate attempts at 0098 fail `windowNotFoundAtPosition`; no text was entered. These
are partial PDW/PDM endpoint observations, not their functional workflow pass. OS chrome alone
does not establish Light content.

Still missing are full scene/duplicate selection and authored field/reset workflows; reliable
outline/bounds; real load failure/retry; every supported debug view/legend; MaterialLab axes and
TemporalLab transport/rail; held RMB movement/release/guards; actual docking; native menus and
all remaining named shortcut/alias routes; complete live mark sources/tooltips; toolbar contraction;
complete whole-editor Figma/context comparisons; relaunch/layout persistence; and the missing contexts
for each partly observed workflow. Unsupported-device/vendor fallback needs an actual unsupported
condition. No 1× display context is supplied.

`E/inventory-draft.{md,json}` ends at 0020 and is historical. `E/followup-coverage-audit.md`
ends at 0059 and is a later partial mapping; subsequent ledger rows supersede its missing-action
claims only where their actual observations support them. Neither snapshot is the final cutoff.

## Restoration and unchanged gates

`E/original-system-settings.json` records Auto and Reduce Motion off, with original screenshots
0002-original-appearance.jpg and 0001-original-motion.jpg. 0020 observed Reduce Motion off again.
0101 visibly restores Auto; 0102 reopens Accessibility > Motion and confirms Reduce Motion off.
`original-system-settings.json` records the final observed values and screenshot paths.

`E/cleanup-integrity.json` proves both saved workspace originals restored byte-exact:

| Workspace | Original and restored SHA-256 |
|---|---|
| Head | `1f1e1a2780df02bb3a0e47f0687e8236407e74fdaeaeaa8b1100c223955e6134` |
| Parent | `dcc831fbaf29b07a596e55c49e36bc2eba508ec58ef264d49910c111591e2b6d` |

0103/0104 invoke native Quit in the final head/parent sessions. Both owned processes return
exit 0 and their logs record completed frame loops; process inventory confirms they are absent.
Their cited screenshots precede Quit, and the parent menu AX snapshot supplies no pixels.
Closing-frame pixels, relaunch and persistence remain unverified. The older Graph fullscreen session ended by handled SIGINT and
does not establish normal native Quit for that session. `E/runtime-after-native.json` matches
all 127 head runtime-file hashes in `runtime-before.json`; both App hashes remain unchanged.
These integrity checks establish no missing native workflow or renderer gate.

The final owned external document pair reflects the actual Save:
`ux4-native.scene.gltf` has SHA-256
`75043d9ae0d86ada755c3cce957b525052667a294ab7f98e423bbee1c8bf418a`, and
`ux4-native.scene.bin` has SHA-256
`0ce0488a83c3b9662460a25590469eba060e6b761303ea623eed3a426ca1678e`.
The external graph dump exists with SHA-256
`bab6f92782a2db441bb6b808f04c418d971106e0f91ba5139d7d9b053200f430`;
its successful notice was not observed, so Copy/Reveal stay unverified.

| Final evidence input | SHA-256 |
|---|---|
| `gestures.json` | `d0e43260aba2ff68be27c92cdb135be8e073d4531ec4098bdc8e6f5a53f22f22` |
| `native-gallery-dark-review.json` | `ae1290029beb4a4defdd9e545f58b3bce8a0e3c61ecc8871c411a57fa31a401b` |
| `native-gallery-light-review.json` | `1d64bf2213d2cff406f92f3ceda2d063ca7d48cf74c02e5fcce217e9d41b617e` |
| `native-editor-figma-review.json` | `5f0c48f2f17063ccac66bf01107463bbcb7eda0dd6136ad339705d580c9bce1f` |
| `cleanup-integrity.json` | `0cf68dc61123abc1cb89a53fd9b3019ca1d64b6f059205e4570c9bf6ae97bf34` |
| `runtime-before.json` | `cbb553f7b2b2c3e76f9f7dbe4a3f74223850d2690f0d4de97bddacff69d50a77` |
| `runtime-after-native.json` | `cbb553f7b2b2c3e76f9f7dbe4a3f74223850d2690f0d4de97bddacff69d50a77` |

The publication image matrix remains 11/15 FAIL, both Compact cost cells still exceed 1.15×,
and both resource attempts still fail constant allocation. The native follow-up reruns none of those three gates and changes no tolerance. [Combined results](ux4-final-validation.md) retain the measured numbers.

Separate commit checks are recorded under `E/globals-final/`; their results are outside this
native observation record and do not change its remaining gesture coverage.

The first commit-check attempt is retained under `E/globals-final-attempt-1/`. Build, unit tests,
format and compile-command generation passed; project policy then rejected the slash-separated
F, Home and C shorthand as a home path. The two prose occurrences now spell the keys with commas;
the checker is unchanged. The full rerun uses `E/globals-final/`; no result is assumed here.
