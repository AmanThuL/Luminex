# UX4 validation

Status: In progress

Execution resumed after the owner authorized continuation. Task 5 implementation and its planned manual gate pass. Earlier clean-release Computer Use failures remain unverified. Task 6 source, automated checks and all planned gestures pass; fresh supplemental review approved it. Execution stopped after Task 7’s third failed checker review. Tasks 7–19 remain incomplete; no acceptance or merge is implied.

## Builds and automated gates

- Main and parent remain at `28ab04a`; implementation branch is `feat/ux4-design-system` in `../Luminex-ux4`.
- Tasks 1–4 are committed: `da05479`, `873b41b`, `3f0eb3`, `ea5169c`. Each passed the release build, unit tests, format check, worktree-root checkers and applicable token freshness check.
- Task 1 retains a failed debug unit gate: MilkTruck baked-key hash was `dc545f78c9574809c6102cf9f411bdcde3cf5eff18c54c6d02d28bec5341129e`, expected `602b44cf9bffe5e549fae7979de90f667fed0db71c5fbe350ce04c5a4314a1d3`. The source pin matched. Release passed; the exact arithmetic cause remains unknown. No hash, tolerance or assertion changed.
- Task 5 implementation and its validation are committed together. Release build, unit tests, format, direct root checkers and token freshness pass. The original fresh review found no production code defect. Fresh retry review confirms the stated Task 5 gesture gate passes in the manually operated instrumented editor. Duplicate clean-build menu verification is additional evidence, not a separate requirement in that task.
- The evidence-only twenty-switch probe passed: all 21 samples retained font atlas ID 2, one platform texture and 384811008 allocated GPU bytes. The instrumentation patch was reversed from production. This does not verify real menu gestures.
- Parent release captures for six scenes with `--temporal off --frames 1` exist. Candidate comparisons and the eight-round parity matrix are pending Task 8. GPU gates are pending Tasks 10 and 19.

## System settings and display

Original settings were observed with Computer Use: Appearance Auto and Reduce Motion off. Task 6 switched Appearance to Dark and back to Auto, and Reduce Motion on and back off. Both restored settings were observed. Original records exist in `system/` and `task-6/`; restored captures are listed below.

Both observed displays use 2× backing scale. No 1× gesture or typography capture has been verified. Parent release maximized client bounds were 1674 × 1052 points; windowed startup was 1280 × 720 points.

QA wrappers live only in the evidence directory and contain byte-identical executable copies; recorded hashes identify parent release, clean head and instrumented head. Task 5 runs exited normally. Task 6 clean head, motion probe and parent exited with code 0 after validation.

## Decisions

- Selection outline uses the plan default: one encoded `#4CABFD` constant for both themes.
- Body size remains 16 pending Task 12. The required 13/16/20 px gallery captures and the conditional 17 px comparison have not been performed.

## Computer Use record

Every row names the action, expected result, observed result, status and screenshot. Parent observations establish the baseline only; they do not prove candidate behavior. Instrumented observations identify their extra probe behavior explicitly. Screenshot paths are relative to `../Luminex-evidence/ux4/`.

### Parent debug baseline

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Window > Render Graph | Detached graph window opens | Render Graph native window opened with controls and graph | passed | parent-ui/graph-open.png |
| Select scene node and leave Native TAA live for more than ten seconds | Selection and graph navigation remain stable across history alternation | Scene node remains selected at the same position; live costs changed | passed | parent-ui/graph-live-selection.png |
| Click Freeze | Displayed frame and matched timings latch | Header shows Frozen \| frame 6517 | passed | parent-ui/graph-freeze.png |
| More > Dump frame | Export the frozen frame | Dump file graph-dump-frame-6517.txt exists; graph remains Frozen \| frame 6517 | passed | parent-ui/graph-dump.png |
| Click Resume | Return to live graph publication | Frozen header removed and displayed frame advanced to 10622 | passed | parent-ui/graph-resume.png |

### Parent release baseline

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Launch parent release with --windowed | 1280 × 720 pt editor with readable labels and native title bar | Native title visible; hierarchy, viewport, camera inspector and compact Performance rendered at 2×. Long imported source label scrolls horizontally. | passed | parent-release-ui/windowed-start.png |
| Window > Reset Default Layout at 1280 × 720 pt | Default dock groups restored, Inspector and Console selected | Default dock groups restored; camera Inspector and Console visible; reset notice appears in Console. | passed | parent-release-ui/reset-windowed.png |
| Use native title-bar Zoom action | Editor resizes to usable display bounds; essential labels remain reachable | Editor maximized; Inspector and Console remain visible, viewport resized; source row has horizontal scrolling. | passed | parent-release-ui/maximize.png |
| Type Local Light 9 in Hierarchy search | Matching subject remains reachable; filter preserves current selection | Search shows Local Light 9 under Local Lights; count2/24; Editor Camera Inspector retains its explicit subject heading. | passed | parent-release-ui/search-light.png |
| Click filtered Local Light 9 | Inspector names matching light and shows editable fields | Inspector shows Local Light 9, Spot, enabled checkbox, position, color, intensity75,range10,cones. | passed | parent-release-ui/select-light.png |
| Double-click light intensity, replace75 with76 and commit Enter | Value changes and document reports unsaved edits | Intensity76.000; title and document root show*; source file was not saved. | passed | parent-release-ui/edit-light.png |
| Click light subject Reset | Authored intensity restored; dirty document returns clean when canonical data matches | Intensity75.000 and title/root* cleared; reset tooltip names authored values and orbit behavior. | passed | parent-release-ui/reset-light.png |
| While search edits are active, press F, Home and C | Text/caret editing proceeds; no frame/reset/capture command fires | Query changes and Home places caret at beginning; C inserts c. Viewpoint stays on lion; no capture notice; selected light remains and hidden-selection reason is visible. | passed | parent-release-ui/text-shortcuts.png |
| Click search Clear then native Zoom to restore | Selection becomes visible; client returns1280 ×720 pt | Local Light9 reappears selected; hidden-selection warning clears; Console logs swapchain2560×1440. | passed | parent-release-ui/clear-and-windowed.png |
| Window > Performance | Detached native Performance opens with readable average/latest costs and plot units | Native Performance window opens; light stage leads descending average; Average/Latest milliseconds and wall-clock interval plot units visible. | passed | parent-release-ui/performance-open.png |
| Expand Metric definitions & exact memory | Snapshot frame/sample/freshness and timing scope are explicit | Definitions show60/60samples,4updates/s,latest retired frame,controllerN/A; timed pass sum excludes present/driver/untimed work. | passed | parent-release-ui/performance-definitions.png |
| Click Freeze metrics | Frozen coherent snapshot retains values/frame independently of playback | Frozen status visible;frame22462,published297.09s,60/60samples held; Resume tooltip explicitly waits for fresh samples. | passed | parent-release-ui/performance-freeze.png |
| More > Clear history while frozen | Frozen snapshot becomes empty; missing values say N/A or Waiting | Frozen—empty;0/60samples;latestGPU sumN/A;waiting messages shown, empty plot history. | passed | parent-release-ui/performance-clear.png |
| Click Resume metrics | Fresh sample window populates after empty state | Frozen label cleared;frame25456,new32/60samples and32intervals; values populated from freshretirements. | passed | parent-release-ui/performance-resume.png |
| Click detached Performance native Close | Performance closes; main editor remains open | Main Sponza editor remains with Hierarchy, Inspector and Console; Performance window closed. | passed | parent-release-ui/performance-close.png |
| Cmd+Q from the clean parent editor | Editor quits without a dirty-document prompt | Application exited; shell reported frame loop finished, exit0; app inventory is no longer running. | passed | parent-release-ui/before-quit.png |

### Instrumented Task 5 head

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Launch manual probe with appearanceDark | Dark token palette applied to graph editor chrome | Dark chrome and graph background; custom card colors remain existing values pendingTask7. Graph was already open on startup; its opening cause was not established. The retained patch auto-opens only in automatic resource-probe mode. | passed | task-5/cua/probe-graph-start.png |
| Close probe-opened graph to inspect main editor | Main editor remains dark from CLIoverride | Dark surfaces/neutral controls and dark canvas contrast applied; outline/cardliteral tokens pendinglatersteps. | passed | task-5/cua/probe-dark-main.png |

### Blocked Task 5 gesture

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Click View in the main editor after closing detached Render Graph | View opens; Appearance selection and scale verification can proceed | Three coordinate input calls failed before input with `windowNotFoundAtPosition((319.5, 228.0))`; readable screenshots and native AX menus remained available | unverified | task-5/cua/input-failure.png |

Attempt 1 clicked the screenshot position `[137,90]` after closing the graph. Attempt 2 rebound the exact app path, refreshed the screenshot and retried. Attempt 3 rebound the bundle ID, activated the native Luminex menu, canceled it through AX and retried. Raising the native window also succeeded but did not resolve coordinate mapping. This is a Computer Use input failure; it is not evidence that the editor menu itself failed.

The initial failure remains recorded in `task-5/cua/input-failure.json`. The authorized retry below adds successful instrumented gestures and a separate clean-release input failure. No later task or checkpoint was marked passed.

### Authorized Task 5 retry

The manual probe accepted input after a fresh session and native maximize. The clean release visibly rendered Light in both windows, but switching its appearance through the menu remains unverified. Probe mode logged actual handler timestamps and never automated these choices.

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Fresh session; native zoom; click View | View menu opens | Maximized editor accepted coordinate input and displayed View menu. This resolves the prior mapping blockage in maximized bounds. | passed | task-5/cua/retry-view-open.png |
| View > Appearance > Light, immediately Cmd+ | Within 160 ms, scale changes and settles on exact Light palette | Main UI is Light at 110%; probe choice120.435691666, scale120.565521416:129.829750ms. Final panel RGB1,1,1 at110%, transition inactive. | passed | task-5/cua/retry-light-scale.png |
| Cmd+0 | UI scale returns to100% preserving Light palette | 100% readout; Light controls retained. | passed | task-5/cua/retry-scale-reset.png |
| Window > Render Graph while Light | New graph inherits Light palette | Light controls, white nodes/details and light gray canvas visible; custom title/link hues remain Task7 work. | passed | task-5/cua/retry-graph-light.png |
| View > Appearance > Dark | Main editor retints Dark | Dark backgrounds/text/fields and100%readout; selected camera unchanged. | passed | task-5/cua/retry-main-dark.png |
| Native Window > Render Graph after Dark choice | Existing graph retints Dark | Dark canvas, node bodies and details; graph geometry retained; native title appearance belongsTask6. | passed | task-5/cua/retry-graph-dark.png |
| View > Appearance > Light at100% | Main retints Light | Light main controls and unchanged100%scale; screenshot captured. | passed | task-5/cua/retry-main-light.png |
| Choose Auto after Light; reopen Appearance | Auto is selected and resolves fallback palette before Task6 system bridge | Auto(system) checked; main retinted Dark at100%; Unknown system currently falls backDark, OS following remainsTask6. | passed | task-5/cua/retry-main-auto.png |
| Focus graph after choosing Auto | Graph matches Auto-resolved palette | Existing graph matches Dark fallback palette under Auto; system-following not claimed. | passed | task-5/cua/retry-graph-auto.png |
| Launch clean release with --appearance light | Clean graph uses Light palette | Clean graph has white node bodies/details and light gray canvas; no instrumentation. Two screenshot-only save attempts timed out. | passed | task-5/cua/retry-clean-graph-light.png |
| Native Window > Sponza in clean Light build | Main uses Light at100% | Light main controls and100%readout, clean release binary. | passed | task-5/cua/retry-clean-main-light.png |
| Click View in clean release after native Window menu focus and a fresh combined screenshot | View opens and exposes Appearance | Attempt 1 failed before input with windowNotFoundAtPosition((122.5, 78.0)); no dedicated post-failure capture saved | unverified | unavailable; earlier context only: task-5/cua/retry-clean-main-light.png |
| Reset Computer Use, bind exact clean app path, native Zoom to restore/maximize, then click View | View opens and exposes Appearance | Attempt 2 failed before input with the same error; no dedicated post-failure capture saved | unverified | unavailable; no later screenshot attributed to this attempt |
| Relaunch the byte-identical clean release in an evidence-only bundle with unique CFBundleName/DisplayName; select main window with native Window menu; maximize with native Zoom; click View. | View opens and exposes Appearance. | Computer Use error -10005 windowNotFoundAtPosition((122.5, 78.0)); View stays closed. Third clean coordinate-input attempt failed. | unverified | task-5/cua/retry-clean-input-failure-3.png |

The Light choice preceded Cmd+ by 129.829750 ms, inside the 160 ms transition. Scale changed from 100% to 110% during the fade; the logged SurfacePanel was RGB (1.000000,1.000000,1.000000), with transition inactive. The log prints six decimal places; exact application of the full target palette is established by the reviewed final-frame source path, not a whole-palette runtime dump. Raw evidence is `task-5/theme-manual-retry-run.log`; `task-5/theme-manual-retry-summary.json` preserves the timing. Auto currently resolves Unknown system appearance to Dark; real OS following belongs to Task 6.

Clean input attempt 1 failed after selecting the main window through the native Window menu. Attempt 2 reset Computer Use, rebound the exact path, and used native Zoom to restore and maximize before retrying. Attempt 3 relaunched with a unique evidence-only QA bundle display name, selected the main window, maximized, and retried. All three failed before input with `windowNotFoundAtPosition((122.5, 78.0))`. No dedicated screenshots were saved for attempts 1 and 2; their visual outcomes remain unverified. `task-5/cua/retry-clean-main-light.png` records earlier clean-window context, while `task-5/cua/retry-clean-input-failure-3.png` captures only attempt 3. Neither is attributed to an earlier failed attempt. Two screenshot-only calls also timed out; combined AX-and-screenshot capture succeeded. No product defect or bundle-name cause is established by these errors.

The byte-identical clean release and the manual probe both quit normally with exit 0. System settings remain Auto / Reduce Motion off. The stated Task 5 gate passes based on real manual-probe gestures, resource measurements and source review. Its plan does not require duplicating those gestures on the clean binary. Additional clean menu switching remains unverified. The earlier stop was honored; the owner subsequently authorized continuation. Task 5 was committed as `e00a2be`, retaining the additional input limitation. Task 6 later verified clean-release menu switching after a fresh windowed launch and native maximize.

### Task 6 system appearance and native windows

Clean-head gestures use the final Task 6 binary `6e60d936…`; the earlier two input failures retain their separate initial/final launch identities. The third adaptive attempt passed. Parent content stays Dark while its native bars follow the OS. The instrumented motion run uses `fa333d17…`, with concurrent Task 7 panel tokens and a read-only observer; its scope and patch identity are in `task-6/reduce-motion-execution.json`. The original clean Task 6 bundle was preserved.

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| Click View in clean Task 6 main editor after native focus | View opens | Computer Use error windowNotFoundAtPosition((122.5, 78.0)); no input delivered | unverified | task-6/input-attempt-1.png |
| Close native graph; rebind bundle ID directly to only main window; refresh combined screenshot and click View | View opens | Computer Use returned windowNotFoundAtPosition((122.5,78.0)); View remains closed | unverified | task-6/input-attempt-2.png |
| Launch final clean release windowed with graph closed; fresh Computer Use binding; native maximize; click View | View opens | View menu opened; clean coordinate input is working | passed | task-6/input-attempt-3.png |
| View > Appearance > Auto under current system Auto (Light) | Main chrome follows resolved system Light | Main remains Light at 100%; native title bar is Light; Auto was checked | passed | task-6/auto-main-light.png |
| Window > Render Graph under Auto/Light system | Detached graph opens with Light native title and chrome | Graph opened with Light canvas, node bodies, details and native title bar | passed | task-6/auto-graph-light.png |
| Window > Performance under Auto/Light system | Detached Performance opens with Light native title and chrome | Performance opened with a Light panel and native title bar | passed | task-6/auto-performance-light.png |
| Launch and maximize parent under system Auto resolving Light | Parent retains its baseline Dark editor with a system Light native title bar | Dark editor remained under the Light native title bar | passed | task-6/parent-main-light-system.png |
| Window > Render Graph under a Light system | Baseline graph remains Dark with native Light title bar | Detached graph remained Dark and its title bar was Light | passed | task-6/parent-graph-light-system.png |
| Window > Performance under a Light system | Baseline Performance remains Dark with native Light title bar | Performance opened with Dark panel content and Light native title bar | passed | task-6/parent-performance-light-system.png |
| System Settings > Appearance > Dark | Dark is selected | Dark was selected in System Settings | passed | task-6/system-dark.png |
| Change system Light to Dark while Performance is open and editor Auto | Performance retints live including native title bar | Performance panel and native title bar became Dark without relaunch | passed | task-6/auto-performance-dark.png |
| Change system to Dark with graph open and editor Auto | Graph retints live including native title bar | Graph canvas, node bodies, details and native title bar became Dark without relaunch | passed | task-6/auto-graph-dark.png |
| Change system to Dark while editor Auto | Main editor and native title retint live | Main editor panels, menu row and native title became Dark without relaunch | passed | task-6/auto-main-dark.png |
| Change system to Dark with parent Performance open | Baseline content stays Dark; native title follows Dark system | Parent Performance kept Dark content and its native title became Dark | passed | task-6/parent-performance-dark-system.png |
| Inspect parent graph after system change to Dark | Baseline canvas stays Dark; title follows system | Dark canvas remained; native title bar became Dark | passed | task-6/parent-graph-dark-system.png |
| Inspect parent main editor after system change to Dark | Editor stays baseline Dark; native title follows system | Main editor retained Dark content; title became Dark | passed | task-6/parent-main-dark-system.png |
| View > Appearance > Light while system Dark | Editor Light and native Aqua title remain under Dark system | Main editor switched to Light and native title bar became Aqua under Dark system | passed | task-6/forced-light-dark-system.png |
| Inspect open graph after forcing Light under Dark system | Graph uses Light content and Aqua title | Open graph retinted to Light with an Aqua title bar under the Dark system | passed | task-6/forced-light-graph-dark-system.png |
| Inspect Performance after forcing Light under Dark system | Performance Light with Aqua title | Performance content and native title remained Light under Dark system | passed | task-6/forced-light-performance-dark-system.png |
| Close graph, then Window > Render Graph after forcing Light under a Dark system | New native graph window inherits Light and Aqua | Reopened graph showed Light canvas and Aqua title bar under Dark system | passed | task-6/late-graph-forced-light.png |
| Restore System Settings > Appearance > Auto | Original Auto selected again | Auto was restored | passed | task-6/restored-appearance.png |
| Inspect parent View menu for Appearance | Parent has no UX4 Appearance command | Parent View menu had camera, debug view and UI scale controls with no Appearance entry | passed | task-6/parent-no-appearance-menu.png |
| Inspect Reduce Motion before the instrumented check | Original off preference remains | Reduce Motion remained off | passed | task-6/motion-off-confirmed.png |
| Close detached windows before the logging probe | Main editor remains with chosen Light appearance and clean document | Graph and Performance closed; Light main editor remained with no dirty marker | passed | task-6/clean-head-before-quit.png |
| View > Appearance > Dark with Reduce Motion off | 160 ms transition with intermediate palette, then exact Dark | Editor remained Light; trace has no target change. This launch used a CLI override, but the missed choice cause is unknown; no transition was exercised. | unverified | task-6/motion-off-dark.png |
| View > Appearance > Dark without CLI override; Reduce Motion off | Fade samples intermediate palette and finishes exact Dark | Dark editor/title visible; trace frame2150 starts at exact origin, frames2151–2153 are intermediate, frame2156 is exact Dark. | passed | task-6/motion-off-menu-dark.png |
| System Settings > Accessibility > Motion > Reduce motion on | Switch on; editor reads the real preference | Reduce motion visibly on | passed | task-6/system-motion-on.png |
| View > Appearance > Light with Reduce Motion on | Exact Light snap in first prepared frame | Light editor/title visible; first changed-target frame5052 has inactive transition and exact whole Light palette. | passed | task-6/motion-on-light.png |
| View > Appearance > Dark with Reduce Motion on | Exact Dark snap in first prepared frame | Dark editor/title visible; first changed-target frame7429 has inactive transition and exact whole Dark palette. | passed | task-6/motion-on-dark.png |
| Restore System Settings Reduce motion off | Original off setting restored | Reduce motion switch visibly off | passed | task-6/restored-motion.png |
| Observe System Settings Appearance after restoring Auto | Original Auto remains selected | Appearance Auto visibly selected | passed | task-6/final-restored-appearance.png |
| Select parent Sponza in native Window menu after motion preference restored | Parent remains usable with original OS preferences | Parent baseline main window observed after restoration | passed | task-6/parent-motion-restored.png |
| Focus parent Sponza while Reduce Motion on | Observe unchanged parent baseline under on setting | Window menu helper did not find Sponza while a native submenu remained open; no on-setting parent screenshot captured | unverified | unavailable |

The off control sampled an intermediate palette and reached the exact endpoint after 162.175125 ms. With Reduce Motion on, each opposite-theme choice was already exact at `since_change=0`, with no active or pending transition. Equality covers all RGBA channels in all 63 roles. This proves the first prepared frame that observed the target, not physical click-to-display latency. Logs and assertions are retained in `task-6/reduce-motion-menu-run.log` and `reduce-motion-summary.json`. Startup and the first missed menu choice do not count. Toggling Reduce Motion during an active fade remains unverified.

The observer was reversed and `EditorShell.cpp` restored to SHA-256 `ffad4f5c…`; the clean App was rebuilt. No instrumentation or screenshots are committed. The OS originals were restored even though the supplemental parent-on focus observation failed. Auto changes, forced Aqua bars and late-created graph appearance passed on the clean head. Source review and all Task 6 automated gates passed before the observer was applied.

### Task 7 stopped after three failed reviews

Panel token replacements and xmake/CI wiring have no reported review defects. The literal-color checker remains unapproved and uncommitted. Attempt 1 missed explicitly typed declarations/returns and misattributed nested lambda returns. Attempt 2 fixed those examples but missed `if constexpr` color returns and falsely flagged templated non-color lambdas. Attempt 3 introduced delimiter-aware callable classification; all 16 tests pass, but independent review still found two P2 defects:

- A defaulted function template, `template<typename T = int> ImVec4 ink() { return {1,0,0,1}; }`, escapes checking because the header contains `=`.
- `operator[]` callable boundaries are missed, producing false positives on a local class’s integer-array returns and missing an explicitly typed ImVec4 operator’s numeric return.

The exact reproductions and logs remain in `task-7/`; no fourth attempt was made and no gate was loosened. The initial C++ layout run also failed with 661 orphan test separators while compile-command regeneration overlapped it. A stable sequential rerun passed 2673 definitions / 352 files; its original cause remains unproven. Build, unit, format, root checks and token freshness passed before the checker reviews. Later partial checks do not override the failed review gate. Task 7 is left for an explicitly authorized retry; Tasks 8–19, push and PR are pending.

## Remaining verification

Tasks 7–19, Figma comparisons, Style Gallery typography, density, actor/provenance marks, native menu and shortcut dispatch, the UX1 completion gestures in both themes, exact-image gates and the final GPU suite remain pending. ADR 0029 has not been created; when created it must remain Proposed until separate milestone review.
