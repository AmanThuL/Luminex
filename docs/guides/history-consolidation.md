# Milestone history consolidation

**Status**: Accepted

On 2026-09-23 the owner authorized consolidation of the published history through R3.6 and
revision of its commit messages. This follows the [first R3 consolidation](r3-history-consolidation.md),
which replaced 214 R3 commits with six after R3.6 completed. Routine integration follows the
[squash convention](../conventions/commits.md); neither operation authorizes later history rewrites.

## Scope and preserved state

This publication replaces the 120-commit chain ending at
`35101f0c6f7901d39c120873ae3f2d0901b48cb5` with 51 commits. A separate documentation commit adds
this guide and updates the conventions and the first consolidation record, making 52 commits.
The first four bootstrap commits keep their original IDs. M1 through M3.1 remain the single
baseline that was originally published; this operation does not invent separate milestones.

Nine contiguous ranges are consolidated. Other milestone commits retain their boundaries,
including the six R3 commits from the first publication. Independent fixes, CI and roadmap
changes remain separate. The R2.3 rename and R2.4 extraction no longer need their development
commits on Luminex's main branch: the original revisions and RojoRHI's imported history remain
available for provenance.

| Milestone | Commits before | Commits after | Original PRs |
|---|---:|---:|---|
| M4 | 36 | 1 | #7 |
| M4.1 | 2 | 1 | #8, #9 |
| M5.3 | 2 | 1 | #14, #15 |
| M6.1 | 2 | 1 | #20, #21 |
| M6.3 | 2 | 1 | #25, #26 |
| UX1 | 4 | 1 | #31 |
| M7.1 | 2 | 1 | #33 |
| R2.3 | 10 | 1 | #41 |
| R2.4 | 18 | 1 | #42, #43, #44, #45 |

Every replacement uses the exact Git tree at the end of its original range. All 51 endpoint
comparisons include file contents, modes and gitlinks. The final documentation tree is also
compared with the archived main tip. No source, shader, asset or RojoRHI pin changes are part
of this publication. RojoRHI's repository and existing evidence tags are unchanged.

Messages describe the final behavior instead of concatenated development logs. All 51 messages
were reviewed, including already consolidated milestones; 41 were revised. The remaining ten
already fit the convention. Technical limitations and failed gates remain in the messages where
needed and in the unchanged validation records. Each replacement preserves the author name,
email and date of its original endpoint; only new commit objects receive new committer metadata.
Original messages and metadata remain in the archives. Historical paths in a message refer to
that commit's tree, even if a later milestone moved the file.

## Published endpoint mapping

The full mapping, including each original range start and tree ID, is in the external receipt.
This table covers all 51 preserved endpoints. Old IDs refer to the chain before this publication;
for the pre-consolidation R3 development chains, use the first consolidation record linked above.

| # | Commit subject | Old endpoint | New endpoint |
|---|---|---|---|
| 1 | build: initialize the repository | `451e6ea6ccb3` | `451e6ea6ccb3` |
| 2 | build: pin third-party dependencies | `1e940811de0c` | `1e940811de0c` |
| 3 | build: add project generation and setup scripts | `161194c199c1` | `161194c199c1` |
| 4 | rhi: add the Vulkan triangle path | `fa72d524148d` | `fa72d524148d` |
| 5 | render: establish the Metal 4 baseline (M1-M3.1) | `d11e571e2439` | `a32b6fcf6d3f` |
| 6 | ci: handle hosted runners without Metal 4 | `2b3bf7592308` | `ddea4c75939e` |
| 7 | docs: describe the renderer and its supported features | `cc8e6d4642e8` | `758f9108f305` |
| 8 | docs: update the rendering roadmap (#6) | `484d2888bbc7` | `1bce35088337` |
| 9 | render: add HDR image formation and GGX materials (M4) | `e60101f59927` | `1bc46160339c` |
| 10 | rhi: separate the RHI and add MaterialLab lookdev (M4.1) | `aa5b9e4a93f1` | `39f2163f0657` |
| 11 | render: add graph execution and GPU diagnostics (M5, #10) | `5b494aba2059` | `6b586a660ceb` |
| 12 | spike: evaluate address-first RHI submission (M5.1, #11) | `84cfc6f79652` | `3c482a327bed` |
| 13 | ci: allow spike squash subjects (#12) | `7b26817fedff` | `d5f3ed85668b` |
| 14 | rhi: add a growable frame-data arena (M5.2, #13) | `9535f6f66dae` | `daf7cf73bf20` |
| 15 | app: add the editor workspace and selection model (M5.3) | `9e42ae39cb79` | `69ed16546779` |
| 16 | app: draw the Render Graph as nodes (M5.4, #16) | `116848aa5d0c` | `5fd1687c77eb` |
| 17 | app: detach and lay out the Render Graph window (M5.5, #17) | `4345b2a53acd` | `dc4829833112` |
| 18 | docs: define the GPU work-submission experiment | `30ab99c184d1` | `8906c12cbe4d` |
| 19 | docs: bound temporal and scalable rendering milestones | `467f705f14c9` | `fc30e7b74cab` |
| 20 | docs: close the submission study with adoption deferred (M5.6, #19) | `8bf08bd425ee` | `c4d2c4ff245e` |
| 21 | render: add temporal state and rigid motion (M6.1) | `05dc523360af` | `072d99af061c` |
| 22 | render: add native TAA and exposure adaptation (M6.2, #22) | `efb395ba1175` | `e58479d0538c` |
| 23 | render: smooth bloom and studio reflections (#23) | `acdf872b70df` | `0fb7aedf2b26` |
| 24 | docs: record remote GPU evidence archive and recovery | `bed51009f92e` | `0af5ea41eff0` |
| 25 | render: add temporal upscaling and dynamic resolution (M6.3) | `6debb9fdf085` | `fd18290b76a8` |
| 26 | render: add the MetalFX temporal adapter (M6.4, #27) | `7fd8b755251f` | `f6e43f533daf` |
| 27 | render: define SDR display and capture domains (M6.5, #28) | `65ba3fce5fcd` | `bb1c5203093a` |
| 28 | core: separate assets, scenes and editor models (R1) | `4edd0f3b6e9c` | `230b9bade5e5` |
| 29 | docs: accept the scene handoff contract (Gate B, #30) | `89ef4686290f` | `721af4a859c9` |
| 30 | editor: improve scene controls and frame diagnostics (UX1) | `b47d40174617` | `452fcdfa3a7c` |
| 31 | docs: compare the rendering roadmap with practice since 2023 | `51407b148514` | `183b180df08f` |
| 32 | docs: add neural rendering studies to the roadmap | `5a509dc5ec46` | `9e2046aa9e47` |
| 33 | ci: skip the macOS build for documentation-only changes | `5a931968cbf1` | `29a19d9202d2` |
| 34 | docs: define the delivery order and split rendering milestones | `95cbeaae3a87` | `b205aaa09076` |
| 35 | scene: share GPU scene tables across direct draws (M7.1) | `acf82edcea0b` | `ee47d6840bf2` |
| 36 | render: add CPU visibility and draw submission modes (M7.2) | `a5c447f3c803` | `02f10281f1bf` |
| 37 | render: generate draw work with GPU visibility (M7.3) | `147206e5c959` | `72b6f4daf898` |
| 38 | render: add previous-frame HZB occlusion (M7.4) | `98e0ed6cce40` | `630279adc10a` |
| 39 | render: add clustered local lighting (M7.5, #37) | `677e3b0b7e48` | `f55abcbda528` |
| 40 | docs: schedule R2-R4 and UX2 before neural rendering (#38) | `3388c6df786f` | `a52de4fb14f8` |
| 41 | docs: accept the RojoRHI relocation contract (R2.1, #39) | `a2787b51cb4b` | `deb290a84cb1` |
| 42 | rhi: decouple the RHI before extraction (R2.2, #40) | `459ac2b9e96a` | `96836bf43caa` |
| 43 | rhi: rename the component to RojoRHI (R2.3) | `df1645dd435c` | `6802a6d636a9` |
| 44 | build: extract RojoRHI and mount its submodule (R2.4) | `d7d43edf10e8` | `0dd9d4e66cbb` |
| 45 | docs: group milestone records by series (R3.1) | `f9172dbd27f0` | `aa49357dc3c8` |
| 46 | shader: group sources by pass family (R3.2) | `6b189c812c36` | `c3fe984ad6db` |
| 47 | engine: move scene ownership below Render (R3.3) | `f0c1d5c0f4b7` | `72330b9cad03` |
| 48 | core: add shared math and containers (R3.4) | `16b61e533071` | `5c1d6ca49b67` |
| 49 | render: group passes and share stage helpers (R3.5) | `3a895661b471` | `0a476d297d26` |
| 50 | app: separate editor code and use Core helpers (R3.6) | `123345cbd565` | `64160e30738f` |
| 51 | docs: use squash integration and archive the R3 history (#55) | `35101f0c6f79` | `5d35903eb277` |

## Archives and recovery

The immutable remote tag `archive/pre-milestone-squash-2026-09-23` retains the old main chain,
including the documentation PR for this publication. The earlier
`archive/pre-r3-squash-2026-09-23` retains the R3 development commits before the first consolidation.
Existing evidence tags, validation SHAs, PR records, captures and RojoRHI history keep their
original meaning. Do not relabel old runs as tests of the new commit IDs. Tree equality preserves
the source state; it does not turn a failed gate or scoped exception into a pass.

The external receipt is at `../Luminex-evidence/history-milestones-2026-09-23/`. It contains
`mapping.json`, the final message inventory, `publication.json`, protection snapshots and a
SHA-256 checked `history-before-and-after.bundle`. The bundle contains both chains and is
verified by restoring the old archive and new main into a fresh bare repository and running
`git fsck`. The remote archive and local bundle provide separate recovery copies.

Inspect archived history without changing the current checkout:

```sh
git fetch origin tag archive/pre-milestone-squash-2026-09-23
git log archive/pre-milestone-squash-2026-09-23 -- docs/plans/
git show <original-sha>:<path>
```

Old clones cannot fast-forward to the new main. Preserve local work and a backup ref before
fetching. Reset a clean local main only after checking that it contains no unique work; never
reset a dirty checkout. Replay only a topic branch's own commits onto the new main:

```sh
git branch backup/topic-before-history-consolidation <topic>
git fetch origin
git rebase --onto origin/main <old-base-of-topic> <topic>
```

Use the actual old base, not the entire archive. Validate the rebased branch normally. Detached
evidence worktrees and old SHA references may remain unchanged. Do not merge an archived chain
back into main or use an unexamined `git pull` to reconcile the rewritten history.

## Publication checks

Verify every endpoint tree, the final documentation tree and the RojoRHI pin before publication.
Confirm that no competing writer has advanced main. Publish and verify the archive tag, then
verify the offline bundle before replacing main with `--force-with-lease` against the exact
archived SHA. Preserve all protection fields and restore administrator enforcement and disabled
force pushes immediately afterward. Required PRs, checks, linear history and conversation
resolution remain enabled. Repository settings permit squash merge only, with the PR title and
an empty default body; select the final message explicitly when a body is needed.

Verify the remote tip, unchanged existing tags, restored protection and CI after publication.
Rollback requires separate authorization and an exact lease against the then-current main;
never discard work published after this operation. Use the archive or the verified bundle to
recover the old tip, and restore the saved branch protection after any recovery operation.
