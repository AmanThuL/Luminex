# R3 history consolidation

**Status**: Closed (historical record; current mapping in the [history guide](history-consolidation.md))

On 2026-09-23 the owner authorized a one-time consolidation of Luminex's published R3.1–R3.6
history, to be published after both R3.6 PRs and their evidence custody completed. The target is
one commit per milestone; routine future integration uses [squash merge](../conventions/commits.md).
This first consolidation left R2 and earlier history unchanged. RojoRHI's repository and submodule
commit were unchanged. The owner later authorized the broader consolidation and message revision
recorded in the [current history guide](history-consolidation.md). The hashes below identify this
first publication; they remain available through its archive and the second publication's archive.

## First publication milestone commits

The unchanged parent is `d7d43edf10e82fc8c89c5cc9a476f85b43f1b07a` (the R2.4 closure).
Each replacement commit uses exactly its original milestone's final Git tree, including file
modes, documentation and the submodule gitlink. The 214 original commits become six.
R3.2 includes the format-gate follow-up (#47); R3.5 and R3.6 each combine their two integration PRs.
The integration-policy and recovery documents are a separate documentation commit after R3.6.

| Milestone | Original PRs | Original commits | Original end | First publication end |
|---|---|---:|---|---|
| R3.1 | #46 | 27 | `d2a0e55b2eb72d40c3f6fec23b318182ed4470a3` | `f9172dbd27f0b47f310f6fee87ebcf98c80ea287` |
| R3.2 | #47, #48 | 28 | `75dd5396768fff5407ae17cec3f90a67bff00222` | `6b189c812c368f9621f24aef21a64df68832d7ca` |
| R3.3 | #49 | 26 | `52945a6714b9d9e894a224a926760e5238cd8547` | `f0c1d5c0f4b7745ae8f6b4e09209891b709af922` |
| R3.4 | #50 | 47 | `2f37615d55d9724e9deabdd263dc30ddfb686c67` | `16b61e533071db959ea327fca215ead1262b2410` |
| R3.5 | #51, #52 | 48 | `7b5710271a92280e8bdf40de02137cb47b02c9e2` | `3a895661b471a550de1cb2864ef5af9c2d52cd3b` |
| R3.6 | #53, #54 | 38 | `fafe9d387ff839397f7a2fef8436fd16588f1caf` | `123345cbd56508f82901e5164bef515fc0777e33` |

The original R3.6 endpoint is `fafe9d387ff839397f7a2fef8436fd16588f1caf`. Its replacement is
`123345cbd56508f82901e5164bef515fc0777e33`. Their entire file trees are identical. The subsequent
documentation commit changes only AGENTS.md, the commit convention and this recovery guide.

## Evidence remains on the original revisions

The immutable remote tag `archive/pre-r3-squash-2026-09-23` retains the complete old main chain,
including the integration-policy documentation PR. Existing evidence tags are neither moved nor
deleted, including `r2.4-pre-extraction-evidence`, `r3.1-closed-plans`, the R3.2 chains and
`r3.4-gated-chain`. Old validation SHAs, captures and PR records keep their original meaning;
they are not relabeled as runs against the new SHAs. Closed executor plans remain recoverable
through the archive and existing tags even when squashing removes them from the canonical chain.

The migration receipt is stored outside the repository at
`../Luminex-evidence/history-squash-2026-09-23/`. It contains the old-to-new mapping, a verified Git
bundle, before/after protection settings, exact-tree comparison results and the remote update
receipt. The bundle and remote archive retain the original history independently.

For example, inspect the original history without changing a working checkout:

```sh
git fetch origin tag archive/pre-r3-squash-2026-09-23
git log archive/pre-r3-squash-2026-09-23 -- docs/plans/
git show <original-sha>:<path>
```

Do not rewrite evidence records to claim new renderer tests. Tree equality transfers the source
state only; every recorded failure, parity exception and evidence limit still applies.

## Existing clones and branches

Old `main` no longer fast-forwards to new `main`. Do not merge the old chain back or run an
unexamined `git pull`. First preserve local work and a backup ref, then fetch. For a clean local
`main` with no unique work, reset it explicitly to the verified `origin/main`; never reset a dirty
checkout. A topic branch based on the old tip must replay only its own commits, for example:

```sh
git branch backup/topic-before-r3-consolidation <topic>
git fetch origin
git rebase --onto origin/main <old-base-of-topic> <topic>
```

Use the actual old base, not the entire archived R3 range. Resolve and validate the resulting
branch normally. Detached evidence worktrees and their old SHA references may stay unchanged.

## Publication and recovery

Before replacing `main`, verify every milestone's old/new tree ID, the final tree including the
documentation update, the unchanged RojoRHI gitlink, the archive tag and the offline bundle.
Pause competing writers, retain all branch-protection fields, and use `--force-with-lease` with
the exact archived main SHA. Restore protection immediately after the update, including required
PRs/checks, administrator enforcement, linear history and disabled force pushes. Only squash merge
remains enabled in repository settings. Verify remote refs and the final CI result afterward.

Rollback is an exceptional owner-authorized operation, not normal development: recover the old
main SHA from the archive tag or receipt, verify it, and update `main` using an explicit lease
against the then-current remote SHA. Never discard work published after the consolidation.
Restore the saved protection configuration after any such operation. Local bundle recovery also
works if the network archive is unavailable; run `git bundle verify` before fetching from it.
