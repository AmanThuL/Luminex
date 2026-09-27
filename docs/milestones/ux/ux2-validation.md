# UX2 — Editor surfaces validation

**Status**: Implemented — owner accepted on 2026-09-27; evidence limits retained.

The [implemented record](ux2.md) owns placement and conventions. The owner accepted UX2 overall
after the [completion gate](../../roadmap/editor-experience.md#completion-gate) checklist, without
per-gesture results, then authorized local closure. The executor plan is closed and removed. The owner authorized
publication and squash integration on 2026-09-27.

## Revisions and evidence

The parent is `de6bdcb` on `docs/ux2-design`. The candidate is `9fe2e3a` on
`feat/ux2-editor-surfaces`, in the sibling `Luminex-ux2` worktree: the workspace migration is
`76a266c` (`editor: migrate the workspace to schema 4 (UX2)`) and the operator documents are
`9fe2e3a` (`docs: describe the UX2 editor surfaces`). Each change had a separate implementation
and review, with the strongest review for the Debug View model and the workspace migration.
The local annotated tag `ux2-editor-surfaces-validated` preserves `9fe2e3a`; closure changes only
documentation. The plan remains recoverable there, with the final acceptance copy retained as
`task15/closed-plan.md` in the local evidence archive.

Raw evidence is retained outside source in the owner's local `Luminex-evidence/ux2/` archive.
Paths below are relative to that archive; they are local evidence, not published attachments.
`execution.md` records the serial commits, tests-first failures, reviews, corrections and limits.
Per-task implementation/review reports retain the underlying logs and source hashes.

## Automated results

The gates after the workspace migration and the operator documents passed from the worktree root, using `-P .` on every xmake
invocation. Root checkers ran directly; `xmake policy` was not used.

| Check | Result |
|---|---|
| Build and formatting | Passed |
| `Tests/unit` | 744 cases, 1,070,918 assertions passed |
| Python suites run with unit tests | 179 tests passed: 169 + 4 + 6 |
| Project policy | 844 files passed before this validation record |
| Shader imports | 63 shaders, 77 imports passed |
| Module dependencies, including link check | 492 files, 10 units, 1 external component passed |
| Standalone public headers | 167 passed; zero allowed failures |
| C++ comments | 336 owned files passed |
| C++ layout | 2,141 definitions in 308 files passed |
| Submodule pin | Passed |
| Protected-path diff from `de6bdcb` | Empty |
| GPU suite with Metal validation | 201 cases, 1,160,546 assertions passed |

The protected diff covers `Source/Render`, `Source/Engine`, `Source/Scenes`, `Shaders/`,
`RojoRHI/` and `Source/App/Headless`. No renderer, CLI, measurement-schema or capture-output
change is included. Gate logs are in `task13/integrated-gates/` and `task14/integrated-gates/`;
the acceptance GPU results are retained in `task15/gpu.log`.
The run used `MTL_DEBUG_LAYER=1 xmake test -v -P . Tests/gpu` (the `[gpu]~[.]` filter),
on Apple M3 Max with Metal 4, macOS 26.7 build 25G229. `task15/environment.json` records the host.

### Scene-only identity

All eight catalog scenes are installed, including San Miguel. Each parent and candidate run used
`--scene <id> --temporal off --frames 4 --screenshot <id>.png` from its binary directory.
All 16 runs exited successfully; the eight pairs have identical PNG SHA-256 values.

| Scene | PNG SHA-256, identical on parent and candidate |
|---|---|
| Sponza | `c97102403c886c39f29abf94cc1d8814baeb8cda8af571e73351f91197c8ac00` |
| Damaged Helmet | `e4a157c4de1209643ffe2b148ad2e7b8d973bc7361daf8b29c002b33c5b9e244` |
| Milk Truck | `72e7021bcfd1673e20b3bfb21af6cb81efde3d6b590448e66b5fee884cbe48f8` |
| MaterialLab | `71a8e01435081665242694cd03c34348cce70b48df26b682917b15973b3c977e` |
| TemporalLab | `b03760107f3bb25737b8c960aea4d8c63cc671233ec4e745eb6e5d7d18bb418f` |
| San Miguel | `7f31e5279cd9ef3961e3f87479b8ec311f47bb9c92c8406a0c7063d7b4e80cf3` |
| VisibilityLab | `efbbe19f1bf14d8c4c7d69cc5ab8a92192cc344b6024d2de64bbc0b94fb16b82` |
| LightLab | `3668c5cc91487fca7eb16775977c3ca3bee2a1f2ca675cf6b0de44f4a5d4892d` |

`task15/identity-results.json` records full revisions, binary hashes, commands, working
directories, elapsed times and output hashes. PNGs and logs are in `task15/identity/`.
Both Damaged Helmet runs reported a missing baked mip chain and used runtime mip generation,
whose normal maps omit offline per-level renormalization. The shared fallback still produced
identical outputs; this evidence does not claim that scene used offline-baked textures.
This checks editor-only image identity under the prescribed settings; it does not establish new
performance conclusions or close historical M6/R1/M7 image, capture or reliability exceptions.

### Workspace migration and focused probes

The workspace migration passed 309 production docking/settings checks across 1280 × 720 and 1920 × 1080 at
100% and 150% scale, including reset while either new tab had focus. A failed reset-focus probe
was retained, then corrected by focusing Viewport after selecting the default neighbour tabs.

A frozen pre-migration `393575e` editor saved an actual schema 3 workspace with both detached
windows open at 150%. Candidate first and second native launches each presented 60/60 frames.
Both preserved all six old visibility bits, scale, detached viewport IDs and exact window origins
and sizes; Rendering and the compact Performance tab became visible. Inspector and Console were
the selected neighbours. The first launch rebuilt docks once; the second rebuilt them zero times.
`task13/native-migration-result.json`, saved ini files and launch logs retain those assertions.
The owner's original main-worktree ini was backed up before testing and was not replaced by the
fixture. Native Reset Default Layout has overall owner acceptance but no separate gesture log.

Earlier probes include 100 Inspector header/grid checks and four default-grid docking checks;
140 Console checks; 641 Performance checks; and eight Graph header configurations plus eight
dump-to-notice checks. These are production-code offscreen probes, not manual UI passes.
The Console count/chip may trail the message child by one frame. A 640 × 210-point Performance
summary at 150% needs 67 pixels of vertical scrolling. Saved custom narrow widths remain saved.

## Owner checkpoints and final completion matrix

The owner accepted the first three wave checkpoints, including the corrected transport icon
centering. The second checkpoint was accepted with “Looks good to me. Continue”; the third with
“Looks good, continue”. These are overall checkpoint acceptances, not fabricated per-gesture
records. The first icon-centering adjustment was insufficient; the subsequent visible-ink
centering correction was reviewed and accepted.

On 2026-09-27, after the final gate report and running-editor checklist, the owner replied
“Looks good, continue”. This accepts UX2 overall and authorizes local closure. It does not supply
individual action results, a size/scale matrix or new screenshots. The table below preserves
those evidence limits rather than assigning unsupported per-action passes.

The supplied checklist covered the native title bar, restored and Reset Default Layout states,
maximized usable bounds and 1280 × 720 points at 100% and 150% scale, with essential labels and
actions reachable through deliberate reflow or scrolling. Each row has overall owner acceptance;
individual gestures remain unverified where no separate result was supplied.

| Owner task | Current result / evidence limit |
|---|---|
| Restore, quit/reopen and reset schema 4; Rendering beside Inspector, Performance beside Console; neighbours selected; preserve scale and detached bounds on restore | Native migration passes above; individual owner gestures unverified |
| Sponza and San Miguel: search, distinguish duplicate names, select/frame/outline, edit and reset a transform; filter selected rows; inspect unavailable optional content | Individual action results and unavailable-content gesture unverified |
| Raw, Native TAA and MetalFX; temporal inputs and dynamic resolution; requested/effective mode, extents, freshness and unavailable/fallback reasons | Individual action results unverified; no unavailable value may read as valid zero |
| Every available Debug View and legend, chip switch/Final, HZB mip; invalidation to Final with notice; MaterialLab axes; TemporalLab play/pause/step/stop and rail follow; camera/light/scoped Rendering resets | Individual action results unverified |
| Performance summary and Live share a snapshot; identify costliest stage/pass, expand/sort, explain Latest/Average/timed sum, freeze/clear/resume; Measure Start/Stop/completion, progress and preview restoration | Individual action results unverified |
| Native TAA Graph selection, group state and pan/zoom for ten seconds; expand scene-import bundle; freeze, inspect exact resources/timings, dump same record, resume; real topology changes | Individual native gestures and record comparison unverified |
| Console severity/search, scroll-up freeze, new-arrival chip, return-to-bottom/chip resume, frozen Copy visible and Clear | Native clipboard and individual scrolling gestures unverified |
| Capture-disabled startup reason; enabled capture success and recoverable failure; dump/capture notices, output path, Copy path and Reveal | Individual native capture/path/failure actions unverified |
| All menus, visibility, detached Graph close/reopen, docking, focus, shortcut suppression during text entry, open popups and right-button look; fly camera, F, Home and C, and Cmd UI-scale shortcuts | Individual native gesture matrix unverified |

No new before/after editor screenshots or per-action records accompanied final acceptance.
The automated scene-only PNGs above are not editor screenshots. A gesture not reported or
observed remains unverified; overall acceptance does not fill that evidence gap.

## Corrections after acceptance

On 2026-09-27 a whole-branch review after local closure found defects that the checkpoints had not
exercised; they were corrected on the same branch after the validated tag, so the owner's
acceptance above does not cover them and each corrected behaviour below is unverified in the
running editor until the owner rechecks it.

- F in the focused Render Graph also framed the selection in the main camera; F, Home and C now
  ignore keys while the Render Graph or Performance window has focus. C without capture again
  posts the reason. The legend chip no longer covers the occlusion overlay caption.
- Debug View availability used the requested reconstruction, so a MetalFX fallback to Native TAA
  hid Rejection, Weight and Age; it now uses the effective mode, as before UX2. Scoped resets no
  longer clear or count the Debug View. The oracle adds `--visibility cull|off` (96 modes).
- Reset icons had lost the changed state the earlier Modified labels showed; each is enabled only
  when its scope differs. Display compared and restored the renderer's navy default instead of the
  editor's grey, a pre-UX2 reset error now corrected, and the guide's Display row with it.
- Smaller corrections: a copy or reveal error keeps its notice; the transport shows Warmup,
  Measuring and Finishing; a wheel-down at a shrunken Console end resumes it; severity chips mark
  every shown level; the fallback warning moved to the Rendering panel top; stale comments.

The corrected head passes the build, format and every root checker; `Tests/unit` passes 750 cases
and 1,086,407 assertions; `MTL_DEBUG_LAYER=1` GPU cases pass 201 cases and 1,160,546 assertions;
all eight scene-only PNGs match the table above. With `codicon.ttf`
moved aside, a 30-frame editor launch exited normally and logged exactly one icon warning.

## Scope adjustments and publication

Implementation added small App-only helpers and observer fields where needed for truthful
freshness, retained Performance readings, notice routing and default-width behaviour; per-task
reports retain their tests-first and review evidence. None changed the protected paths. The
operator documents also corrected obsolete operator routes in the frame-pipeline, GPU-visibility and
temporal-comparison guides, beyond its four named documents. Fenced commands stayed unchanged.

The owner accepted local closure with these evidence limits, then authorized publication and
squash integration on 2026-09-27; the validated revision's tag is published with the branch.
UX3 remains inactive and requires its own execution authorization.
