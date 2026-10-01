# UX4 combined final validation

**Status**: In progress

The Task 19 result remains **FAILED / INCOMPLETE**. The latest publication bundle passes
six static BMPs (6/6) and the Metal-debug GPU group, while its eight-round matrix passes 11/15 and fails the exact gate.
Both Compact cost cells fail the unchanged 1.15× limit; both resource attempts fail constant
allocated bytes. Every current native completion action remains **UNVERIFIED**.
Run completion is not gate acceptance. ADR 0029 remains [Proposed](../../decisions/0029-design-system-token-contract.md);
the [milestone](ux4.md) and [retained plan](../../plans/2026-09-30-ux4-design-system.md) remain In progress.

## Frozen identity and evidence

The controller completed and restored the combined gates after the documentation commit
`e8625de051d2d2d61f6fedac5442eb33560dcef3`. The parent, local main and origin/main were
`28ab04a2d33a785545fa982cee7e7d05840205d1` at the recorded refresh. Source inventories cover
972 unchanged files before documentation edits. This audit changes documentation only.

| Identity | SHA-256 |
|---|---|
| Final clean head App | `874867dd4156eead419a04881216473a799de8da780467b99a584812d88f05f7` |
| Frozen parent App | `50ff38651a8af579d1e58b2acc4f82ca06414dd3eed379a0a739f58f31020906` |
| Earlier combined-run head App | `593c6ca8a7043b0c1cea69e8722b3f5b22e3ab99f3c6df39ed7a2efca003d29f` |
| Unchanged matrix reference | `b37bb9b89919b35a354c99945e0603547d6240a260823f707862b6296d2fe20e` |

Raw evidence root `E = ../Luminex-evidence/ux4/task-19/` contains
`controller-notes.txt`,
`combined-gate-rollup.json`,
`restored-production-identity.json`,
source/doc/runtime shader/driver hashes, exact commands, logs and before/after inventories.

The first combined run used `593c6ca8…`. Removing the resource observer and rebuilding produced
`874867dd…`; a configuration clear/full rebuild reproduced it. Source hashes and symbol sets
matched, but two loaded stub sections differed, with 240 differing bytes outside UUID/signature.
Byte equivalence is not established. The controller reran images, GPU and cost for the new
identity, retaining every earlier outcome. Later relinking can alternate these App hashes without
source changes. The actual clean-produced `874867dd…` backup was restored exactly after probes;
publication checks must retain the App and validated shader inventory or revalidate affected gates.
See `E/production-refresh/{retest-reason.md,macho-comparison.json}`.

## Publication runtime follow-up

The fresh review found 62 current candidate metallibs differing from the restored-bundle
capture inventory; all generated MSL and parent shaders matched. The loader prefers metallibs,
so App equality cannot establish runtime equality. The first point or cause of drift is unknown.
The controller reran affected images and GPU on the current bundle; the earlier run remains below.
Full inventories and raw results are in `E/publication-runtime/`; no byte equivalence is claimed.

The current candidate shader-map digest is
`5d13292d123647ff2b98093d454e66ebf8140b367f4dd2df344303b3c4f2d78c` (SHA-256 of sorted compact JSON).
App remains `874867dd…`. GPU passes with Metal validation, exit 0 in 54.208 s; six static BMPs
pass 6/6. All 240 matrix captures complete under the unchanged eight-parent-set rule, but
Only 11/15 cases pass. Capture before/after Apps, shaders, documents and drivers match exactly.

| Current bundle / scene | Off | Native TAA 0.5 | Native TAA 1.0 | MetalFX 0.5 | MetalFX 1.0 |
|---|---|---|---|---|---|
| MaterialLab | PASS | PASS | FAIL | PASS | PASS |
| TemporalLab | PASS | PASS | PASS | PASS | PASS |
| Sponza | PASS | FAIL | PASS | FAIL | FAIL |

| Round | Current-bundle unseen capture | SHA-256 absent from parent set |
|---|---|---|
| 1 | `material-lab-taa-1` | `1bb688b1dbd9ad7ef620d287ac7f1c8802c4fde5e4dc3e40629dd591449705db` |
| 4 | `sponza-vendor-1` | `c8e2804f62417a3e92afe593d20135443ca8220cd5884387670db6d571b9d932` |
| 5 | `sponza-taa-0.5` | `683cc0367e164595cf180434bc4db4c3c6bb939bb5186fd0371db8911513c05a` |
| 6 | `sponza-vendor-0.5` | `ae7397fc1beabdbe8cbf376d0124225b34efb0649e5b99fdb4ec429ddb354e0d` |
| 7 | `material-lab-taa-1` | `6a6fdd959694ab232446f99d9f5af7037e50bd4e269d60acc2e640595f0a84b0` |

`publication-rollup.json` retains every hash and raw result. This repeat addresses publication
identity, not an image fix or accepted exception; neither result replaces the other. Cost and
resource observations remain tied to their recorded instrumented Apps and contexts; no third
resource attempt occurred. The final checker records the complete post-build runtime inventory
and restores the exact separately validated App/shader backup before publication. Restoration
must match every file, rather than assume a regenerated library is equivalent. Native checks
remain UNVERIFIED, and the combined gate remains FAILED / INCOMPLETE.

## Complete gate table

This table retains the earlier restored-bundle run. The publication image/GPU follow-up above
records the later runtime bundle separately; cost/resource outcomes remain failed as measured.

| Required gate / scope | Execution | Result and limit | Raw record in E |
|---|---|---|---|
| Task 8 token freshness and generated contrast audit | Complete | PASS, 48/48 specified pairs; no whole-screen certification | `contrast-audit-result.json`, `contrast-audit.log` |
| Task 8 independent contrast/theme CPU tests | Controller final unit-check phase | New results belong to the final 17-check record; generator audit is separate | [phase ownership](#controller-final-check-and-publication-phase) |
| Task 8 six scenes, temporal off, one frame | Complete | PASS 6/6 exact BMP hashes on final App | `captures-restored/six-scenes-summary.json` |
| Task 8 eight alternating rounds, fifteen cases | Complete, 240/240 captures, 16 complete reports | FAIL 11/15; five candidate captures outside parent sets | `captures-restored/parity-rounds-1/summary.json` |
| Tasks 5/8 twenty-switch resource check | Complete twice, 21 samples each | FAIL in normal and Debug contexts; atlas/texture stable, allocation varies | `atlas-normal/result.json`, `atlas/result.json` |
| Task 8 schema 4 migration | Complete, four-frame automated App run | PASS dock/settings identity only; native migration/readability UNVERIFIED | `schema4/result.json` |
| Task 8 system/forced appearance, motion, Figma/outline | Native action list retained | UNVERIFIED on final head; historical passes remain revision-specific | [current native actions](ux4-final-native-validation.md#appearance-and-restyle-actions) |
| Task 12 UI index cost, frame 600 | Complete, parent plus four head cells | Comfortable PASS; Compact FAIL in both themes, ≤1.15× unchanged | `metrics-restored/{results.json,gate.json}` |
| Task 12 UX1 completion, typography/layout | All required contexts inventoried | UNVERIFIED on final head; historical 152 gestures retained | [current contexts](ux4-final-native-validation.md#completion-contexts) |
| Task 14 marks, sources, activities and status retention | Checklist retained | UNVERIFIED; CPU placements do not prove tooltips or native pixels | [marks checklist](ux4-marks-validation.md) |
| Task 16 native menus, shortcuts and focused editing | 127 action rows retained | UNVERIFIED in all four head contexts and parent comparison | [native-menu checklist](ux4-native-menu-validation.md) |
| Task 17 Gallery/Figma and type ramp | 21 × 4 = 84 native component cells retained | UNVERIFIED; no final captures or actual 1× display | [Gallery checklist](ux4-gallery-validation.md#per-component-native-checkpoint) |
| Task 17 current-source state probe | Complete after restoration | PASS 64/64 CPU text/state checks; no native pixels or Geist ink proof | `gallery-cpu/final-restored-green/{commands.json,probe.log}` |
| Task 19 GPU group, Metal validation | Complete, exit 0, 52.282 s | PASS with `MTL_DEBUG_LAYER=1` | `restored-gpu/{gpu-result.json,gpu.log}` |
| Fresh review, final 17 checks, commit and publication | Controller final-check phase | Frozen results accompany publication; no acceptance or merge | [phase ownership](#controller-final-check-and-publication-phase) |

GPU command: `MTL_DEBUG_LAYER=1 rtk proxy xmake test -P . Tests/gpu` from the worktree root.
This is a passing GPU test group, not a native capture gesture, Xcode replay or missing workflow pass.

## Recorded restored-bundle image results

<a id="exact-image-results"></a>

The six fixed 1280 × 720 BMPs use `--temporal off --frames 1`, the corresponding checked-in
scene documents and Metal validation: Sponza, MaterialLab, TemporalLab, San Miguel,
VisibilityLab and LightLab all match. Full paired hashes/commands are in the six-scene summary.

The unchanged `parity_rounds.py` alternates parent/candidate order over eight rounds. Each case
requires every candidate hash to occur in that case's eight parent rounds. The reference uses
1280 × 720, 32 frames and BMP output; these are not PNG comparisons. `inspect_image` checks
`BM`, and the retained filenames end in `.bmp`. External notes calling this matrix PNG are mislabeled.
The complete driver exits 1 because the exact gate fails; individual capture exits are 0.

| Scene | Off | Native TAA 0.5 | Native TAA 1.0 | MetalFX 0.5 | MetalFX 1.0 |
|---|---|---|---|---|---|
| MaterialLab | PASS | PASS | PASS | PASS | PASS |
| TemporalLab | PASS | PASS | PASS | PASS | PASS |
| Sponza | PASS | FAIL | FAIL | FAIL | FAIL |

All five unseen candidate captures are retained below; none is excused by a matching later round.

| Round | Failed case | Candidate SHA-256 absent from parent set |
|---|---|---|
| 2 | `sponza-vendor-0.5` | `c65a32a8bc13c63ff6e471711354bf4bd9bd2520aa49caa5381ceb575a0683a5` |
| 3 | `sponza-taa-1` | `39b2ad030fa47f034a881377e5387e41770c2793d9a8a429b52828e9359bcf1b` |
| 5 | `sponza-vendor-1` | `232291bcfae5a05c620d3f3b1e14618049a1d6e2eac9766b0c436abd35370e93` |
| 5 | `sponza-vendor-0.5` | `baa68e56504a12774c42774f173d04a08bac717859f653112dccc5f7c9d4289a` |
| 8 | `sponza-taa-0.5` | `769bb4a62c8d111556d2aa7ef23e5eadd336b08a5bf2790c208563a4700d3744` |

Historical-reference matches are separately 119/120 parent and 115/120 head. Parent TAA 1.0
has two observed hashes; that variation does not explain or pass unseen candidate hashes.
App, runtime shaders, documents, reference and driver inventories match before/after capture.
No image tolerance, camera, rig, frozen input or gate algorithm changed; no cause is established.

The earlier `593c6ca8…` run remains 5/6 static BMP and 12/15 matrix, with failures
`material-lab-taa-0.5`, `material-lab-taa-1`, `sponza-vendor-1`. Its San Miguel pair differs at
seven pixels, max channel delta 3. `E/captures/` and `image-rollup.json` retain commands,
hashes and diagnostics. These are separate identity results, not a best-outcome selection.
Historical Task 8 remains [5/6 BMP and 9/15 matrix](ux4-history-validation.md#task-8-exact-image-results),
including all eight failure rows. No continuation exception turns any image gate into PASS.

## UI cost

The restored-head protocol applies the same original rectangle `(54, 65, 1674, 1052)` to
parent/head at frame 2, then resets default docks at frame 10 after geometry settles. Default
Sponza, Console selected, 100% UI scale, 2× backing and 3348 × 2104 framebuffer pixels match.
Frame 600 reads `io.MetricsRenderIndices`; all five runs log 600 presented, zero skipped.
Comfortable starts with empty INI; Compact starts with only schema 5, scale 100 and density.
The observer and geometry/reset hooks are identical on parent/head. Compact work-area height
reflects its toolbar metrics; the original window/display/framebuffer rectangle remains fixed.

| Cell | Indices | Ratio to parent 9993 | Unchanged limit 11491.95 | Result |
|---|---:|---:|---:|---|
| Dark Comfortable | 11049 | 1.1056739717802462 | ≤1.15× | PASS |
| Light Comfortable | 11049 | 1.1056739717802462 | ≤1.15× | PASS |
| Dark Compact | 11553 | 1.1561092764935454 | ≤1.15× | FAIL |
| Light Compact | 11553 | 1.1561092764935454 | ≤1.15× | FAIL |

Control radius remains 0 after the historical 3 → 2 → 0 sequence. No threshold loosening,
new optimization or third resource retry occurred. Cost is draw-index count, not CPU/GPU time.
The measurements use instrumented Apps, parent `1a22d55f…` and head `b073c870…`, backed by
unchanged production source and exact backup restoration. `metrics-restored/results.json`
retains complete geometry, INI/binary hashes, commands and ratios. The first combined metrics
remain under `metrics/`; historical Task 12 Compact remains [1.169919× FAIL](ux4-validation.md#task-12-cost-gate-failure).

## Resource samples and restoration

Two head-only runs follow the Task 5 exception for twenty automated theme switches. Graph opens
and freezes at frame 200; text is prewarmed; samples begin at frame 600, then every 120 frames
through 3000. Each sample precedes the next `setAppearance` change, totaling twenty switches
and 21 observations. Normal runtime removes the additional Metal Debug/Capture environment
flags; Debug is retained separately. Both use instrumented App `c1268dc3…`. Atlas UniqueID 2,
one platform texture and scale 1.0 stay stable in every sample; allocated bytes do not.

| Switch | Frame | Debug allocated bytes | Normal allocated bytes |
|---|---:|---:|---:|
| 0 | 600 | 363855872 | 363806720 |
| 1 | 720 | 363855872 | 363806720 |
| 2 | 840 | 363855872 | 363806720 |
| 3 | 960 | 363855872 | 378814464 |
| 4 | 1080 | 363855872 | 363806720 |
| 5 | 1200 | 378863616 | 378814464 |
| 6 | 1320 | 363855872 | 363806720 |
| 7 | 1440 | 363855872 | 378814464 |
| 8 | 1560 | 378863616 | 363806720 |
| 9 | 1680 | 363855872 | 378814464 |
| 10 | 1800 | 378863616 | 363806720 |
| 11 | 1920 | 363855872 | 378814464 |
| 12 | 2040 | 378863616 | 363806720 |
| 13 | 2160 | 363855872 | 379011072 |
| 14 | 2280 | 378863616 | 364003328 |
| 15 | 2400 | 363855872 | 379420672 |
| 16 | 2520 | 363855872 | 364412928 |
| 17 | 2640 | 378863616 | 379420672 |
| 18 | 2760 | 363855872 | 364412928 |
| 19 | 2880 | 378863616 | 379420672 |
| 20 | 3000 | 363855872 | 364412928 |

Both runs exit 0 but fail the unchanged equality rule. Debug spans 363855872 to 378863616 bytes;
normal spans 363806720 to 379420672. Allocation variation has no established cause and cannot be
attributed to the validation layer or themes alone. The stable atlas/texture subset is not a
resource-gate PASS. These observations establish no actual menu switch, fade or native screenshot.

Each run retains external patch/helper, protocol, instrumented build, app log and restoration proof.
`atlas/restoration-proof.json` restores its original `593c6ca8…`; `atlas-normal/restoration-proof.json`
restores `874867dd…`. `metrics-restored/restoration-proof.json` restores both Apps, both original
INIs and `main.cpp`; `schema4/restoration-proof.json` restores the head INI. Final identity proof
records no `ThemeProbe.mm` or probe symbols. Sources match their recorded backups exactly.

| Restored original workspace | SHA-256 |
|---|---|
| Head INI | `1f1e1a2780df02bb3a0e47f0687e8236407e74fdaeaeaa8b1100c223955e6134` |
| Parent INI | `dcc831fbaf29b07a596e55c49e36bc2eba508ec58ef264d49910c111591e2b6d` |

Schema 4 → 5 uses the retained derived fixture `42e5554b…`; output `3ed7eeae…` keeps all six
DockIds and seven node/parent identities, with Auto, Comfortable, scale 100 and no default
rebuild log. This is automated migration evidence only. Native docking and readability remain UNVERIFIED.

## Source scope and remaining native evidence

Source audit finds exactly 21 catalog sets and an exhaustive renderer switch. Agent lifecycle,
attention and Pending/Applied proposal controls remain Gallery fixtures; no runtime backend exists.
The literal-color policy guards panel/shell colors and shared neutral disclosure headers. Its
bounded lexical scope and narrow semantic fixture allowances remain unchanged by this audit.
Renderer/Engine/Scenes, headless capture/measurement, scene documents, screenshot reference/drivers
and RojoRHI pin remain unchanged from parent. The editor-only SelectionOutline shader changes its
encoded constant to `#4CABFD`; UI zoom/selection framing uses existing renderer contracts.
These source findings do not prove exact output equality, native focus or actual gesture delivery.

[Current native validation](ux4-final-native-validation.md) retains every Task 8/12/14/16/17 and
UX1 completion context as UNVERIFIED. Current System Settings original/read/restore checks are
UNVERIFIED while locked; no OS preferences changed in Tasks 14–19. Earlier actual restorations
remain tied to their observed revisions. No 1× capture, held RMB movement, graph pan, docking,
native menu editing or missing final gesture is inferred from probes or old screenshots.

## Controller final-check and publication phase

The controller owns fresh independent review and all 17 final checks, then the serial
`docs: record design system validation (UX4)` commit, push, PR creation and evidence attachment.
Their exact frozen results accompany publication separately from this combined-run record.
This audit does not claim those checks have passed or invent a PR URL. Publication is authorized;
owner acceptance and merge are not. ADR 0029 stays Proposed and the milestone/plan stay In progress.
