# GPU work-submission experiment: terminal evidence record

**Status**: Frozen — non-normative
**Date**: 2026-09-06

## Disposition

The user explicitly approved closing M5.6 as **reliability failure / DEFER / no accepted
performance conclusion**. [ADR 0012](../decisions/0012-gpu-submission-defer.md) and the
[roadmap](../roadmap.md) own this accepted terminal exception. The
[milestone record](../milestones/m5.6.md) records closure, not successful original acceptance.
Production retains its existing renderer/RHI and gains no experimental implementation.

The exception permits stopping without the full 1,920-pair matrix. It does not turn incomplete
or failed measurements into passes, establish a break-even range, relax future adoption gates,
or establish hardware damage or a driver/compiler defect. Root cause remains unresolved.
No GPU work was run for this documentation/evidence closure.
Production App and Tests builds and the CPU unit suite passed during closure; these do not
certify the failed experiment's GPU path. No production runtime source changed.

## What is preserved

The initial corpus has 1,043 structurally valid pairs, two aborted processes and one interrupted
pair. All remain unaccepted performance evidence. Pre-collection correctness tests passed,
including 160 case/suite/mode combinations × 256 frames, all-mode 900-frame stress and production
checkpoint A. Those verification runs used readback-altered scheduling; they did not certify
uninterrupted no-validation submission, which subsequently failed.

Seventeen later unscored diagnostic records contain 13 retired runs and 4 failures. All start/
outcome records are structurally consistent; that is not parity or reliability acceptance.
Single gpu-args/S runs repeatedly timed out during warmup on submission 2, with later callbacks
also failing. Increasing the consumer mask did not eliminate the fault. API-only validation
retired one run while the same binary with validation off failed. CPU-filled full-N argument
streams retired twice; GPU generation retired once and failed once. None proves a root cause.

ICB remains unresolved for the pinned Slang route; GPU-span/stage instrumentation is unverified;
captured post-dispatch arguments were not established by initial-state trace decoding. Missing
gates remain missing. Native results are not production-renderer speedups.

Last verified GPU environment: Apple M3 Max (40 GPU cores, 16 CPU cores, 64 GB), Mac15,9,
macOS 26.5.2 (25F84), Darwin 25.5.0; Apple clang 21.0.0 (clang-2100.1.1.101), Slang 2026.14.1,
metal-cpp 26.4. Runtime MSL compilation was used without the optional offline Metal toolchain.
The last diagnostic reset was recorded at 22:36:24.322 Asia/Shanghai on 2026-09-06.

## Source and artifact custody

Immutable local tag: `m5.6-gpu-submission-evidence` on the experiment history. It retains
`Experiments/GpuSubmission/` (executor, shaders, oracle/reference, runner, report tools and tests),
the historical design/executor plan, and every per-attempt research note. The production baseline
contains only the decision, milestone, closure record and roadmap/navigation updates. It contains
neither experimental source/build registration nor the completed executor plan.

The tag and evidence bundles are local; neither a remote tag nor uploaded artifacts are claimed.
The original evidence root directories are siblings of the project checkout on the authoring
machine. Each contains `artifact-index.json`. At closure every listed file was re-hashed and
matched its inventory, including contained capture aliases; all five index hashes also matched.

| Local bundle basename | Artifacts | SHA-256 of artifact-index.json |
|---|---:|---|
| `luminex-m5.6-evidence.ltlLq0` | 12092 | `cc978619a7a83654f10071bee63481f7af47e42ddf9d6146cc7f391f75cb96dd` |
| `luminex-m5.6-diagnostic.0aHXYM` | 27 | `d602a06007a1f347d3b2c7f0a982f1fa752547983e7f06bae9e047fe80ff6d4e` |
| `luminex-m5.6-dependency.ntXuRw` | 43 | `a3b3565dc8933721d5d9dd998fc651f9551b0195545a30b83f4afdaca9cdf702` |
| `luminex-m5.6-instrumentation.KtOwrj` | 52 | `54869498c32d9569098b7f5c76fcc3ee7fa3ec9edea042028b01b6d4b70c6836` |
| `luminex-m5.6-argument-source.FWZ50j` | 48 | `aec5425fb545fb59aaa79a56fedaff86baa3ce7403ce96248f50b4ecb3813ecc` |

These are inventory hashes, not executable hashes. Each run retains its own executable/shader/
manifest identities and requested environment. Later diagnostics never replace a failed pair.
The separate local `luminex-m5.6-closure.3enEhj` bundle contains the read-only inventory verifier,
verification receipts and closure/build/policy logs; old bundles were not edited.

Inspect retained sources without restarting the failed workload:

```sh
git show m5.6-gpu-submission-evidence:Experiments/GpuSubmission/README.md
git show m5.6-gpu-submission-evidence:docs/specs/2026-09-06-m5.6-gpu-work-submission-design.md
git show m5.6-gpu-submission-evidence:docs/plans/2026-09-06-m5.6-gpu-work-submission.md
```

The per-attempt records under `docs/research/` at that tag preserve the interrupted collection,
diagnostic, dependency, instrumentation and argument-source findings. Their historical wording
that M5.6 remained open describes those dates/attempts; this accepted terminal disposition is later.
The initial partial report was regenerated twice with byte-identical summaries, as recorded there;
that reproducibility does not make it a complete or accepted performance corpus.

## Follow-up and project fit

Root-cause diagnosis is separate future work, not an active M5.6 plan or scheduled GPU task.
It should seek a smaller reproducer and distinguish submission pacing, native execution and
resource visibility before claiming a correction. No validation toggle, delay, broader barrier
or isolated successful retirement has been adopted as a fix. Any future comparison starts from
new source/workload identities, complete exact-artifact validation and a fresh paired corpus;
all capture/capability and uncertainty/regression guards still apply.

M6 temporal/display work is unblocked. M7 first needs a maintained CPU/batched production benchmark
and oracle, then considers proven GPU work at interface gate B. Neither ICB nor an RHI redesign
is automatically selected. The earlier project-fit assessment is retained at the evidence tag in
`docs/research/2026-09-06-graphics-paradigm-project-fit.md`; this failed lab does not establish a
different project's performance advantage. A bounded neural-shader study remains optional,
and new hardware/server access remains an enabler rather than a prerequisite for M6.
