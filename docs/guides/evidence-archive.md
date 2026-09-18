# Historical evidence archive

**Status**: Implemented

The sibling `../Luminex-evidence/` checkout owns the private evidence index, small reports,
file checksums and recovery scripts. Its private GitHub repository is
[AmanThuL/Luminex-evidence](https://github.com/AmanThuL/Luminex-evidence).
Large historical artifacts are stored in a private Google Drive folder; per-archive
locations and verification state are recorded in `manifests/archives.json` in that repository.

Paths such as `../Luminex-evidence/m7.3` in milestone records are relative to the renderer
repository root and identify the original evidence layout, not a promise of resident files.
Archived directories are materialized only when requested. Small readable copies live
under `../Luminex-evidence/reports/`, preserving their original relative paths.
Historical absolute paths embedded in raw logs and JSON remain unchanged as provenance.

## Recover evidence

From the evidence checkout, run `python3 scripts/restore.py` to list archives and transfer status.
Choose a `verified` archive for recovery. The authenticated rclone remote `personal:` on the
authoring machine is rooted at the private evidence Drive folder. Run:

```sh
python3 scripts/restore.py m7.3 --remote personal:
```

The script requires Python 3.9+, `tar` and `zstd`. It verifies archive SHA-256 before extraction
and checks extracted files against the inventory. It restores into a new
`.restored/m7.3/` directory; the original `m7.3/` layout is nested inside it.
Existing destinations are rejected. `--verify-only` checks just the archive.

The target storage layout uses one `.tar.zst` per archive group. To restore a manually downloaded
complete archive, use `--archive /path/to/m7.3.tar.zst` instead. During consolidation, the same
rclone command also handles groups still listed as multipart in the active manifest. Historical
partition records remain in `manifests/archives-before-consolidation.json` for provenance.

Consolidation is paused after shared-client Google Drive API request-quota errors; verified
original parts remain available. The evidence README records current progress and the required
owner OAuth client setup. Credentials stay outside Git. The active `m7.4-2026-09-18/`
evidence directory is outside the historical inventory and remains untouched.

The `m7.2-validation-isolation-trace` archive contains the complete standalone Instruments
trace package and is separate from the remaining `m7.2` evidence. Full source/build snapshots
are retained in cloud archives for provenance; they are not part of the renderer source tree.
The M5.6 GitHub Release archive workflow remains separate; see
[gpu-submission-archive.md](gpu-submission-archive.md).
