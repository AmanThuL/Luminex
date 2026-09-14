# Neural and Learned Rendering

**Status**: Accepted

Part V of the [rendering roadmap](../roadmap.md) owns learned techniques as bounded experimental
features over the shared scene, material, light and temporal contracts. The
[2026-09-14 direction review](../research/2026-09-14-rendering-direction-review.md) motivates it:
Metal 4 ships first-party in-shader tensor operations and an ML command encoder, vendor
reconstruction and denoising are default on every platform, and the shading-language toolchain
does not yet reach Apple's path. These are accepted study boundaries, not shipped capabilities and
not a commitment to every idea.

## Placement and ownership

The roadmap entry's [execution sequence](../roadmap.md#execution-sequence) owns the cross-part
order: after [UX1](editor-experience.md) and the five
[M7](gpu-driven-hybrid-rendering.md#m7--scalable-scene-and-direct-lighting) slices it is
**M7 → N1 → M9 → M8 → M10 → M11**, with N2–N4 entering when their prerequisites below exist.
Only one implementation plan is active at a time, so N-slices interleave with M-slices rather
than run beside them. Part II's dependency map still governs the M-slices; this order changes
priority, not their gates. N1 delivers through the four slices N1.1–N1.4 below.

Rendering Foundations owns the temporal, exposure and display contracts every learned pass
consumes. GPU-Driven Hybrid Rendering owns the raster, query and cache paths that supply signals.
This part owns learned-technique slices, weight provenance, the training substrate policy and
the hardware gates. Vendor reconstruction stays behind the M6.4 adapter boundary; N3 adds the
denoiser adapter in the same pattern.

**Hardware floor.** Mesh shaders and hardware ray tracing require Apple M3/A17 Pro or later.
GPU neural acceleration requires the per-core neural accelerators of M5/A19 Pro or later; on
earlier Apple GPUs the same tensor code runs on ordinary shader ALUs. The recorded development
machine is an M3 Max, so N1 validates correctness, overhead and fallback there and records
acceleration as unavailable until an M5-class device exists.

## Shared rules

- Every learned pass names an analytic or reference oracle, an ordinary-shader fallback, a
  frozen budget, a capability query and a removal path before measurement. A study closes with
  an explicit adopt/retain/defer decision; adoption returns through a roadmap or ADR change.
- Training runs offline on a rented CUDA host or in MLX on the Mac. CUDA is a training substrate,
  never an RHI backend or an App dependency. Training scripts, dataset manifests, seeds and
  recorded GPU-hours live with the study; trained weights are pinned assets with provenance and
  hashes like fetched content, never committed binaries of unknown origin.
- Weights export as a flat tensor container with a layout descriptor. A CPU numerical oracle in
  Core/Asset loads the same weights and is the source of truth for every GPU path; numerical
  agreement is a gate before any image or cost claim.
- Slang remains the shader source of truth. Metal tensor code that Slang cannot emit is authored
  in MSL only under the bounded exception in
  [ADR 0022](../decisions/0022-msl-tensor-module-exception.md); each such module keeps a plain
  FP16 Slang path against the same oracle.
- Measure whole-pass cost including conversion, packing and data movement, plus memory and
  warm/cold behavior; report accelerator numbers only from a device that has one, and never infer
  hardware acceleration from API availability.
- Capture sequences may serve as datasets; they carry the same declared warmup, seeds and camera
  tracks as other evidence. Learned passes expose the same intermediate views and reset reasons
  as the native paths they compare against.
- Learned output never changes scene-linear lighting rules, pre-exposure, the display transform
  or the SDR/UI/capture domains; a learned pass consumes named engine signals and returns a
  signal with the same meaning.

## Dependency map

| Slice | Required foundation | Independent ordering |
|---|---|---|
| N1 inference lab | Gate B; no M-slice | First N-slice, after M7 in the accepted order; N1.1 → N1.2 → N1.3 → N1.4 on the recorded machine; acceleration gate needs an M5-class device |
| N2 learned reconstruction | N1; M6 temporal/capture contracts | Does not wait for M7–M11; training needs a rented or local GPU |
| N3 denoiser and reconstruction adapters | N1; M6.4 adapter pattern; M10 noisy signal for the denoiser | The reconstruction adapter part can precede M10 |
| N4 learned materials, compression and caches | N1; per candidate: MaterialLab, TextureBake, M10 oracle | Candidates are accepted separately; the material residual can precede M7 |
| Cross-API port | Validated Windows host and the D3D12 backend (ADR 0007) | Never before the backend |

## N1 — In-shader inference lab

**Outcome:** one fixed tiny network evaluates correctly and measurably inside a representative
rendering pass through every Metal execution path, with a CPU oracle and capability fallback.
N1 finishes when the four slices below pass.

**Deliver:** through N1.1–N1.4: a frozen small multilayer perceptron with fixed inputs and FP16
weights; a plain FP16 Slang shader implementation; an MSL tensor-operation implementation under
ADR 0022; the same network deployed as an exported package through the ML command encoder; the
CPU oracle and weight-loading path; evaluation inside MaterialLab or another representative pass;
the pinned weight/provenance convention; capability queries that distinguish API availability
from acceleration.

**Sequence:** N1.1 → N1.2 → N1.3 → N1.4. N1.3 starts only once
[ADR 0022](../decisions/0022-msl-tensor-module-exception.md) is accepted; N1.2 is the fallback
path every later slice selects when tensor support or acceleration is absent.

**Exit gate:** all GPU paths agree with the oracle within a declared tolerance; whole-pass cost,
conversion cost and memory are reported for each path on the recorded machine; fallback selects
the Slang path when tensor support or acceleration is absent; the acceleration measurement is
recorded as unavailable or, on an M5-class device, as a paired result; the study reports which
paths are viable for fragment-stage use.

**Defer:** any trained consumer to N2–N4; quantization formats beyond FP16 and INT8; a general
operator library, network IR or compiler; D3D12 and Vulkan implementations.

## N1.1 — Network, oracle and weights

**Outcome:** The frozen network, its weight container and a CPU oracle exist before any GPU code.

**Deliver:** The fixed multilayer perceptron with declared inputs, layer sizes and FP16 weights; the flat tensor container with a layout descriptor; the CPU numerical oracle in Core/Asset that loads it; the pinned weight/provenance convention with hashes and a recorded training or generation recipe.

**Exit gate:** The oracle reproduces the recorded reference outputs bit-for-bit on the fixed inputs; the container round-trips through the loader; provenance is pinned like fetched content; no GPU work is claimed.

**Defer:** Every GPU path to N1.2–N1.4; INT8 or other quantization until a consumer needs it.

## N1.2 — Slang FP16 in-pass evaluation

**Outcome:** The network evaluates inside a representative rendering pass through plain FP16 Slang, which is the fallback every later path selects.

**Deliver:** A Slang implementation that consumes the N1.1 weights from a bound buffer inside a MaterialLab pass or another representative pass; capability queries that distinguish API availability from acceleration; fallback selection; whole-pass, conversion and memory measurement on the recorded machine.

**Exit gate:** The GPU result agrees with the oracle within the declared tolerance; the pass exposes its inputs and output as intermediate views; whole-pass cost, conversion cost and memory are reported; the capability query never infers acceleration from API availability.

**Defer:** Tensor operations to N1.3; the ML command encoder to N1.4.

## N1.3 — MSL tensor-operation path

**Outcome:** The same network runs through Metal tensor operations under the ADR 0022 exception, measured against N1.2.

**Deliver:** An MSL-authored tensor module with its plain Slang twin from N1.2, the module's documented interface and build rule, and paired measurements on the recorded machine with acceleration recorded as unavailable.

**Exit gate:** Numerical agreement with the oracle within the same tolerance as N1.2; paired whole-pass and conversion cost against N1.2 on the frozen fixture; the module compiles and validates on the recorded machine and falls back to N1.2 when tensor support is absent.

**Defer:** Acceleration claims until an M5-class device exists; any second MSL module until N1.4 closes.

## N1.4 — ML command encoder path and viability report

**Outcome:** The exported-package path completes the comparison, and the study records which paths are viable for fragment-stage use.

**Deliver:** The network exported as a package and executed through the ML command encoder between graph passes with declared barriers; the closing table comparing N1.2–N1.4 on cost, memory, conversion and integration constraints; the recorded adopt/retain/defer decision for each path.

**Exit gate:** The encoder path agrees with the oracle; all three GPU paths are compared on the same fixture and sequences; the report states which paths suit fragment-stage use and which need a separate pass; the acceleration measurement is recorded as unavailable or as a paired M5-class result.

**Defer:** Any trained consumer to N2–N4; a general operator library, network IR or compiler.

## N2 — Learned reconstruction study

**Outcome:** a reconstruction network trained on the renderer's own sequences is compared with
native TAAU at matched output extent, and the roadmap records adopt, retain or defer.

**Deliver:** dataset generation from capture sequences with color, depth, motion, jitter and
exposure at render extent and reference images at output extent; a reproducible training recipe
following an open upscaling/extrapolation design; an in-engine inference pass consuming the same
temporal inputs as the native path as a capability-selected third reconstruction mode; offline
comparisons through the existing FLIP tooling; history, reset and disocclusion behavior views.

**Exit gate:** the inference pass agrees with the oracle; comparisons run at the same output
extent under identical sequences; camera cuts, disocclusion and resize reset history explicitly;
whole-pass cost and memory are reported beside native TAA and TAAU; Native TAA remains default and
reference; the decision states the workloads and extents the result covers.

**Defer:** frame interpolation or extrapolation to the frame-generation research in Part II;
vendor-specific network formats; training on content the repository cannot pin.

## N3 — Denoiser and reconstruction adapters

**Outcome:** vendor denoising and reconstruction are replaceable adapters over engine-owned
signals, each with a native reference and offline comparison.

**Deliver:** a capability-selected MetalFX denoising adapter consuming M10's noisy signal, guides
and history under the M6.4 adapter rules; a native temporal/spatial denoiser as reference; an
offline Open Image Denoise reference for evidence; source, confidence and rejection views.

**Exit gate:** native and vendor paths replay matching sequences with separate valid histories;
switching and fallback are validation-clean; captures identify the algorithm and inputs; the
adapter does not redefine light, exposure or temporal semantics.

**Defer:** vendor denoising as a required path; frame interpolation, which needs the stable
real-frame timing, frame identity and pacing recorded in Part II.

## N4 — Learned materials, compression and caches

**Outcome:** independently accepted studies decide whether learned materials, compressed
representations or caches improve on the analytic and baked baselines at bounded cost.

**Deliver:** candidates entered one at a time, each with its own oracle and baseline: a neural
residual over the analytic material evaluated in MaterialLab against the BRDF oracle and an
offline reference; neural texture decode compared with the baked block-compressed baseline;
an inference-only irradiance or light-probe volume validated against the M10 transport oracle;
an online-trained radiance cache after M10 on an M5-class device.

**Exit gate:** each candidate reports absolute and relative error, energy behavior, memory,
training accounting and whole-pass cost against its baseline, and closes with adopt, retain or
defer; a result that the analytic or baked path wins is published, not hidden.

**Defer:** broad material-graph coverage, architecture search and a budget-aware compiler until
one approximation shows a useful tradeoff; RT-gated neural geometry until M10 exists.

## Cross-API and hardware policy

| Investment | Unlocks | When justified |
|---|---|---|
| M5-class Mac | Neural-accelerator measurements; third-generation Apple ray tracing | When N1 reaches its acceleration gate, not before N1 starts |
| Rented CUDA GPU | Training at hourly cost | With the first trained consumer |
| Windows GPU host | D3D12 backend validation; a Linear Algebra port of N1 | Only when the D3D12 backend is scheduled under ADR 0007 |

A D3D12 implementation targets the Linear Algebra feature, not the superseded cooperative-vector
proposal, and only after the backend passes checkpoint A. Vulkan remains design evidence.

## Boundaries and deferrals

Generative world models replace the renderer and are out of scope. Black-box vendor networks
enter only as adapters. Splat conformance and hybrid mesh/splat selection stay in Part II's
independent research. No learned pass may become a correctness dependency of scene, material,
temporal or display semantics, and no bespoke IR, operator library or shader compiler is built.
