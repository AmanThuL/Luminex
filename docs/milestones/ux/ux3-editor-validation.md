# UX3 editor and icon validation

**Status**: In progress

This continues [UX3 validation](ux3-validation.md). Evidence paths are relative to
`../Luminex-evidence/ux3/`. Final common-head gates and owner acceptance remain pending.

## Session and document implementation (2026-09-28)

Coverage includes reviewed integration `c657ea0` and the two committed GUI repairs through `b07ddd6`. Paths use the existing evidence root. Historical image/orientation failures, Task 11's independent STEP-scale P2, Proposed ADR 0028 and owner-acceptance limits remain unchanged.

| Scope | Required commands | Evidence |
|---|---|---|
| Task 14 repaired | **11/11 PASS** | `task14-final-gates.json` |
| Task 15 | **11/11 PASS** | `task15-final-gates.json` |
| Task 16A repaired | **10/10 PASS** | `task16a-r1-integrated-gates.json` |
| Task 16B repaired | **10/10 PASS** | `task16b-r1-gates.json` |
| Task 17 repaired | **11/11 PASS** | `task17-r1-gates.json` |
| Combined 16B/17 before GUI repairs | **11/11 PASS** | `task17-integrated-gates.json` |
| Inspector visibility repair `b6c6aff` | **10/10 PASS** | `task17-gui-inspector-integrated-gates.json` |
| Save-before-Open repair `b07ddd6` | **10/10 PASS** | `task17-gui-save-before-open-integrated-gates.json` |

Ten commands cover build, full CPU suite, format, compile-database regeneration and six root checkers; the eleventh is full Metal validation. The two GUI repairs change App code/tests only and each has its own ten-command integration run; their evidence does not replace the final common-head gate.

Task 14 separates authored own flags from effective masks and freezes Measure population. Its P1 cancelled GPU measurement before sample zero while CPU passed. The approved Render declaration/shared-sampler repair publishes known population without fabricating retired classification results: **24-assertion runtime RED**, then **1,192 assertions / 62 cases** and **8/8 CPU/GPU CLI controls PASS**. Invalid-bounds and six missing-companion fixture failures remain retained; packaging was corrected (`task14/r14-1-*`).

Task 15 uses the approved fallible export API and immutable imported-pose baseline. Generated edits, animated previews and ordinary camera movement remain outside persistence; failed exact-orientation export stays an error. Tests pass **80,897 assertions / 26 cases** plus **376 / 25** using the corrected transport tag; the unmatched initial `[playback]` tag remains recorded. Khronos passes **65 documents (6 catalog + 59 outputs)** (`task15/controller-validator.log`).

Task 16's independent repair reviews pass. 16A fixes hidden-selection navigation, collapsed-root search and repeated generated-label scans (**335 assertions / 38 cases**). 16B resets imported own flags from the adopted document, preserving immutable bindings (**10-assertion RED**, then **727 / 62 PASS**). Original findings, API-compilation REDs and fixture/format corrections remain in their frozen reports/reviews.

Task 17 fixes selected paths dropped by queued Quit, alternate padded-buffer corruption and drawable-dependent work processing: **17 failed / 57 passed assertions RED**, then **84,502 / 104 PASS** (`task17/r1/`). The owned mailbox/pump runs before acquisition; missing-drawable tests are CPU simulation plus compiled/source-inspected Shell/main ordering, not a real failed swapchain.

Save As guards both outputs against active JSON and the actual decoded source-buffer identity, including normalized/symlink/hardlink equivalents. Provenance is validated and excluded from canonical dirty state. Normal regular-file Save remains available; the writer rejects symlink targets, and the corrected contrary test expectation stays recorded. Same-scene adoption preserves live IDs, own flags, imported-pose baseline and generated defaults; replacements are constructed before old-state invalidation.

Document/path/hash/library registration and reset-camera baselines adopt only after successful export, write, canonical reread/equality and hash. Failure adopts no metadata and retains dirty state; post-write read/hash failure can follow a completed disk save. Ordinary two-file rollback is **not crash atomicity**. Approved scope extensions cover sampler ownership, fallible export/baseline, library adoption and mailbox/pump/provenance; no RHI or tolerance change follows.

Initial Task 17 Khronos validation **failed with four accessor errors in two stale RED JSONs**: 32-byte declarations beside restored 36-byte buffers. The controller archived/hashed the fixture directory, removed only those two JSONs, regenerated GREEN fixtures (**606 / 6**) and passed **84 documents (6 + 78)**; integrated validation separately passes **83 (6 + 77)**. Preserve `task17/r1/controller-validator-initial.log`, `stale-red-cleanup.json`, regeneration/after-cleanup logs and `task17/controller-integrated-validator.log`.

The concurrent layout preflight's **781 orphan-separator diagnostics** remain failed; isolated probes found definitions, but the cause is unestablished. Its retry stopped for coordination (−15). Unchanged source later passed controller layout (**2,573 definitions / 342 files**), then integrated layout (**2,589 / 346**); retain `task17/r1/layout*`, `task17-r1-08.log` and `task17-integrated-08.log`.

Actual GUI work exposed Open's Save confirmation being cancelled with its chooser. Commit `b07ddd6` saves before opening the chooser and aborts Open on failure: **47-assertion runtime RED**, then **277 / 3** exact regressions and **1,304 / 25** focused PASS. Independent review reran **1,304 / 25 PASS**; all ten integration commands pass (`task17/gui-repairs/save-before-open/independent-review.md`, gate manifest above). The compact native sequence chose Save on dirty Open, checked saved disabled-helmet/enabled-Key flags while the chooser was open, then cancelled the chooser and retained a clean document (`task17/gui/compact-open-confirm.png`, `task17/gui/compact-save-open-cancel-clean.png`; `task17/gesture-record.json`, compact `dirty-marker`). The latter screenshot shows the original Key red value about 0.448: the later red 0.750 and EV 0.79 edits belong to a separate save/relaunch sequence. The original GUI failure remains evidence, not a passed historical run.

Commit `b6c6aff` fixes the Inspector warning by using the filtered document rows. Independent review passes with **338 assertions / 39 cases**, and all ten integration commands pass (`task17/gui-repairs/inspector/review.md`, gate manifest above). The native compact Key recheck shows the selected filtered light without the incorrect hidden-selection warning (`task17/gui/windowed-key-inspector-repair-pass.png`). These two additional repair commits deviate from the planned commit sequence; neither changes the approved interfaces, references, tolerances or RojoRHI.

## GUI provenance

GUI provenance is per launch. `task17/gui/windowed-save-fix-resume-launch.json` started at HEAD `b6c6aff` with the Save-before-Open working-tree repair and App SHA-256 `c578e8cb2816d22f348e1bc2eea03c64ed1c5395feef8f9cf5bd1b84104c0791`; its clean File Quit ended with exit 0. `task17/gui/compact-persistence-relaunch-launch.json` records committed `b07ddd6` with the identical App hash. The compact saved disabled helmet, Key red 0.750 and EV 0.79 survive that relaunch and native Open (`task17/gui/compact-saved-disk.json`, `task17/gui/compact-relaunch-{disabled-object,light,exposure}.png`). Its log reports matching workspace schema and restored docks. The evidence-only QA bundle contains the built Mach-O and staged Fonts so CUA can attach; this adapter is neither product packaging nor proof of bare-binary UI attachment.

## Both-size GUI results

The final `task17/gesture-record.json` records **62/74 agent-verified**, with **12 not verified**:
31/37 at maximized size and 31/37 at 1280×720 content size. The ledger combines earlier recorded
gestures with repair rechecks completed at `b07ddd6`; it does not claim all 74 ran at one head.
Maximized save/relaunch/schema/capture evidence includes `c657ea0`; compact Inspector and
Save-before-Open evidence includes earlier runs and the later repair rechecks. Per-launch
manifests identify binaries and working-tree patches. `task17/gui-finish-report.md` and
`task17/gui-final-independent-review.md` pass the independent evidence audit. There are 232 GUI PNGs,
with 157 unique PNG references in the ledger; all references exist. Final File > Quit exited 0.

Both sizes verified disabling an object, editing a light and exposure, Save, clean Quit/relaunch,
and finding all three; native Open, Revert, schema-4 workspace restoration, dirty markers,
confirmation before Open/switch/Revert/Quit/close, and Stop from clean and already-dirty states.
Active-path Save As and a read-only companion fail visibly. Hash audits preserve both files.
The compact dirty Save As attempt is bracketed by the retained read-only baseline and later
audit; the first clean attempt predates that baseline and has no separate pre-attempt hash.
Nonempty companion/alias failures remain automated writer evidence, not additional GUI claims.

| Incomplete gesture at both sizes | Observed portion and remaining reason |
|---|---|
| `sponza-search` | Source-node filter/pose work; `fabric` returns no row. The converted asset has one Crytek Sponza node with 25 material primitives, so legacy duplicate primitive-name search was not reproduced. |
| `san-miguel-search-pose` | Source-node selection and pose edit/reset work; the asset's single 281-primitive node cannot establish the former duplicate primitive-name task. |
| `graph-navigation` | Selection, group expansion and zoom persist across temporal-resource alternation; CUA could not exercise the required middle-button pan. |
| `focus-camera` | Text entry does not move the camera; held RMB plus simultaneous WASD/QE was unavailable in the native controls. |
| `dock-navigation` | Two panel-drag attempts left the layout unchanged; docking movement was not verified. |
| `dialog-pending-quit` | The native chooser consumed Cmd-Q, then delivered the selected document. Delivery of a queued application Quit/close was not established. |

The complete UX1 gate remains **unverified** because these twelve rows are incomplete. CPU
workflow/mailbox tests do not substitute for the missing native-dialog gesture. Generated
source-node search granularity is an observed behavior change, not proof of a filter defect.

Temporal mode/extents, all supported temporal/lighting/HZB legends, Performance operations and
graph freeze/dump/reopen were exercised. The no-local-light lighting views reported Final
fallback; these screenshots do not verify nonzero-light heatmaps. Maximized graph dump
`task17/gui/graph-dump-frame-400127.txt` is retained. Both sizes injected a capture destination
beneath a regular file: “Cannot replace capture document: Not a directory” remained until
dismissed, while controls/rendering continued. That is a passed recovery exercise. Capture
traces were produced in separate enabled runs; no Xcode replay is claimed. No timings from
these interactive sessions support a performance conclusion.

## Application icon

Task 18 is committed at `e74d4b2`. Both SVGs are byte-identical to the supplied FACET B2.2 sources;
`task18/artwork.json` records hashes. Artwork is **provisional; owner approval pending**. Native
AppKit rendering produced a 1024×1024 RGBA PNG with coloured facets, a dark rounded tile and
transparent corners; two renders were byte-identical. The first ImageMagick conversion returned
success but lost gradient colours and was visually rejected. Its image and log remain retained.

The initial Objective-C++ build failed on explicit release under ARC; removing that release
fixed it. The reviewed implementation stages Icons beside App and applies the PNG only during
windowed startup. Missing-PNG windowed startup exited 0 with exactly one warning; present PNG
exited 0 with none; headless capture with missing PNG exited 0 with none. Metal validation and
the shared GPU lock were enabled, and the staged PNG was restored. Worker and integration each
pass **10/10 required checks** (`task18/final-gates.json`, `task18-integrated-gates.json`).
Independent code and visual-evidence reviews pass (`task18/independent-review.md`,
`task18/visual-evidence-review.md`).

Fresh integration launch used App SHA-256
`1b2e655d1b696d129eaa7132fb30beed27fe1410c3adbc0d5ce502ef47ed481a`, built at `b07ddd6` plus the
reviewed seven-file icon patch subsequently committed as `e74d4b2`. The bare binary ran, but CUA
could not identify it. The evidence-only QA adapter copied that same Mach-O and staged Icons;
it is not product packaging. `task18/fresh-launch.png` records the editor and Cmd-Q exited 0.

**Dock and switcher appearance are not verified.** Two CUA Dock-selection attempts timed out
with `timeoutReached (-10005)`. The supported Cmd-Tab gesture released the modifier before
observation, leaving only an app-window screenshot. Three evidence-backed attempts were retained
in `task18/visual-verification.md`; they neither prove an icon defect nor pass the visual gate.
No owner approval is implied by source, staging or warning tests.
