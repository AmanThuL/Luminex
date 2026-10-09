# Historical evidence archive

**Status**: Implemented

The sibling `../Luminex-evidence/` checkout holds the private evidence index, small reports,
file checksums and recovery scripts. Its private GitHub repository is
[AmanThuL/Luminex-evidence](https://github.com/AmanThuL/Luminex-evidence).
Large historical artifacts live in a private Google Drive folder, and `manifests/archives.json`
in that repository records each archive's location and verification state.

Milestone records cite paths such as `../Luminex-evidence/m7.3` relative to the renderer
repository root. They name the original evidence layout; the files themselves are not kept
on disk and appear only when restored. Every evidence group through `ux6` is archived, including the
`m7.4`/`m7.5` dated groups, `r2.2`–`r4.1` and the two 2026-09-23 history groups.
Raw logs and JSON keep their historical absolute paths as provenance.

Small readable copies live under `../Luminex-evidence/reports/` at their original relative
paths. The 13 initial groups keep every readable file up to 1 MB. Later groups keep summaries
and every path a milestone record cites, while their bulk per-run logs and dumps exist only in
the archives. The raw BMP frames of the three `m7.4-2026-09-18` attempt1 sequence and
controller-rail folders were deleted without archiving; their reports remain, and the evidence
README records the count and the receipt.

## Recover evidence

From the evidence checkout, `python3 scripts/restore.py` lists archives and their transfer
status. Recover from a `verified` archive. On the authoring machine, the authenticated rclone
remote `personal:` is rooted at the private evidence Drive folder:

```sh
python3 scripts/restore.py m7.3 --remote personal:
```

The script needs Python 3.9+, `tar` and `zstd`. It checks the archive's SHA-256 before
extracting, then checks every extracted file against the inventory. It restores into a new
`.restored/m7.3/` directory with the original `m7.3/` layout nested inside, and rejects an
existing destination. `--verify-only` checks the archive alone.

Each archive group is stored as one `.tar.zst`. To restore a complete archive downloaded by
hand, pass `--archive /path/to/m7.3.tar.zst` instead. Seven of the initial archives, `m7.3`
among them, are still stored as multipart uploads; the same rclone command handles them from the
active manifest, and a manual restore needs every part with `--parts-dir`.
`manifests/archives-before-consolidation.json` keeps the original partition records for provenance.

Merging those multipart uploads into single archives stopped when rclone's shared client hit the
Google Drive API request quota. The verified parts remain available. The evidence README records
progress and the owner OAuth client setup needed to resume; credentials stay outside Git.

The `m7.2-validation-isolation-trace` archive holds the complete standalone Instruments trace
package, separate from the rest of the `m7.2` evidence. Full source and build snapshots are kept
in the cloud archives for provenance and are not part of the renderer source tree. The M5.6
GitHub Release archive workflow is separate; see [gpu-submission-archive.md](gpu-submission-archive.md).
