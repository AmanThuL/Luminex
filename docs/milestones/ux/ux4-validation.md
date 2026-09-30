# UX4 validation

**Status**: In progress

Tasks 1–7 and 9 are complete; Task 8's gate run and independent evidence review are recorded. Task 7's authorized retry failed three fresh reviews and stopped. After the owner waived its stop limit, the trailing-comma correction passed all required automated checks and a fresh independent review. Earlier failures and Computer Use limitations remain recorded below. Tasks 10–19 and Task 8's forward outline/Gallery checks remain incomplete; no push, pull request, acceptance or merge occurred.

## Builds and automated gates

- Main and parent remain at `28ab04a`; implementation branch is `feat/ux4-design-system` in `../Luminex-ux4`.
- Tasks 1–4 are committed: `da05479`, `873b41b`, `3f0eb3`, `ea5169c`. Each passed the release build, unit tests, format check, worktree-root checkers and applicable token freshness check.
- Task 1 retains a failed debug unit gate: MilkTruck baked-key hash was `dc545f78c9574809c6102cf9f411bdcde3cf5eff18c54c6d02d28bec5341129e`, expected `602b44cf9bffe5e549fae7979de90f667fed0db71c5fbe350ce04c5a4314a1d3`. The source pin matched. Release passed; the exact arithmetic cause remains unknown. No hash, tolerance or assertion changed.
- Task 5 implementation and its validation are committed together. Release build, unit tests, format, direct root checkers and token freshness pass. The original fresh review found no production code defect. Fresh retry review confirms the stated Task 5 gesture gate passes in the manually operated instrumented editor. Duplicate clean-build menu verification is additional evidence, not a separate requirement in that task.
- The evidence-only twenty-switch probe passed: all 21 samples retained font atlas ID 2, one platform texture and 384811008 allocated GPU bytes. The instrumentation patch was reversed from production. This does not verify real menu gestures.
- Task 8 fresh parent/head captures failed the six-scene exact gate (5/6) and eight-round exact gate (9/15); complete capture execution and full failure evidence are recorded below. GPU gates are pending Tasks 10 and 19.

## System settings and display

Original settings were observed with Computer Use: Appearance Auto and Reduce Motion off. Task 6 switched Appearance to Dark and back to Auto, and Reduce Motion on and back off. Both restored settings were observed. Original records exist in `system/` and `task-6/`; restored captures are listed below.

Both observed displays use 2× backing scale. No 1× gesture or typography capture has been verified. Parent release maximized client bounds were 1674 × 1052 points; windowed startup was 1280 × 720 points.

QA wrappers live only in the evidence directory and contain byte-identical executable copies; recorded hashes identify parent release, clean head and instrumented head. Task 5 runs exited normally. Task 6 clean head, motion probe and parent exited with code 0 after validation.

## Decisions

- Selection outline keeps the plan default: one encoded `#4CABFD` constant for both themes. The shader change belongs to Task 10 and has not run; the current outline remains amber. Task 8 must record this pending check and revisit it after Task 10.
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

At the initial stop, panel token replacements and xmake/CI wiring had no reported review defects; the literal-color checker remained unapproved and uncommitted. Attempt 1 missed explicitly typed declarations/returns and misattributed nested lambda returns. Attempt 2 fixed those examples but missed `if constexpr` color returns and falsely flagged templated non-color lambdas. Attempt 3 introduced delimiter-aware callable classification; all 16 tests pass, but independent review still found two P2 defects:

- A defaulted function template, `template<typename T = int> ImVec4 ink() { return {1,0,0,1}; }`, escapes checking because the header contains `=`.
- `operator[]` callable boundaries are missed, producing false positives on a local class’s integer-array returns and missing an explicitly typed ImVec4 operator’s numeric return.

The exact reproductions and logs remain in `task-7/`; execution stopped at that point and no gate was loosened. The initial C++ layout run also failed with 661 orphan test separators while compile-command regeneration overlapped it. A stable sequential rerun passed 2673 definitions / 352 files; its original cause remains unproven. Build, unit, format, root checks and token freshness passed before the checker reviews. Later partial checks do not override the failed review gate.

### Authorized Task 7 retry

The owner explicitly authorized another run. A fresh implementer added failing regression cases for defaulted templates and operator callable boundaries before fixing the checker. Additional test-first cases cover binary, hex-float and digit-separated numeric literals. Zero sentinels and data-derived colors remain allowed; nested non-color returns are not attributed to outer color functions. The final checker is `d168783c…`, with 23 passing checker tests. The retry changed only the checker and its tests; the earlier panel token replacements and policy wiring remain in this task's diff.

The final release build, unit tests, format check, seven root checkers, token freshness, literal-color scan and all 275 Python policy tests pass. Compile-command generation completed before either AST scan; layout passed 2673 definitions / 352 files. The first full Python invocation failed before discovery because `-t .` made `Tools/tests` non-importable. The established CI command without that option passed; no package files or tests were changed to bypass discovery. Both runs, failing regressions, exact reproductions and file hashes are retained in `task-7-retry/`.

The first fresh review of this authorized retry failed with two P2 findings: `ImVec4 empty{}, ink{1,0,0,1};` escapes checking after the first declarator, and a function-try-block's catch handler escapes its explicit color-return scope. Both reproduce through the checker API and CLI; the reviewed 13-file snapshot matches the gate hashes. Panel roles and policy wiring have no additional reported defect. Reproductions remain in `task-7-retry-review/`.

The correction adds test-first handling for multiple declarators and complete function-try scopes, with zero, data, array and nested non-color controls. Pre-review probes also found qualified/cv array element types, explicit array returns and operator-name arrows; shared type and ownership handling now covers those cases. A temporary Python syntax error was corrected and retained. Two partial gate runs were stopped with exit 130 before source edits, with no checker processes remaining; their logs are archived in `task-7-retry-fix1/` and do not count as passes.

The frozen corrected checker `146649a0…` passes all 35 checker tests and all 287 Python policy tests. All 17 final checks pass, including release build, unit, format, seven root checkers, token freshness and literal-color scan; layout remains 2673 definitions / 352 files. The before/after 13-file inventories match, and the 11 panel/wiring files are unchanged from the retry baseline. The checker is lexical: macro expansion, aliases, inferred types and complete C++ grammar are outside its claimed coverage.

Second fresh review failed with one P2 false positive: the array scan treats a nested integer-array argument in `std::array<ImVec4,1> colors{{sampleColor(std::array<int,4>{1,2,3,4})}};` as a color literal. Expected no diagnostic; the API reports one and CLI exits 1. Raw-array and array-return forms reproduce it. Previous findings now pass, and no additional panel/wiring defect was found. Evidence remains in `task-7-retry-review2/`. The test-first correction now walks actual anonymous color-element clauses and skips balanced named/call expressions. The frozen checker `44e72b81…` and tests `cc25c2f8…` pass 41 focused tests, all eight review-2 API/CLI probes and fourteen prior API probes. Parenthesized numeric arguments, copy-list constructors, explicit new color arrays and nested data were independently compiled against the pinned ImGui header. A suggested double-list scalar form proved invalid C++ and was removed from required controls. The active gate run was cleanly interrupted with exit 130 before that test edit; its logs are retained and are not passing evidence. All 17 corrected final checks pass, including the release build, unit tests, formatting, seven root checkers, token freshness and literal scan; layout remains 2673 definitions / 352 files. All 41 checker tests and 293 Python policy tests pass. The third fresh review failed with a P2 omission: valid `ImVec4 ink{1,0,0,1,};` compiles against the pinned ImGui header, but the API reports no diagnostic and the CLI exits 0 instead of rejecting it. Prior recorded findings now pass. The reviewer stopped probing after confirmation; fixture, syntax-check result and API/CLI evidence remain in `task-7-retry-review3/`. Execution stopped at retry failure three as required. No fourth correction, Task 7 commit or Task 8 gate ran.

The owner then explicitly waived the three-failure stop for Task 7 and authorized the remaining work. This preserves the failed review gate and authorizes a focused trailing-comma correction, fresh review and normal automated checks before a Task 7 commit. It does not waive gates or the stop rule for other tasks. A fresh implementer added three regression tests before changing optional trailing empty-channel handling in two checker lines. RED recorded 44 tests with 17 failures; GREEN passes all 44. All 52 new valid fixtures compile against the pinned ImGui header and match the expected API/CLI results. The eleven retained panel/wiring files are unchanged; evidence is in `task-7-override/`. The first global run passed configure, build, unit, format and compilation-database generation, then failed project policy because the controller's override note made the plan 301 lines. The note was compacted back to 300; no implementation changed and no AST scan had started. The failed run remains evidence. All 17 final automated checks now pass, including 44 checker tests, 296 Python policy tests and layout 2673 definitions / 352 files. The fresh review passes with no actionable findings within the stated lexical scope. All thirteen frozen implementation hashes match; forty-four tests, the current scan and ten independently compiled API/CLI fixtures pass. The fifty-two retained fixtures also verify. Task 7 was committed as `4d193e3`; Task 8 has recorded its automated and real-editor checks below.

### Task 8 current-head checkpoint

The current head is `4d193e3`. Original settings were recorded again before this checkpoint: Appearance Auto, currently resolving Dark, and Reduce Motion off. Task 6's earlier observations remain historical evidence. Task 8 repeats its required checks on the current clean head.

| Action | Expected | Observed | Status | Screenshot |
|---|---|---|---|---|
| System Settings > Appearance: read original | Record original appearance before changing it | Auto selected; current system chrome is Dark | passed | task-8/cua/system-appearance-original.png |
| System Settings > Accessibility > Motion: read original | Record original Reduce Motion before changing it | Reduce motion off | passed | task-8/cua/system-motion-original.png |
| Launch clean head windowed with derived schema-4 fixture | Restore docking, visibility and scale; use Auto and Comfortable defaults | 1280×720 client; Hierarchy left, Viewport center, Inspector/Rendering right, Performance/Console below; 100% scale and Dark chrome under system Auto | passed | task-8/cua/schema4-head-startup.png |
| Native Zoom after schema-4 restoration | Maximize usable bounds retaining docks | Maximized main editor; same left/center/right/bottom docking arrangement and 100% scale | passed | task-8/cua/schema4-head-maximized.png |
| View > Appearance after schema-4 migration | Auto selected without rebuilding default docks | Auto (system) is checked; current main chrome is Dark | passed | task-8/cua/schema4-appearance-auto.png |
| Head: Window > Render Graph under Auto/Dark | Detached graph has Dark content and native title bar | Graph opened with dark canvas, colored kinds/links and dark title bar | passed | task-8/cua/head-auto-dark-graph.png |
| Head: Window > Performance under Auto/Dark | Detached Performance has Dark content and native title bar | Live tables and summary opened with dark surfaces and title bar | passed | task-8/cua/head-auto-dark-performance.png |
| Parent: launch with the derived schema 4 fixture | Original layout and scale restore | Hierarchy, Inspector/Rendering, Viewport and Performance/Console dock regions restored at 100% | passed | task-8/cua/parent-dark-windowed.png |
| Parent: native Zoom | Dock regions resize without overlap | All dock regions expanded to usable display bounds; fields remain readable | passed | task-8/cua/parent-dark-maximized.png |
| Parent: open Render Graph under system Dark | Baseline graph opens | Baseline dark graph and native bar opened; original blue header/controls retained | passed | task-8/cua/parent-dark-graph.png |
| Parent: open Performance under system Dark | Baseline detached Live window opens | Dark Live tables, plots and native bar opened | passed | task-8/cua/parent-dark-performance.png |
| System Settings: choose Light | Light becomes selected | Light selected in Appearance preferences | passed | task-8/cua/system-light.png |
| Head Auto: system Light, existing Performance | Live content and native title bar become Light | Existing Live window became white/light gray with dark text; title bar became Light | passed | task-8/cua/head-auto-system-light-performance.png |
| Head Auto: system Light, existing Render Graph | Graph content and title bar become Light live | Canvas, cards, details and native title bar became Light without reopening | passed | task-8/cua/head-auto-system-light-graph.png |
| Head Auto: system Light, main editor | Main content and title bar become Light live | Main docks, fields, toolbar, plot and native title bar became Light with layout retained | passed | task-8/cua/head-auto-system-light-main.png |
| Parent: system Light, existing Performance | Baseline native bar follows system; content retains baseline palette | Native bar became Light while Live content stayed original Dark | passed | task-8/cua/parent-system-light-performance.png |
| Parent: system Light, existing Render Graph | Native bar follows system; baseline graph palette remains | Light native title bar; graph remained dark with original colors | passed | task-8/cua/parent-system-light-graph.png |
| Parent: system Light, main editor | Native bar follows system; baseline UI remains Dark | Light title bar; original Dark panels, blue fields and layout retained | passed | task-8/cua/parent-system-light-main.png |
| System Settings: choose Dark | Dark selected | Dark selected, system chrome dark | passed | task-8/cua/system-dark.png |
| Head Auto: system Dark, main editor | Main retints Dark live | Dark toolbar, docks, fields, plot and native bar restored without relaunch | passed | task-8/cua/head-auto-system-dark-main.png |
| Head Auto: system Dark, existing Render Graph | Graph returns to Dark live | Dark canvas, cards, details and native bar returned live | passed | task-8/cua/head-auto-system-dark-graph.png |
| Head Auto: system Dark, existing Performance | Performance returns to Dark live | Live tables, plot and native bar returned Dark without reopening | passed | task-8/cua/head-auto-system-dark-performance.png |
| Head: View > Appearance > Light under system Dark | Light editor and Aqua native bar override system Dark | Main surfaces/fields/plot Light, native title bar white under recorded Dark system | passed | task-8/cua/head-forced-light-system-dark-main.png |
| Head forced Light: system Light then Dark | Forced Light survives system changes | Main stayed Light with Aqua bar after macOS returned to Dark | passed | task-8/cua/head-forced-light-after-system-change.png |
| System Settings during forced Light: choose Light | System Light selected | Light selected | passed | task-8/cua/forced-light-system-light.png |
| System Settings during forced Light: choose Dark | System Dark selected | Dark selected | passed | task-8/cua/forced-light-system-dark-again.png |
| Focus existing Performance with forced Light while macOS is Dark | Performance content and native chrome remain Light | Existing Performance remained Light with a white native title bar under explicit macOS Dark | passed | task-8/cua/head-forced-light-system-dark-performance.png |
| Focus existing Render Graph with forced Light while macOS is Dark | Graph content and native chrome remain Light | Existing graph remained Light with Aqua chrome under macOS Dark | passed | task-8/cua/head-forced-light-system-dark-graph.png |
| Close Graph, then Window > Render Graph after forcing Light under macOS Dark | New graph uses Light palette and Aqua native chrome | Newly opened graph matched forced Light, including native title bar | passed | task-8/cua/head-forced-light-late-graph.png |
| Choose Dark with system Reduce Motion on | Dark target appears without a fade | Dark endpoint visible; clean screenshot cannot resolve the first transition frame (Task 6 instrumented snap retained) | endpoint passed; first-frame unverified in this run | task-8/cua/motion-on-head-dark.png |
| Choose Light with Reduce Motion on | Light target appears without a fade | Light endpoint visible; first-frame timing remains unverified in this clean run | endpoint passed; first-frame unverified in this run | task-8/cua/motion-on-head-light.png |
| Restore original Reduce Motion setting | Reduce Motion off as recorded before testing | System Settings shows Reduce motion off | passed | task-8/cua/system-motion-restored.png |
| Restore original system Appearance | Auto selected as recorded before testing | Auto selected; current system resolves Dark | passed | task-8/cua/system-appearance-restored.png |
| Turn Reduce Motion on in System Settings | Switch reads on for live appearance checks | Reduce motion reads on | passed | task-8/cua/system-motion-on.png |
| Select Local Light 9 and Console in Light; compare editor export | Inspect matching editor surfaces against the Light Figma export | Light surfaces visible; Inter, unrounded controls, missing provenance and native menus, stacked vector axes and fixture/layout differences recorded for later producers | captured; design completion pending | task-8/cua/figma-editor-light-task8.png |
| Use the same selected subject and Console in Dark; compare editor export | Inspect matching editor surfaces against the Dark Figma export | Dark surfaces visible with the same pending differences | captured; design completion pending | task-8/cua/figma-editor-dark-task8.png |
| Restore head View > Appearance > Auto | Auto follows restored system Dark | Main editor returned to the Dark system palette and chrome | passed | task-8/cua/head-auto-restored.png |

The derived fixture comes from the real parent schema 4 INI, changing only detached Performance and Render Graph visibility to closed. Its SHA-256 is `42e5554b…`. Startup restored its dock groups without a reset. Final head settings are schema 5, Auto, Comfortable and 100%; all dock assignments and node/parent topology match the fixture. Window geometry changed during native Zoom and the bottom selected tab changed through the recorded Console click. Detached visibility changed through the recorded opens. These are expected gesture effects, not migration loss. Both original INIs were restored byte for byte after both QA builds quit with exit 0. Evidence: `task-8/schema4-restoration.json` and original/final INIs.

The first evidence-only wrapper omitted Resources/Fonts and Resources/Icons, causing font fallbacks and an icon warning. No passes above use that launch. It was closed, the fixture restored, and bundle links corrected before clean verification (`qa-resource-correction.json`). Clean head and parent wrapper executable hashes match their release binaries (`qa-build-identities.json`); wrappers, screenshots and instrumentation are not committed.

The repeated atlas probe passed all 21 samples: atlas UniqueID 2, one platform texture and 384516096 Metal-allocated bytes stayed unchanged through twenty switches. Source, INI and App were restored exactly; clean App SHA-256 is `95919a44…`. Contrast/theme tests pass 1738 assertions in five cases. Evidence: `task-8/theme-resource-summary.json`, `phase1-before.json` and `phase1-restored.json`. Task 8 clean screenshots show the Reduce Motion endpoints only; exact first-frame snaps remain established by Task 6's identified instrumented run. No clean screenshot is claimed to measure a 160 ms transition.

Task 8 Figma comparison uses editor exports 01/02 and page 05's component vocabulary, with selected Local Light 9 and Console in both real themes. The captures are 2× at a different logical size from the exports; pixel equality is not claimed. Visible deviations:

- Inter remains in the running editor; Geist and its Medium/Mono roles belong to Task 9. Current subject headers and selected tabs use Regular.
- Controls remain square, tree lines/dirty marker and primary-action treatment are pending Task 10. Performance's collapsing definition header still uses selection blue; Task 10 owns the neutral helper.
- Native File/Edit/View/Window/Debug/Help menus are pending Task 16; commands currently occupy the in-window row. macOS Tahoe chrome also differs from the export's flat mock chrome.
- Provenance and activity marks shown on the export's root and subject are absent until Task 14. Authored data differs: the export has 41/41 subjects and an edited root; current clean Sponza has 24/24, a duplicate Sponza group and a Crytek source row. Light 9 values and console contents differ from mock data.
- The restored hierarchy/Inspector/bottom regions are about 219/359/210 logical points, compared with 244/380/300 in the export. XYZ fields stack vertically at this restored width; the export has a horizontal vector row. Position's unit label wraps. No digit overlap is observed in these editor captures; the required gallery type ramp remains Task 12.
- Existing labels still show “Colour” and “metres”; user-facing spelling must be converted to US English during the relevant panel/document tasks, preserving external names and identifiers.
- Style Gallery does not exist before Task 11; the page 05 runtime comparison remains unverified until Tasks 11/17. No Gallery capture or state is claimed passed here.
- The shader still contains amber `float3(1.0, 0.76, 0.38)` at line 62. The single encoded `#4CABFD` outline check is unverified until its explicit Task 10 producer; it must be revisited in both themes. This pending check is not waived.


All seventeen final automated checks pass: release build, unit tests, format, seven worktree-root checkers, token freshness, literal scan, focused checker tests and Python policy suite. Compile commands were generated before both AST scans; layout passed 2673 definitions / 352 files. The source inventories match before and after. Logs and exit codes are in `task-8/gates.json`, `version-before.json` and `version-after.json`. These checks do not override the failed exact-image gates.

### Task 8 exact-image results

Both exact-image gates failed. The fresh six-scene BMP comparison (`--temporal off --frames 1`, Metal validation enabled) passed 5/6; San Miguel differed at seven pixels, maximum channel delta 3. Parent SHA-256 `7f6090b855d0cee3cc0254fabfae6d915c2c6d4a5a95ee85c73ba373d5849a3f` differs from candidate `34a243172bdac46296ff7b24013f2f99945a65d0cc0b5948ed586a5b0394eb47`. Both captures exited 0. Full hashes and commands are in `task-8/six-scenes-summary.json`; metrics are in `san-miguel-initial-difference.json`.

The unchanged eight-round, fifteen-case driver completed 240/240 captures with exit 0 and sixteen complete reports; its exact parent-observed hash gate passed 9/15 and exited 1. The six failing cases are MaterialLab Native TAA at 1.0, MaterialLab MetalFX at 0.5/1.0, Sponza Native TAA at 0.5 and Sponza MetalFX at 0.5/1.0. Eight candidate captures have hashes absent from all eight parent rounds:

| Round | Case | Differing pixels vs same-round parent | Maximum channel delta |
|---|---|---:|---:|
| 3 | material-lab-taa-1 | 24 | 4 |
| 5 | sponza-vendor-1 | 233560 | 2 |
| 6 | sponza-taa-0.5 | 378 | 13 |
| 6 | material-lab-vendor-1 | 6807 | 1 |
| 7 | sponza-taa-0.5 | 2061 | 9 |
| 7 | sponza-vendor-1 | 234811 | 2 |
| 7 | material-lab-vendor-0.5 | 9887 | 1 |
| 8 | sponza-vendor-0.5 | 227905 | 8 |

Historical-reference matches were 115/120 parent and 112/120 candidate, counted separately. The unchanged parent also varied across rounds in five captures; that establishes parent variation without explaining the unseen candidate hashes. App, runtime shader, document and driver inventories stayed frozen throughout. Renderer, Engine, Scenes, shader source and scene documents have no changes from the parent. The seventeen differing generated Metal text files match after substituting only the worktree root name; that text comparison does not prove executable equivalence or establish a cause. No tolerance, reference, camera, lighting or renderer change was adopted, and no extra capture retry was performed. Evidence: `task-8/parity-rounds-1/summary.json`, per-round logs/images, `parity-failure-analysis.json` and before/after capture inventories. UX4.1's exact-image exit gate remains failed.

### Task 9 typography verification

Geist setup fetched and verified the pinned archive, three faces and OFL license into the head's physical directory; the parent retains Inter. Deliberately tampered Medium bytes failed naming `Geist-Medium.ttf`; restored setup reran with unchanged hashes and timestamps. Type-role tests were written first and failed on the missing API, then passed. All seventeen global checks pass on the frozen twenty-three-file implementation; focused/full Python totals remain 44/296. Evidence: `task-9/gates.json`, `source-freeze-verification.json` and setup logs. The first independent review failed: at 150%, selected Measure needs 86 pixels but receives 84 and ImGui reports clipping. The stronger allocated-width regression fails as expected; the original ContentWidth assertion missed it. The first correction refreshes retained tab width requests before layout and reserves the larger Regular/Medium label width without owning selection or changing inactive fonts. Both stronger probes fail before correction; afterward 560 direct clipping checks and width assertions pass across five resource scenarios, two contexts and twenty-eight frames, including scale transitions. All seventeen corrected global checks pass with frozen sources; fresh review passes with no actionable findings, independently verifying 1120 clipping checks across both densities and five resource scenarios. Evidence: `task-9-review/tab-width-regression.log` and `task-9-fix1/` RED/GREEN logs, patch, `gates.json` and `completion.json`.

The production-code dynamic-font probe passes complete resources and missing Medium, Mono, Regular and icons across two contexts, six roles and 75/100/150% scale. It verifies font-stack restoration, one warning per missing face, icons inside Mono scopes and selected/inactive tab widths. Earlier fixture/rounding errors and corrected legacy-icon/initial-tab issues are retained; an overwritten early log is explicitly a transcription, with the controller's captured-text copy retained. Evidence: `font-probe-results.json`, `font-probe-verified-*.log`, `probe-failures.txt` and `font-probe-first-failure-controller-read.txt` under `task-9/`.

Sans and Medium digits have the required 9.6 advance at body 16. Unmodified Mono digits measure 7.384615: the pinned loader scales a 600-unit glyph by 16 / 1300 ascent/descent units, rather than 1000 units per em (`mono-native-metrics.json`). Native Mono spacing stays as required; the design's equal-advance explanation is not observed. Body stays 16 pending Task 12's real Gallery captures. Dock tabs remain Regular; selected editor tabs use Medium. CPU probes establish no native-editor screenshot, legibility or GPU pass.

## Remaining verification

Task 8 global checks and independent evidence review pass with no actionable findings. Its exact-image gates have run and failed. Tasks 10–19, complete Figma comparisons, Style Gallery typography, density, actor/provenance marks, native menu and shortcut dispatch, the UX1 completion gestures in both themes and final GPU suite remain pending. ADR 0029 has not been created; when created it must remain Proposed until separate milestone review.
