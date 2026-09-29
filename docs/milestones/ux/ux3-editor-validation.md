# UX3 editor and icon validation

**Status**: Implemented — owner authorized integration on 2026-09-29; image gates failed as measured

This continues [UX3 validation](ux3-validation.md). Evidence paths are relative to
`../Luminex-evidence/ux3/`. The [final validation](ux3-final-validation.md) records the automated
results of the integrated run; this page keeps the editor-side gate results, the native-gesture
ledger and the icon checks.

## Session and document implementation (2026-09-28)

| Scope | Required commands | Evidence |
|---|---|---|
| Enabled state and measurement population | **11/11 PASS** | `task14-final-gates.json` |
| Session document state and export | **11/11 PASS** | `task15-final-gates.json` |
| Hierarchy tree | **10/10 PASS** | `task16a-r1-integrated-gates.json` |
| Environment and Inspector | **10/10 PASS** | `task16b-r1-gates.json` |
| Open, Save, Save As, Revert workflow | **11/11 PASS** | `task17-r1-gates.json` |
| Combined Environment and workflow | **11/11 PASS** | `task17-integrated-gates.json` |
| Inspector visibility repair | **10/10 PASS** | `task17-gui-inspector-integrated-gates.json` |
| Save-before-Open repair | **10/10 PASS** | `task17-gui-save-before-open-integrated-gates.json` |

Ten commands cover build, the full CPU suite, format, compile-database regeneration and six root
checkers; the eleventh is full Metal validation.

**Enabled state.** Authored own flags are separate from effective masks, and Measure freezes its
starting population. A GPU measurement had been cancelled before sample zero while the CPU path
passed; the Render declaration and shared sampler now publish the known population without
fabricating retired classification results: a 24-assertion runtime failure became 1,192
assertions / 62 cases, and 8/8 CPU/GPU CLI controls pass (`task14/r14-1-*`).

**Export.** The fallible export API uses an immutable imported-pose baseline. Generated edits,
animated previews and ordinary camera movement stay outside persistence. Tests pass 80,897
assertions / 26 cases plus 376 / 25 under the transport tag; Khronos passes 65 documents (6
catalog + 59 outputs) (`task15/controller-validator.log`).

**Hierarchy and Environment.** Hidden-selection navigation, collapsed-root search and repeated
generated-label scans are fixed (335 assertions / 38 cases). Resetting imported own flags uses the
adopted document and keeps immutable bindings (10-assertion failure, then 727 / 62 pass).

**Workflow.** Fixes cover selected paths dropped by a queued Quit, alternate padded-buffer
corruption and drawable-dependent work processing: 17 failed / 57 passed assertions, then 84,502 /
104 pass (`task17/r1/`). The owned mailbox and pump run before drawable acquisition; the
missing-drawable tests are CPU simulation plus compiled and source-inspected Shell/main ordering,
not a real failed swapchain.

Save As guards both outputs against the active JSON and the decoded source-buffer identity,
including normalized, symlink and hardlink equivalents. Provenance is validated and excluded from
canonical dirty state; the writer rejects symlink targets. Same-scene adoption preserves live IDs,
own flags, the imported-pose baseline and generated defaults, and replacements are built before the
old state is invalidated. Path, hash, library and reset-camera baselines adopt only after export,
write, canonical reread/equality and hash all succeed; a failure adopts nothing and stays dirty.
A post-write read or hash failure can follow a completed disk save. Two-file rollback is not crash
atomic.

Initial Khronos validation of the workflow fixtures failed with four accessor errors in two stale
failure-case JSONs (32-byte declarations beside restored 36-byte buffers). The fixture directory
was archived and hashed, only those two files were removed and regenerated (606 / 6), and 84
documents (6 + 78) pass; the integrated run passes 83 (6 + 77). See
`task17/r1/controller-validator-initial.log`, `stale-red-cleanup.json` and
`task17/controller-integrated-validator.log`.

A concurrent layout check reported 781 orphan-separator diagnostics whose cause is unestablished;
the same source later passed layout at 2,573 definitions / 342 files, and the integrated tree at
2,589 / 346 (`task17/r1/layout*`, `task17-integrated-08.log`).

Two repairs came from native use. Open's Save confirmation was cancelled together with its chooser;
Save now completes before the chooser opens and a failed Save aborts Open (47-assertion failure,
then 277 / 3 and 1,304 / 25 pass, `task17/gui-repairs/save-before-open/`). The Inspector showed a
false hidden-selection warning; it now tests the filtered document rows (338 assertions / 39 cases,
`task17/gui-repairs/inspector/review.md`). The native compact run saved on a dirty Open, checked
the saved flags while the chooser was open, then cancelled the chooser and kept a clean document
(`task17/gui/compact-open-confirm.png`, `compact-save-open-cancel-clean.png`).

## GUI provenance

Provenance is recorded per launch. `task17/gui/windowed-save-fix-resume-launch.json` and
`compact-persistence-relaunch-launch.json` name the binary (App SHA-256
`c578e8cb2816d22f348e1bc2eea03c64ed1c5395feef8f9cf5bd1b84104c0791`) and end with a clean File >
Quit, exit 0. The compact session saved a disabled helmet, Key red 0.750 and EV 0.79, which
survived relaunch and native Open (`task17/gui/compact-saved-disk.json`,
`compact-relaunch-{disabled-object,light,exposure}.png`); its log reports a matching workspace
schema and restored docks. The QA bundle holds the built Mach-O and staged Fonts so native
automation can attach. It is an adapter, not product packaging, and does not show that the bare
binary attaches.

## Both-size GUI results

`task17/gesture-record.json` records **62/74 verified** gestures and **12 not verified**: 31/37 at
maximized size and 31/37 at 1280×720 content size. The ledger combines recorded gestures with
rechecks after the two repairs; it does not claim all 74 ran on one build. Per-launch manifests
identify binaries and working-tree patches. The ledger cites 157 unique PNGs of 232 captured, all
present (`task17/gui-finish-report.md`, `gui-final-independent-review.md`).

Both sizes verified disabling an object, editing a light and exposure, Save, clean Quit and
relaunch, and finding all three; native Open, Revert, schema-4 workspace restoration, dirty
markers, confirmation before Open/switch/Revert/Quit/close, and Stop from clean and dirty states.
Save As onto the active path and onto a read-only companion fail visibly, and hash audits preserve
both files. The compact dirty Save As attempt sits between the retained read-only baseline and a
later audit; the first clean attempt predates that baseline and has no pre-attempt hash.
Nonempty-companion and alias failures are automated writer evidence, not GUI claims.

| Incomplete gesture at both sizes | Observed portion and remaining reason |
|---|---|
| `sponza-search` | Source-node filter and pose work; `fabric` returns no row. The converted asset has one Crytek Sponza node with 25 material primitives, so the former duplicate primitive-name search was not reproduced. |
| `san-miguel-search-pose` | Source-node selection and pose edit/reset work; the asset's single 281-primitive node cannot reproduce the former duplicate primitive-name task. |
| `graph-navigation` | Selection, group expansion and zoom persist across temporal-resource alternation; the required middle-button pan could not be driven. |
| `focus-camera` | Text entry does not move the camera; held RMB with simultaneous WASD/QE was unavailable. |
| `dock-navigation` | Two panel-drag attempts left the layout unchanged; docking movement was not verified. |
| `dialog-pending-quit` | The native chooser consumed Cmd-Q, then delivered the selected document; delivery of a queued Quit or close was not established. |

The complete UX1 gate stays unverified because these twelve rows are incomplete, and CPU
workflow and mailbox tests do not substitute for the missing native-dialog gesture. The coarser
source-node search granularity is an observed behavior change, not a filter defect.

Temporal mode and extents, every supported temporal, lighting and HZB legend, Performance
operations and graph freeze, dump and reopen were exercised. The no-local-light lighting views
report the Final fallback, so nonzero-light heatmaps are not verified. The maximized graph dump is
`task17/gui/graph-dump-frame-400127.txt`. At both sizes a capture destination beneath a regular
file produced "Cannot replace capture document: Not a directory", which stayed until dismissed
while controls and rendering continued. Capture traces came from separate enabled runs; no Xcode
replay is claimed, and no interactive timing supports a performance conclusion.

## Application icon

Both SVGs are byte-identical to the supplied FACET B2.2 sources (`task18/artwork.json` records
hashes). The artwork is provisional. Native AppKit rendering produced a 1024×1024 RGBA PNG with
coloured facets, a dark rounded tile and transparent corners, byte-identical across two renders.
A first ImageMagick conversion lost the gradient colours and was rejected; its image and log are
retained.

The Objective-C++ implementation stages Icons beside App and applies the PNG only during windowed
startup; an explicit `release` call failed to build under ARC and was removed. With the PNG
missing, windowed startup exits 0 with exactly one warning; with it present, exit 0 with none;
headless capture with it missing, exit 0 with none. Metal validation and the shared GPU lock were
on. The implementation and integration each pass 10/10 required checks (`task18/final-gates.json`,
`task18-integrated-gates.json`), and code and visual-evidence reviews pass
(`task18/independent-review.md`, `task18/visual-evidence-review.md`).

A fresh launch of App (SHA-256 `1b2e655d1b696d129eaa7132fb30beed27fe1410c3adbc0d5ce502ef47ed481a`)
showed the editor and exited 0 on Cmd-Q (`task18/fresh-launch.png`).

**Dock and switcher appearance are not verified.** Two Dock-selection attempts timed out with
`timeoutReached (-10005)`, and the Cmd-Tab attempt released the modifier before observation,
leaving only an app-window screenshot (`task18/visual-verification.md`). These neither show an
icon defect nor pass the visual gate. Source, staging and warning tests do not approve the artwork.
