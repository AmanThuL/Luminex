# UX4 independent review and fix validation

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

An independent reviewer examined the UX4 branch on 2026-10-01. Fourteen fix commits followed, each
reviewed, and the recorded gates were re-run on the resulting runtime. This page records the
findings, the fixes, how each fix was verified and what remains unverified. It changes no gate
result: ADR 0029 stays [Proposed](../../decisions/0029-design-system-token-contract.md) and the
historical failures stay recorded as failures for their runtimes. The owner's
[acceptance for integration](#owner-acceptance-and-integration) follows below. The earlier 11/15 matrix result and both earlier
resource failures are not replaced by the results below.

## Scope and identity

| Item | Value |
|---|---|
| Reviewed revision | `478a74b` (range `28ab04a..478a74b`, 20 commits) |
| Fix range | `478a74b..74848ba`, fourteen commits |
| Final revision | `74848baf2da254c05b938403b5d09fd32637ef46` on `feat/ux4-design-system` |
| Final frozen App SHA-256 | `a5bdf90df89159f19a9a81ad3255e2f2a3138dda88bc5e4029a5512f933a356a` |
| Final shader-map digest | `e2ade3586f1cb6f765469b70bab6e0de51c5661489ebb98b3d9d90fcb4ad552a` |
| Frozen parent App SHA-256 | `50ff38651a8af579d1e58b2acc4f82ca06414dd3eed379a0a739f58f31020906` (parent `28ab04a2d33a785545fa982cee7e7d05840205d1`) |

Four commit subjects were shortened after the gates ran to meet the 72-character rule. The gate
evidence names the earlier ids `e00b487` (final) and `a4c9369` (R001 to R003 build); their trees are
identical to `74848ba` and `514d33c` (final tree `d62fa4817fb5e477d014801fe01e4485a6150b6f`).

The digest is the SHA-256 of the sorted, compact JSON map of file name to SHA-256 for every file
in `Shaders/` beside the App. Compared with the earlier validated bundle (App `874867dd…`), 63 of
136 files differ: the App and 62 metallibs. The frozen bundle is the one the gates ran against.

Evidence prefix `E = ../Luminex-evidence/ux4/review-2026-10-01/`. Gate results are in
`E/gates/report.md` and `E/gates/rollup.json`; native review records are in `E/gestures.json`
with screenshots under `E/shots/`.

The review found Critical 0, Important 4 and Minor 15. Its verdict was not ready to merge without
fixes. Its inputs were the whole-branch review, a read-only
[native input investigation](#unverified-gestures-investigated) and the controller's fix brief.

## Findings and fixes

"Unit" means a test in `Tests/` that fails without the fix. "Code review" means the change was
read and an independent reviewer accepted it; no test or native run exercises it.

| ID | Observed defect | Cause | Fix | Verified |
|---|---|---|---|---|
| F1 | Generated Hierarchy row tooltip stated its provenance sentence twice (gesture 0088) | The tree-row tooltip and the mark's source both appended the sentence | `2a97b3f`: one pure tooltip function, the sentence appears once | Unit; only the tree row duplicated, the leaf row did not |
| F2 | Gallery checked Menu item drew `?` | U+2713 is in no loaded face | `5a20e36`: plain text plus `ImGui::RenderCheckMark`; other Gallery glyphs checked against the font tables | Code review; no native recheck |
| F3 | Proposal card advertised two changes and drew one diff | The fixture drew a single row | `5a20e36`: two labeled diff rows in Pending and Applied | Code review; no native recheck |
| F4 | Local-light Color (sRGB) showed no R, G, B letters in Light (0064, 0079, 0081) | The pinned ImGui hides the prefixes whenever color markers are drawn | `42164bd`: labeled R, G, B rows 0–1 with `%.3f` and a swatch picker; Rendering > Display Clear color uses the same helper | Native R002 PASS for Local Light 1 on an earlier build; Clear color by code review |
| F5 | Light viewport padding was white (0057, 0058, 0087) | The dock window used the default panel background | `85b42a2`: window background and image hairline use `surface/viewport` | Native R001 PASS on an earlier build |
| F6 | Native menu bar lost Close, Minimize, Zoom and Full Screen; Graph could not leave full screen (0062) | The native menu replaced SDL's default menu and cleared the windows menu | `3ee8c05`: Window submenu gains Minimize, Zoom, Enter Full Screen and Close; Cmd+W, Cmd+M and Ctrl+Cmd+F resolve in any focus | Unit for the model only; rows read in R001; no command executed |
| F7 | Cmd+S and Cmd+Shift+S dropped with no message while playing or measuring | The native path discarded a chord whose row was disabled | `ae69092`: the disabled reason is posted once per press; the decision moved into `MenuModel` | Unit; native notice UNVERIFIED |
| F8 | Native shortcut intents dropped with no trace (review I1) | Every drop point returned silently | `9bb45df`: a log line names the command and cause at each drop point | Unit for the outcome split; native UNVERIFIED |
| F9 | Cmd+Q refused during text entry, popup, mouse look or on a detached window (review I2) | Quit used the document focus policy | `ae69092`: Quit is exempt from the focus gates and still runs the unsaved-changes confirmation | Unit; native UNVERIFIED |
| M4 | Documents said slot names are verified at startup | Only the Render Graph canvas called the check | `550cad1`: ImGui table verified at shell creation; node-editor table still verified when the canvas first draws | Code review; 120-frame smoke launch |
| M11 | Theme token test passed silently without `ThirdParty/imgui` | Missing path skipped the assertion without a skip | `514d33c`: `skipTest` | Python suite |
| M12 | Literal-color checker did not reject framed tree nodes | It guarded `CollapsingHeader` only | `514d33c`: rejects `ImGuiTreeNodeFlags_Framed` outside `EditorStyle` | Python test, red before the change |
| M13 | One failed icon merge disabled icons everywhere | `fonts.icons` held the Medium merge result | `f7db043`: icons follow the Sans merge; one warning remains | Code review |
| I1 (round 1) | Unmet prerequisite (F with nothing selected) logged at a level the report called WARN | `Policy` outcome conflated focus refusals and unavailable commands | `a86d2d0`: `Unmet` and `OtherSurface` split | Unit |
| I2 (round 1) | Debug records never reached the Console | `LMX_LOG_DEBUG` mapped to a compiled-out macro | `0381f0e`: runtime logger call | Unit (`CoreTests`), red before the change |
| I3 (round 1) | Style Gallery was borderless and could not be closed | Its viewport kept `NoDecoration` | `a1ddcd1`: titled, resizable, closable native window | Code review; native UNVERIFIED |
| Round 2 | Plain typing in a field was recorded as a dropped shortcut and matched Console search | Text-entry refusals logged like other refusals | `74848ba`: unmodified keys refused for text entry leave no record; Command chords log at debug | Unit |

Facts the fix reports corrected: F1's duplication was in the tree row only; F4's cause is the
ImGui prefix rule, not the field width the brief first assumed; the first F8 report described
levels the code did not implement, which round 1 fixed. Behavior changes to know: Clear color
now reads 0–1 with three decimals instead of 0–255 integers and has no inline alpha number; each
color row is one line taller; the Dark viewport surround changes from `surface/panel` to
`surface/viewport` and loses its hairline; Hierarchy tooltip provenance lines sit at the end.

## Unverified gestures investigated

The [native follow-up](ux4-native-followup-validation.md) left seven gestures UNVERIFIED. The
investigation read code and retained screenshots only; it ran nothing, and it could not read
SDL's source (it rests on strings of the linked library and on passing gestures). In the
failing screenshots for 0049, 0067, 0073, 0079, 0080 and 0081 the window title is the dimmed
inactive gray, so single clicks were delivered to a window that was not key. A "no defect found"
verdict is a statement about the code. Each ledger row stays UNVERIFIED until a person repeats it.

| Gesture | Verdict | Stated limit |
|---|---|---|
| 0080 Cmd+S did not save | No defect found in the save path; likely automation, window not key. Separate likely defect: the chord dropped silently while playing or measuring (fixed as F7) | Not reproduced; the parent comparison is invalid because the parent's document chords answered to physical Control |
| 0067 Intensity edit | No defect found; field code equals the parent's apart from label marks | Not reproduced; likely automation |
| 0078 Dock splitters | No defect found; separator size and dock flags equal the parent's | Splitter hit-testing was not read line by line |
| 0062 Leave Graph full screen | Confirmed defect: the native menu lacked the standard window commands (fixed as F6). The two native timeouts: cannot tell | Not established why two native actions timed out |
| 0091 MaterialLab time after Play | No defect found in UX4; Play never advances a scene without animation tracks, Step does, and the parent behaves the same | The MaterialLab generator and Helmet import were not read in full |
| 0049 Strip Stop during Measure | No defect found; Stop cancels synchronously, and the run most likely finished first | Timing inferred from the ledger |
| 0073 Gear tooltip | No defect found; the tooltip is wired at the single draw site | Whether hover reaches a non-key window was not read |

## Native observations

An external input helper (CGEvent and System Events) drove R001 to R003 on an earlier fix-wave
build: App `d3db1a5d6034614d5e68a8b90f304b596833f3aa8823b7ff958d34406e8f90d4`, built at `514d33c`. It is not the final frozen runtime and predates
rounds 1 and 2. The session ended when the Mac's screen locked; the R003 keystrokes went to the
login window.

| ID | Context and action | Result | Limit |
|---|---|---|---|
| R001 | Forced Light windowed launch; read the native Window menu through accessibility | PASS: viewport padding is near-black around the image; the menu lists the model rows, then Minimize, Zoom, Enter Full Screen, Close, then AppKit rows | Rows read, not opened or selected; no window command executed |
| R002 | Click Hierarchy row Local Light 1 | PASS: three labeled R/G/B rows 1.000, 0.720, 0.450 with markers and a swatch below | Swatch picker not opened; no edit made |
| R003 | Double-click Intensity, Cmd+A, 50, Return | UNVERIFIED: Intensity stays 45.000 | The frontmost process was `loginwindow`; not a product observation |

## Gate revalidation

Run on the final frozen runtime with the recorded drivers; the screen was locked during the run
(`CGSSessionScreenIsLocked=Yes`, read after the windowed gates). Every windowed run logged all
frames presented and none skipped, so none was marked not run. Native and manual gates were out
of scope. `RojoRHITests/unit` is not among the recorded 17 checks and was not run.

| Gate | Previous result | New result |
|---|---|---|
| GPU group, Metal debug layer | PASS, 52.282 s and 54.208 s | PASS, 47.716 s, 1/1 group |
| Six static BMPs | PASS 6/6 | PASS 6/6, all equal the parent hashes |
| Eight-round matrix, parent-hash-set rule | FAIL 11/15 (restored bundle), FAIL 11/15 (publication bundle) | FAIL 14/15 |
| Schema 4 to 5 migration | PASS, output `3ed7eeae…` | PASS, same output `3ed7eeaeb4c4be4eb7ae76f59caff648effa1becd14f1de82fafefaaf2a16e4e` |
| UI index cost, frame 600, 1.15× | Comfortable PASS, Compact FAIL | Comfortable PASS 1.1056739717802462×, Compact FAIL 1.1561092764935454× |
| Resource, normal runtime | FAIL, 363806720 to 379420672 bytes | PASS, constant 313245696 bytes |
| Resource, Metal debug | FAIL, 363855872 to 378863616 bytes | PASS, constant 313245696 bytes |
| Token check and audit | PASS 48/48 | PASS 48/48 |
| Recorded commit checks | 17/17 exit 0 | 17/17 exit 0 |

**Matrix.** 240/240 captures and 16/16 reports completed; the driver exited 1. One case failed:
round 3, `material-lab-taa-1`, candidate hash
`cec02d6ea2abc966f8ac0f6ef3123d6120882bd0c6749df74e41ad1df0148cb7`, absent from the parent set,
which holds only `a6386adf9bb7b7ae9651bf7dca89b12cc881863d5db60230e705411bdab26646`. The other
seven rounds of that case produced `a6386adf…`. The parent produced two hashes for
`sponza-taa-0.5`, so that case passes under the rule. The previous restored-bundle run failed four
Sponza cases and the previous publication-bundle run failed `material-lab-taa-1`,
`sponza-taa-0.5`, `sponza-vendor-0.5` and `sponza-vendor-1`. The rule requires 15/15. No cause is
established, and the change in the count is not evidence of a fix.

**Cost.** Parent 9993 indices, limit 11491.95. Comfortable has 11049 indices in both themes.
Compact has 11553 in both themes and exceeds the limit. The indices are identical to the previous
run, and the fixes did not target this gate.

**Resource.** Both contexts sampled 21 times over 20 switches with atlas 2, textures 1 and scale
1.00, and exited 0 after 3000 presented, 0 skipped frames. Under the unchanged equality rule both
pass. The change from the earlier varying values is unexplained. The 3000 frames took about 26 s
here against about 41 s in the recorded normal run. The screen was locked, and whether that
affects the allocations (for example through the detached Render Graph window) was not measured.
The earlier failures remain recorded for their runtimes.

**Commit checks.** The 17 are configure, build, unit, format check, compile commands, project
policy, module dependencies, source headers, C++ comments, C++ layout, submodule pin, shader
imports, token freshness, literal colors, literal-color tests (46 OK), policy tests (312 OK) and
diff check. The App hash was unchanged across them.

**Restoration and deviations.** `E/gates/final-restoration-proof.json` records `proven: true`:
136 candidate and 127 parent files re-hash to their frozen inventories, both worktrees are clean,
no probe code remains and both `imgui.ini` files equal their backups. The head `imgui.ini` found
at the start (`03d4ca84027c532f0c28b7827ff414f67b4680220ef696d3294a689dc536cd09`) was not the
expected `1f1e1a27…e6134`; it was backed up and restored to those found bytes. No gate reads the
file, and the recorded original `1f1e1a27…e6134` was copied back afterwards. The cost driver's relink produced App `5dbab48c…` and the driver restored the frozen bytes.

## Remaining human checks

None of these has been performed. Every row is UNVERIFIED. Ids use the fix report's numbering.

| Area | Check (expected result) |
|---|---|
| F6 menu | Open Window with the main window key: model rows, separator, Minimize, Zoom, Enter Full Screen, Close, then AppKit's window list; five openings add no duplicate rows (rows 1, 2) |
| F6 full screen | Ctrl+Cmd+F leaves and re-enters Render Graph full screen once; Window > Exit Full Screen leaves it (3, 4) |
| F6 close | Cmd+W closes a detached Render Graph or Performance window and clears its Window check; the Gallery closes by Cmd+W, red button or Window > Close (5, 6, 6a) |
| F6 main window | Cmd+W quits when clean and asks Save, Discard or Cancel when dirty; Cmd+M minimizes and restoring resumes rendering (7, 8, 9) |
| F6 focus | In Hierarchy search, Cmd+M and Ctrl+Cmd+F still act and type nothing; a floating undecorated panel ignores Cmd+W (10, 11) |
| F7 | Play, then Cmd+S once: one notice "Stop playback before saving or reverting the scene." and no save; the same for Cmd+Shift+S; Stopped and dirty saves once; Cmd+O during Measure gives one notice |
| F8 | F with nothing selected writes one warning; typing `fc` or holding F in a field writes no row; Cmd+S in a field writes one debug line and holding adds none; Render Graph key writes debug lines ending `(other surface)` (rows 1 to 6) |
| F9 | Cmd+Q quits, after confirmation when dirty, from a focused text field, a detached Render Graph, mouse look and an open popup; Cancel then Cmd+Q shows one confirmation (a to e) |
| Round 1 and 2 | Gallery shows one native title bar; retest gesture 0080 (Cmd+S) with the window key |
| Fixed visuals | Reopen the Light Inspector swatch picker, Gallery checked item and two-diff proposal card natively |

## Deferred minor findings

- M6: the menu model is built twice per frame.
- M7: Hierarchy provenance work runs per row per frame and may need caching for a 4096-light LightLab.
- M8: the policy checker carries source-expression masks for the actor vocabulary.
- M9: Console severity chips are square where the spec gives chips a pill radius.
- M10: `vector3` component marker colors are hard-coded.
- M14: generated token files use a relative include and carry no "generated" banner.
- M15: the UI scale log line also appears on density-only changes.
- Round 1: the non-Apple polling route keeps the old Quit policy (unbuildable on this machine).
- Round 1: a Command chord refused on a detached window logs at debug, not warning.
- Round 2: "text field has focus" uses the broad text-input flag, so an unmodified key during any active widget is unrecorded.
- Round 1: AppKit's window list shows a detached Render Graph twice, and whether transient popup windows appear there needs a look.

## Unchanged status

The combined validation stays FAILED / INCOMPLETE: the publication-bundle matrix remains 11/15 and
the final-runtime matrix is 14/15, both below the 15/15 rule; Compact cost fails on both runtimes;
the earlier resource failures remain recorded. Every native completion action stays UNVERIFIED
except the scoped observations above. ADR 0029 stays `Proposed` and no tolerance changed. See the
[combined results](ux4-final-validation.md) and the
[native follow-up](ux4-native-followup-validation.md).

## Owner acceptance and integration

On 2026-10-01 the repository owner accepted UX4 for integration by squash merge, with its failed
and incomplete gates retained as measured. The milestone is Implemented and its executor plan is
closed. No tolerance, threshold or default changed, and ADR 0029 stays Proposed.

- **Image matrix.** The eight-round exact matrix fails: 11/15 on the earlier runtimes and 14/15 on
  the final runtime. No cause is established and no exception is approved.
- **Compact cost.** Both Compact draw-index cells fail at 1.1561092764935454× against the 1.15×
  limit; Comfortable passes.
- **Resource constants.** The earlier allocation failures stay recorded for their runtimes. The
  final runtime passed with an unexplained change, measured under a locked screen.
- **Native gate.** Incomplete. The follow-up's 101 scoped records hold 88 PASS, 12 UNVERIFIED and
  one OBSERVED. The review's native-menu, shortcut-logging and Gallery fixes are not exercised
  natively; their human checks are the rows under [Remaining human checks](#remaining-human-checks).

The plan is recoverable from the tag `ux4-integration-chain`.
