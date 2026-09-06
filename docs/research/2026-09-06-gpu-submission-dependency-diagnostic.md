# GPU submission: consumer dependency discriminator

**Status**: Frozen — non-normative

**Date:** 2026-09-06. Follow-up to the [single-mode timeout](2026-09-06-gpu-submission-diagnostic.md),
on the same user-authorized Mac. No performance result, production correction or M5.6 closure
is claimed. Previous evidence bundles and research records are unchanged.

## Result

Expanding the consumer barrier to all stages did **not** eliminate the timeout. Four sequential
fresh processes used the same executable, shaders, manifest, gpu-args/S case
N=16,384/T=32/visibility=0.5/B=1, three slots, 32 warmup and 900 requested replay frames.
API validation, shader validation and capture were disabled in every run.

| Order | Destination mask | Outcome |
|---|---|---|
| 1 | All stages | 900 replay frames retired after warmup |
| 2 | Declared vertex + fragment | 900 replay frames retired after warmup |
| 3 | Declared vertex + fragment | 900 replay frames retired after warmup |
| 4 | All stages | Timeout during warmup; replay never began |

Run 4's first retained failure identifies submission 2, slot 1, warmup frame 1;
submissions 3 and 4 also report MTL4CommandQueueErrorDomain code 1 (Timeout).
The kernel records GPURestartBegin at 18:41:25.550 Asia/Shanghai. The process exits 1 with a
failed diagnostic JSON after feedback delivery. The prior three kernel checks found no reset.
No further GPU run was attempted after this reset. The failure is not a 900-frame pass.

This is a bounded unscored repeatability check, not a statistical comparison of failure rates.
Both masks have now been seen to retire and fail across diagnostic sessions. Success of the
first all-stage run was insufficient evidence of correction. This does not exclude every
synchronization defect, identify the faulting GPU stage, or establish a driver/compiler defect.
Logging and callback joins remain observers; retirement does not claim image/argument parity.

## Meaning of the discriminator

Only the diagnostic switch changes the compute-to-raster consumer dependency from
dispatch → vertex|fragment to dispatch → all, keeping Device memory visibility. Ordinary
measurement retains the original mask. `--diagnostic-dependency declared|all` is diagnostic-only;
`all` requires gpu-args. Both start and outcome JSON record the mask under diagnostic protocol
`pipelined-feedback-dependency-v2`; no scored result.json is produced.

Apple's [render command-stage table](https://developer.apple.com/documentation/metal/mtl4rendercommandencoder?language=objc)
assigns indirect drawPrimitives to vertex and fragment stages, supporting the original mask.
However, [consumer barriers](https://developer.apple.com/documentation/metal/synchronizing-passes-with-consumer-barriers)
also constrain later passes on the queue, including other command buffers. The all-stage version
can therefore change inter-frame compute scheduling; it does not isolate argument-fetch visibility.
No dedicated indirect-fetch stage or additional buffer-storage requirement was found in the
installed Metal 4 headers. Contract inspection is not proof of fault-time visibility.

Independent review also closed a diagnostic API isolation gap: renderNativeFrame now rejects
either diagnostic flag, including all-stages alone, before native initialization. CLI rejection
tests use otherwise-valid requests, require the intended error and verify that output was not
created. Empty dependency values are rejected rather than silently becoming the default.
These guard/test changes happened after the GPU snapshot and do not alter the successful or
failed runs retroactively. The snapshot's actual shader and executable identities are retained.

The hardened benchmark and tests rebuilt. CPU regression passed 22 cases / 37,019 assertions;
CLI tests passed nine cases. Paired-analysis, validation-assembly and archive-tool tests passed
33, 38 and 16 cases respectively. Focused experiment policy passed 12 files / 121 parsed
definitions, and project policy passed 349 files. No production GPU suite was rerun after reset.

## Custody and next work

The separate local bundle `luminex-m5.6-dependency.ntXuRw` retains all four start/outcome JSONs,
logs, kernel observations, executable/MSL snapshot and its source.patch, plus final guard-hardening
patch and CPU-check logs. It is local evidence, not an uploaded artifact. Reproduction uses the
retained snapshot or an explicitly identified rebuild; never mix these with scored collection.

| Snapshot identity | SHA-256 |
|---|---|
| Executable | `d77b1ac15e3251f313020e7a4de1098c089e4820507a7006daf87ea63071f577` |
| Shader bundle | `ae045f9a07b7a002e899f9e6b25c3936c4ef537bd0b328311d347304ec10bd1c` |
| Target manifest | `f2969cecb51bc15feaf7f055495afe5d0bf6950ffd8b0d1b6e324602b32995f7` |

In an arranged test session, use a fresh output directory for each run:

```sh
GpuSubmissionBench --diagnose --diagnostic-dependency all --case n16384-t32-v50-b1 --suite S --variant gpu-args --frames 900 --output <new-directory>
```

Repeat with `declared` for the original dependency. Do not run a measurement matrix to search
for this fault. A useful next discriminator separates producer and consumer instrumentation
while holding API validation and requested lengths fixed; Apple's
[shader-validation documentation](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage)
explains that instrumentation changes code and can substitute zero/drop invalid accesses.
Any resulting pass would localize sensitivity, not prove correctness. A root-cause correction
still needs fresh complete validation and collection; ICB, capture and GPU-span gates remain open.
