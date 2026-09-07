# GPU submission experiment archive

**Status**: Accepted

The experimental source and reports are preserved by the published Git tag
`m5.6-gpu-submission-evidence`. The complete raw evidence is available as attachments to the
[GitHub Release](https://github.com/AmanThuL/Luminex/releases/tag/m5.6-gpu-submission-evidence).
This is the current archive location; the frozen closure report and ADR describe local custody
at the time of closure.

On 2026-09-07, all six original evidence directories were archived and uploaded. Each archive
was checked against every original file and symbolic link; GitHub's server-side SHA-256 digests
and asset sizes matched the local uploads. The originals were checked again before deletion.
The six local evidence directories have been removed.

## Contents

Each directory below is preserved in a corresponding `.tar.gz` attachment:

| Original directory | Contents |
|---|---|
| `luminex-m5.6-evidence.ltlLq0` | Measurements, GPU captures, frozen binaries and validation logs |
| `luminex-m5.6-diagnostic.0aHXYM` | GPU timeout reproducer evidence |
| `luminex-m5.6-dependency.ntXuRw` | Dependency-mask comparison evidence |
| `luminex-m5.6-instrumentation.KtOwrj` | Validation and shader instrumentation comparisons |
| `luminex-m5.6-argument-source.FWZ50j` | CPU/GPU argument producer comparisons |
| `luminex-m5.6-closure.3enEhj` | Inventory verifier, closure receipts and build/policy logs |

`SHA256SUMS` verifies the six archives and `file-manifest.json`. The manifest records each regular
file's size and SHA-256 and each symbolic link's target. Archives preserve original directory
names and capture symlinks. Raw evidence is stored in Release attachments, not in the Git tree;
cloning the repository alone does not download it.

## Restore

From a separate empty directory outside the source checkout, with GitHub CLI installed:

```sh
gh release download m5.6-gpu-submission-evidence --repo AmanThuL/Luminex
shasum -a 256 -c SHA256SUMS
```

Only after every checksum passes, extract the archives:

```sh
for archive in luminex-m5.6-*.tar.gz; do
    tar -xzf "$archive"
done
```

To inspect the preserved source from a repository clone:

```sh
git fetch origin tag m5.6-gpu-submission-evidence
git show m5.6-gpu-submission-evidence:Experiments/GpuSubmission/README.md
```

The [closure report](../research/2026-09-06-gpu-submission-closure.md) retains the original
artifact-index hashes and findings. Archival publication does not change the experiment's
reliability failure or establish a performance conclusion.
