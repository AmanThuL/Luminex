# Rendering direction review: shipped practice 2023–2026 and learned rendering

**Status**: Frozen — non-normative  
**Research date:** 2026-09-14

This review answers two questions the owner raised after interface gate B: whether the M7–M11
"main course" in [GPU-Driven Hybrid Rendering](../roadmap/gpu-driven-hybrid-rendering.md) still
reflects what studios and platform vendors ship, and which AI/neural techniques could become
actual experimental features of this renderer. It consumes seven evidence notebooks under
[`2026-09-14-roadmap-review/`](2026-09-14-roadmap-review/) and extends the
[2026-09-06 project-fit assessment](2026-09-06-graphics-paradigm-project-fit.md) and the
[2026-08-09 synthesis](2026-08-09-rendering-pipeline-synthesis.md). The
[roadmap](../roadmap.md) owns milestone identifiers, boundaries and order; section 5 below is a
proposal for that document, not a decision.

## Evidence standard

- **Primary**: a vendor, engine, standards or paper page read during this review. Two items were
  re-read by the review itself: the WWDC26 session
  [Build real-time neural rendering pipelines with Metal](https://developer.apple.com/videos/play/wwdc2026/359/)
  and the [D3D12 Work Lists specification](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkLists.md).
- **Reported**: press, forum or encyclopedia coverage without a primary confirmation. Stated as
  such in the text.
- **[UNVERIFIED]**: carried from a notebook without any confirming source. Recheck before a plan,
  ADR or purchase depends on it.

The notebooks were collected in one web-assisted pass, so absence from them is not evidence of
absence. Where notebooks disagree, this review states the reconciled reading.

## Summary

1. **M7–M11 are current practice, not 2020 thinking.** GPU scene tables, GPU culling with indirect
   submission, HZB occlusion and clustered lighting shipped independently in Anvil, Northlight,
   RE Engine, Dawn Engine, Decima-lineage titles and Unity 6 between 2023 and 2025. RT-first GI is
   the prestige ceiling, not the floor: *Doom: The Dark Ages* requires ray tracing while
   *Elden Ring: Nightreign* ships none. The plan's "RT late, oracle first" shape matches that.
2. **The plan's real gaps are named alternatives, not missing eras.** Stochastic many-light direct
   lighting (MegaLights, HypeHype's tile lighting), cluster geometry as the visible headline of
   2024–2026 solo renderers, VSM certified down to Apple M2 by Epic's own documentation, and
   vendor denoisers/frame interpolation on Metal appear nowhere in the roadmap text.
3. **The visible-portfolio problem is real.** The M4–M6.5 foundation exceeds the published rigor of
   every solo renderer surveyed, but the currency of that cohort is a headline feature with a
   measured number. Native-Metal GPU-driven geometry numbers are nearly absent from the public
   corpus; publishing them is an undersupplied contribution.
4. **Apple now ships a first-party in-shader ML path.** Metal 4 exposes `MTLTensor`, an ML command
   encoder for exported Core ML packages, and MSL tensor operations (`cooperative_tensor`,
   `matmul2d`); WWDC26 demonstrates per-frame in-shader training and a neural tone mapper. The
   neural accelerator that makes this fast exists only in M5 and A19 Pro GPUs; the recorded
   development machine (M3 Max) runs the same code on shader ALUs.
5. **Slang cannot reach that path today.** Slang's cooperative-vector support lowers to SPIR-V and
   DXIL only; its Metal target has no tensor types and no ray-tracing code generation. A Metal
   neural experiment needs a hand-written MSL module beside the Slang pipeline, which is a bounded
   exception to [ADR 0003](../decisions/0003-slang-shaders.md) and needs its own decision.
6. **On PC the neural surface is still preview and still moving.** D3D12's Linear Algebra Matrix
   feature (SM 6.10) supersedes the Cooperative Vector proposal and remains preview; Vulkan has no
   cross-vendor cooperative-vector extension; Work Graphs are reported deprioritized and a
   spec-only Work Lists successor targets 2027. A three-API neural interop benchmark is premature;
   a Metal-first study with a CPU oracle and a later D3D12 LinAlg port is the right shape.
7. **CUDA is a training substrate, not a backend.** Rented CUDA hosts are Linux-only, so they train
   networks but cannot validate D3D12; a Windows RTX box is justified only when the D3D12 backend
   starts. Small networks can also train on the Mac through MLX.

## 1. What shipped and was disclosed, 2023–2026

Details and sources: [studio disclosures](2026-09-14-roadmap-review/studio-and-engine-disclosures-2023-2026.md),
[pipeline state of the art](2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md) and
[open-source references](2026-09-14-roadmap-review/open-source-references-2024-2026.md).

### 1.1 Trends

| Trend | Evidence | Status for a Metal-first solo renderer |
|---|---|---|
| GPU-driven culling and indirect submission are baseline | Dawn Engine Deferred+, Anvil (REAC 2025), Northlight (Digital Dragons/REAC 2024), Ghost of Yōtei tech deep dive (2025), Unity 6 GPU Resident Drawer | Native Metal indirect path exists in the RHI; M7's scope is standard practice |
| Cluster/meshlet geometry shipped beyond Epic | RE Engine meshlets (REAC 2025), Alan Wake 2 mesh shaders (2023), Space Marine 2 (REAC 2025); solo projects Nyx, Solis, light-system, niagara headline it | Mesh shaders need M3/A17 Pro; cluster culling with vertex/compute execution runs everywhere; Wicked Engine reports mesh shaders slower than vertex paths in real scenes |
| Stochastic many-light direct lighting | Epic MegaLights production-ready in UE 5.8 (reported); HypeHype stochastic tile lighting (SIGGRAPH 2025); Unity 7 surface-cache GI announced (Unite Seoul, July 2026) | Clustered Forward+ remains the reference; the stochastic variant with RT shadows needs M10 queries and is not Mac-targeted by Epic |
| RT-GI is the prestige ceiling, not universal | Required: Doom: The Dark Ages; primary: AC Shadows, Spider-Man 2, Star Wars Outlaws, Indiana Jones; absent: Elden Ring: Nightreign; probe/surfel/baked GI still shipped (Veilguard, Overwatch 2, Frostbite GIBS) | A probe floor before one dynamic cache stays correct; Apple hardware RT starts at M3 |
| Full ReSTIR path tracing is research-grade | NVIDIA "ReSTIR PT Enhanced" (2026, reported) frames it as closer to production; shipped PT modes are optional high-end toggles | M10's progressive path-trace oracle is appropriately conservative |
| Visibility buffer adopted by id Tech 8 and Wicked Engine | Doom: The Dark Ages talk (Graphics Programming Conference 2025); Wicked features list | M9's opaque-path experiment has two shipped reference designs |
| ML reconstruction, frame generation and ray reconstruction are default and still changing | DLSS 4 → 4.5 transformer models, PSSR and its Project Amethyst successor (PlayStation Blog, Feb 2026), XeSS 3 MFG, MetalFX Frame Interpolation and Denoising (Metal 4) | Native TAA/TAAU remains the reference; MetalFX adapters are the only vendor path on Metal |
| OIT and transparency moved in production | Activision AVBOIT (SIGGRAPH 2025); no AAA title found shipping MBOIT | Sorted premultiplied transparency remains the right M8 baseline; AVBOIT is a named later study |
| Next engines | UE6 announced June 2026 with Nanite v2 and Lumen 2.0 (early access targeted end of 2027, reported); REX Engine reveal scheduled October 2026 | Direction: more virtualization plus cheaper near-reference GI; nothing here removes a raster baseline |

### 1.2 Verdict per M7–M11 area

| Area | Shipped practice | Roadmap text today | Verdict |
|---|---|---|---|
| M7.1–M7.3 scene tables, culling, HZB | Standard since 2015; Metal ICB is Apple's recommended path | Matches | Keep. Publish native-Metal paired measurements as a first-class deliverable, since the public corpus lacks them |
| M7.4 clustered local lighting | Clustered/tiled is the baseline; stochastic many-light is the 2024–2026 frontier | Clustered only | Keep clustered as the reference; name stochastic direct lighting as the evaluated alternative |
| M8 shadows | CSM/PCSS/atlas remain standard; UE VSM lists Apple M2 as minimum | Cascades, PCSS, atlas; VSM deferred to research | Keep; make a VSM-style page-cached atlas an eligible in-M8 evolution rather than research-only |
| M8 AO/SSR | GTAO-class AO; screen-space first with RT fallback even in RT engines | GTAO + SSR/probe fallback | Keep; XeGTAO archived April 2024, so prefer a visibility-bitmask (VBGTAO) implementation |
| M8 transparency/volumetrics | Sorted transparency, froxel fog, Hillaire sky | Same | Keep; note AVBOIT as later research |
| M9 geometry | Cluster LOD through meshoptimizer `clusterlod.h`; visibility buffer in id Tech 8 | Offline LOD first, meshlets later | Reshape: cluster geometry and the opaque-path experiment become the visible core; mesh shaders are an M3+ optional execution path with a vertex/compute fallback |
| M10 queries and transport | Inline queries preferred; NRD open denoisers; MetalFX Denoising on Metal 4 | Queries, oracle, reflections, native denoiser | Keep; add a MetalFX denoiser adapter beside the native reference, in the M6.4 adapter pattern |
| M11 GI | DDGI-class probes remain the most validatable floor; surfel GI has open ports; radiance cascades unproven in 3D; NRC needs on-device training | Probe floor then one cache | Keep; name surfel GI and SHaRC-style hashed caches as candidates; keep radiance cascades and NRC in research |
| M11 residency | Godot and Bevy have no virtual texturing; Apple's IO queue is native | Ordinary streaming before sparse pages | Keep; anchor on Metal fast resource loading plus a hand-rolled feedback buffer |

### 1.3 The visible-portfolio bar

Every notable 2024–2026 solo renderer surveyed leads with one headline: a Nanite-class cluster
pipeline with a triangle count or frame time (Nyx, Solis, light-system), a livestreamed GPU-driven
build (niagara), or a citable negative result (Wicked Engine on mesh shaders). Render graphs and
temporal upscaling are assumed infrastructure in that cohort. Luminex's ADR trail, portability
checkpoint and display-domain work are stronger than anything those projects publish, and none of
it is visible in a screenshot. The only Apple-Silicon cluster-geometry number in the corpus comes
through MoltenVK (light-system on M4). A native-Metal measurement table for GPU-driven visibility
and cluster geometry is therefore both the next visible feature and a genuine contribution.

## 2. Learned rendering: platform reality

Details and sources: [Apple platform](2026-09-14-roadmap-review/apple-metal4-and-on-gpu-ml.md),
[PC vendors and APIs](2026-09-14-roadmap-review/pc-vendor-and-api-landscape.md) and
[toolchain](2026-09-14-roadmap-review/training-and-inference-toolchain.md).

### 2.1 Apple

Metal 4 offers two non-interchangeable paths, both reachable from the existing RHI:

- **Path A — a model as a GPU-timeline pass.** A Core ML model converted with
  `metal-package-builder` runs through `MTL4MachineLearningCommandEncoder`, synchronized with
  ordinary Metal 4 barriers through the machine-learning stage. Inference only; the network is
  fixed at conversion time and the system, not the app, chooses GPU or Neural Engine. This is the
  path for a larger offline-trained network such as a denoiser or tone mapper.
- **Path B — tensor operations inside a shader.** MSL 4 adds `tensor_handle`, `tensor_inline` and
  `cooperative_tensor` with `matmul2d` from Metal Performance Primitives, callable from compute or
  fragment functions at SIMD-group scope. WWDC26 session 359 demonstrates per-frame training
  iterations of a sky-irradiance network inside this path. Community examples report stale
  headers and specification samples that do not compile as written; treat the surface as early.

Acceleration is a hardware fact. Apple's M5 newsroom text says developers program the per-core
neural accelerators through Metal 4 tensor APIs; a reverse-engineering paper on M4 Max
(arXiv 2606.12765) finds `matmul2d` executing on ordinary shader cores with no matrix datapath.
The recorded development machine is an M3 Max, so today the project can validate correctness,
API overhead and fallback behavior but cannot measure the accelerator. M5 Pro/Max/Ultra launch
dates are reported only [UNVERIFIED]; confirm on Apple's site before purchase. A base M5 device
is sufficient to engage the accelerator path.

MetalFX gained Frame Interpolation and a Temporal Denoised Scaler in Metal 4. The denoiser is a
packaged effect with a ray-tracing-oriented input contract, not a general denoiser; it belongs
beside M10 as a capability-selected adapter. Frame interpolation sits after the display transform
and needs stable real-frame timing and UI pacing, as the roadmap already requires.

**Slang gap.** Slang's Metal target lists no tensor or cooperative-vector support, its
cooperative-vector feature targets SPIR-V and DXIL only, and Metal ray-tracing code generation is
tracked as unimplemented. The SIGGRAPH 2026 neural-shading course repository marks its
cooperative-vector examples as Windows/Linux with NVIDIA hardware. Consequently a Metal tensor
experiment is authored in MSL. The build already keeps readable MSL beside every Slang source
(ADR 0003), so an MSL-authored module is a bounded exception rather than a new toolchain, but it
needs an ADR that names the exception, the fallback and the removal path.

Other relevant Apple facts: `MTLGPUFamily` exposes `apple10`/`apple11` cases without documented
chip mapping; Xcode 26 inspects tensors in the Metal debugger; Intel Open Image Denoise has a
native Metal backend since 2.2 and adds temporal denoising in version 3 (reported), which gives
an offline reference denoiser today; Apple's open-source SHARP and RealityKit's splat support
show where Apple's own neural-rendering investment sits.

### 2.2 PC APIs

| Item | State on 2026-09-14 |
|---|---|
| D3D12 Cooperative Vector (SM 6.9 era) | Preview; the HLSL proposal is marked superseded by the Linear Algebra Matrix specification; AMD's MiniDXNN sample migrated accordingly |
| D3D12 Linear Algebra (SM 6.10) | Preview through Agility SDK 1.721 (May 2026); `VectorAccumulate` completes the op set; Thread/Wave/ThreadGroup scopes are distinct performance models |
| Work Graphs | GA at SM 6.8/6.9 since 2024; a DXC change disables them for SM 6.10; Microsoft is reported (secondary source) to have stopped advancing them for lack of adoption |
| Work Lists | Public spec, "early in development… implementations are not ready… hopefully a preview some time in 2027"; GPU-selected pipeline per draw over a program table, an ExecuteIndirect extension |
| Vulkan | `VK_KHR_cooperative_matrix` ratified but not core; only `VK_NV_cooperative_vector`; `VK_EXT_device_generated_commands` ratified with uneven driver support; Roadmap 2026 mandates none of these |
| Slang | Khronos-hosted with stable governance; cooperative vectors experimental; `neural.slang` module; SlangPy for training bridges |
| NVIDIA | RTX Neural Shaders v1.4, NTC v0.10 beta (its D3D12 path is declared non-shippable), RTXGI v2 replaces DDGI with NRC and SHaRC, RTXDI v3 adds ReSTIR PT, NRD v4.17; no shipped title names NTC or Neural Materials |
| AMD | FSR 4 ML upscaling, Redstone ray regeneration and radiance caching (preview), MIT FidelityFX sources; Brixelizer GI ships in SDK 1.1.x but is absent from the current 2.3 tree |
| Intel | XeSS 3 multi-frame generation; Open Image Denoise with Metal backend |

The conclusion for the earlier "Neural Shader Interop Lab" idea stands and strengthens: the
cross-API surface is preview or vendor-only on every PC API, while Apple's path is shipping but
unreachable from Slang. A useful first result is one device, one network, three execution paths
and a CPU oracle. D3D12 LinAlg follows when a validated Windows host exists (ADR 0007).

### 2.3 Toolchain

A defensible minimal pipeline for the project:

1. Train in PyTorch on a rented CUDA host (tiny-cuda-nn or a plain PyTorch trainer with
   quantization-aware training), or in MLX on the Mac for tiny networks.
2. Export weights as safetensors with a small layout descriptor; pin them like fetched assets
   with provenance and hashes.
3. Load the weights into a CPU numerical oracle in `Source/Core`/`Source/Asset`, the same role
   the offline FLIP tooling plays for images.
4. Run inference in a Metal 4 MSL tensor module or a plain FP16 Slang shader, with the oracle as
   the source of truth and a capability fallback.
5. Port to D3D12 LinAlg only with the D3D12 backend.

Unknowns to verify before code: whether Slang gains any Metal tensor lowering; the exact
`MTLGPUFamily` gating for the accelerator; fragment-stage register pressure of
`cooperative_tensor`; whether any GPU rental exposes Windows/D3D12 (none confirmed). Slang
autodiff is already in the toolchain and supports offline fitting tools with no runtime risk.

### 2.4 Hardware gates

| Purchase or rental | Unlocks | When justified |
|---|---|---|
| Base M5-class Mac | Neural-accelerator measurements for Path B and MetalFX-adjacent ML; third-generation Apple RT | Before the first acceleration measurement, not before the first experiment |
| Rented CUDA GPU (Linux) | Training at hourly cost; tiny-cuda-nn, SlangPy | With the first trained consumer |
| Windows RTX Blackwell box | D3D12 backend validation, LinAlg, DXR 1.2, cooperative vectors on Vulkan | Only when D3D12 backend work is scheduled (ADR 0007) |
| AMD RDNA4 card | Work Graphs/DGC driver comparison, FSR sources | Not justified by current plans |

## 3. Learned techniques ranked for Luminex

Details and sources: [neural techniques](2026-09-14-roadmap-review/neural-rendering-techniques.md).
Ranking weighs reuse of existing contracts, availability of an open training recipe, an oracle
the project already owns, and independence from vendor hardware.

| Rank | Technique | Precedent | Reuses | Oracle | Hardware | Earliest entry |
|---|---|---|---|---|---|---|
| 1 | Tiny-MLP inference lab (fixed network, three Metal execution paths) | RTX Neural Shaders methodology; Apple WWDC25/26 samples | Frame-data path, timing, MaterialLab | CPU numerical oracle | M3 for correctness; M5 for acceleration | Now, as a bounded study |
| 2 | Hybrid analytic GGX plus neural residual material | Real-Time Neural Appearance Models (2023/2024); hybrid neural-microfacet BRDF [UNVERIFIED]; "Taming Optimization Variance" (SIGGRAPH 2026) for small-network training | MaterialLab, BRDF oracle, Lighting module | Analytic BRDF and offline reference (Mitsuba 3) | Any; train on rented GPU or MLX | After rank 1 |
| 3 | Learned reconstruction study against native TAAU | NSRR (2020), ExtraNet (open, maintained), STSSNet (open, ~4 ms claim), FuseSR (open) | Temporal contract, capture sequences as datasets, FLIP comparison | Native TAA/TAAU reference | Training needs days on a 24 GB-class GPU | After rank 1; no M7 dependency |
| 4 | Vendor denoiser adapter plus offline reference | MetalFX Temporal Denoised Scaler; OIDN Metal backend; NRD as open native design | M6.4 adapter pattern, temporal debug views | Native denoiser, path-trace oracle | Metal 4 | With M10 |
| 5 | Neural texture compression decode versus BC baseline | NTC (SIGGRAPH 2023), neural block compression with hardware filtering (2025/2026), Apple's neural material compression sample | TextureBake pipeline, DDS baseline | Existing BC images, PSNR/FLIP | M5 for cost parity | After rank 1 |
| 6 | Slang-autodiff fitting tools (material or probe parameter fitting) | SLANG.D (2023) | Slang toolchain, MaterialLab | Reference images | None | Any time; tooling only |
| 7 | Inference-only neural irradiance/light-probe volume | Neural Irradiance Volume (CGF 2026, 0.19–1.35 ms at 1080p on RTX 4090 reported); Activision Neural Light Grid (2024) | G-buffer-free forward inputs, IBL contract | Path-traced reference (M10 oracle) or offline baker | Any for inference | After M10 oracle or an offline baker |
| 8 | Neural shadow post-filter or soft-shadow synthesis | Neural Shadow Mapping (2022); kernel-predicting shadow maps (2025) [UNVERIFIED] | Shadow stage, PCF/PCSS baselines | Existing shadow references | Any | After M8 shadows |
| 9 | Neural bounds for culling | Neural Bounding (2024) | M7 visibility, CPU oracle | Culling accuracy versus AABB | None | After M7.2 |
| 10 | Online-trained radiance cache (NRC-class) | NRC (2021); mobile NRC variants (2025) | Path-trace oracle, temporal contract | Path-traced reference | M5 for per-frame training | After M10 and rank 1 |

Excluded from the roadmap: generative world models (a separate paradigm that replaces the
renderer; several vendors repositioned them toward robotics), black-box vendor SDKs with no
training recipe (DLSS, FSR 4 models, Ray Reconstruction) except as adapters, RT-gated neural
geometry (N-BVH, neural intersection functions) before M10, and "neural assets" as a term (a 2D
diffusion result). RTX Mega Geometry is classical cluster BVH work, not neural. No verifiable
real-time learned tone-mapping paper was found; the existing exposure pipeline stays the baseline.

Gaussian splatting facts that bear on the existing independent items: `KHR_gaussian_splatting`
is ratified with three.js adoption; SPZ is the de facto compressed interchange; MetalSplatter is
the native-Metal reference; Brush trains on Apple Silicon through WebGPU; SuGaR/Frosting bind
Gaussians to meshes, which is the academic precedent under the hybrid mesh/splat idea. None of
this changes the earlier ranking: conformance remains a separate tool and hybrid selection still
needs M8 transparency and M9 geometry contracts first.

## 4. Premise corrections against the 2026-09-06 assessment

| Premise then | Reading now |
|---|---|
| Metal neural-shader work depends on an Apple inline-ML sample of unknown acceleration | The API path is shipping and demonstrated with training; acceleration is confirmed M5/A19 Pro-only and absent on M4 (paper) and therefore on the M3 Max in use |
| Slang facilities may cover Metal | They do not; Metal tensor code is MSL-only today |
| D3D12 Cooperative Vector is the target for a future backend | Linear Algebra Matrix (SM 6.10) supersedes it; both remain preview |
| Work Graphs need only a compatible compiler/runtime pair | Reported deprioritized; Work Lists is the spec-only successor with a 2027 preview hope |
| NVIDIA's DGC sample is the benchmark to exceed | Unchanged; `VK_EXT_device_generated_commands` driver support is uneven, and Metal ICB already covers the GPU-generated-command case |
| Splat conformance tooling exists at Khronos | Extension now ratified with downstream adoption; the gap analysis stands |
| Hardware is not the deciding disadvantage | Still true for software scope; one specific purchase (an M5-class Mac) now unlocks a measurement that no software change can |

## 5. Proposal for the roadmap

These are recommendations for the owner; the roadmap parts change only through their own edits.

### 5.1 Keep M7–M11 and reshape their text

- **M7.4**: keep clustered Forward+ as the validated reference; add "stochastic direct lighting
  (ReSTIR-DI/MegaLights-class)" as a named alternative evaluated against it, with the RT-shadowed
  variant depending on M10 queries.
- **M8**: name a VSM-style page-cached atlas as an eligible evolution of the local-light atlas
  (Epic lists Apple M2 as the VSM minimum), prefer a visibility-bitmask AO implementation, keep
  sorted transparency, and record AVBOIT as later OIT research.
- **M9**: make cluster geometry the visible core: offline cluster LOD through meshoptimizer, GPU
  cluster culling, and the visibility-buffer/compact-deferred experiment; mesh shaders become an
  optional M3+ execution path with a vertex/compute fallback; a published native-Metal
  measurement table is an exit deliverable. Nanite-class streaming stays research.
- **M10**: add a MetalFX denoiser adapter beside the native denoiser reference; state the
  hardware floor (M3/A17 Pro) and the Slang Metal ray-tracing code-generation gap as a planning
  risk.
- **M11**: name surfel GI and a hashed world-space cache as the cache candidates, keep radiance
  cascades and NRC in research, and anchor residency on Metal fast resource loading with a
  feedback buffer.
- **Hardware floor**: state once that mesh shaders and hardware ray tracing require M3/A17 Pro and
  neural acceleration requires M5/A19 Pro, so each milestone can cite it.

### 5.2 Add a fifth roadmap part: Neural and learned rendering

The existing independent-research paragraph is a placeholder. A part with its own slices gives
the owner's request for actual experimental features a home without extending M7–M11:

| Slice | Outcome | Prerequisites |
|---|---|---|
| N1 — In-shader inference lab | One fixed tiny network runs through a plain FP16 Slang shader, an MSL tensor module and the ML command encoder, agrees with a CPU oracle within tolerance, and reports whole-pass cost with capability fallback; an ADR bounds the MSL exception and a convention pins weights and training provenance | None beyond gate B; acceleration measurement recorded as unavailable until an M5-class device exists |
| N2 — First learned consumer | A neural residual over the analytic material, or a learned reconstruction compared with native TAAU, trained offline with a reproducible recipe and measured against the existing oracle | N1; rented or local training |
| N3 — Learned reconstruction and denoising adapters | MetalFX denoiser adapter and offline OIDN reference beside the native path; learned reconstruction versus TAAU using capture-sequence datasets | N1, M6 contracts; M10 for the denoiser's real input |
| N4 — Compressed and cached representations | Neural texture decode versus the BC baseline; inference-only probe/irradiance volume; online-trained cache after M10 | N1; TextureBake; M10 oracle for caches |
| Cross-API | D3D12 LinAlg port of N1 only | Validated Windows host (ADR 0007) |

Graduation follows the existing rules: frozen target, fallback, budget, oracle and debugging
surface before measurement; `exp/<topic>` branches; only conclusions and adopted code return to
`main`. Only one implementation plan stays active, so N-slices interleave with M-slices rather
than run beside them.

### 5.3 Independent research updates

Rewrite the Work Graphs/DGC sentence to reflect their reported status and Work Lists; add
stochastic direct lighting, VSM and AVBOIT as named studies with their prerequisites; keep frame
generation's pacing requirements and add MetalFX Frame Interpolation as its concrete Metal
candidate; note that splat items are unchanged by the extension's ratification.

### 5.4 Delivery-order option

The accepted order is UX1 → M7.1. Two options after M7.2:

- **Breadth first** (current dependency map): M7 → M8 → M9 → M10 → M11, N1 interleaved early.
- **Visible first**: M7 → N1 → M9 cluster geometry with published measurements → M8 → M10 → M11.

The dependency map already permits M9 before M8's atmosphere work. Visible-first trades earlier
shadow/AO quality for the headline feature the 2026 solo cohort is judged on.

### 5.5 Decisions requested from the owner

1. Adopt a fifth roadmap part for learned rendering, or extend the independent-research section.
2. Approve N1 as the first neural slice and choose the N2 consumer (material residual or
   learned reconstruction).
3. Accept an MSL-authored tensor-module exception to ADR 0003, bounded to N-slices.
4. Confirm the hardware policy: M5-class Mac when N1 reaches its acceleration gate; rented CUDA
   for training; no Windows GPU before D3D12 is scheduled.
5. Approve the M7.4/M8/M9/M10/M11 text reshapes in 5.1 and choose between the delivery orders in
   5.4.

## 6. Primary sources consulted by this review

Notebooks list every source they used. The review itself relied on these primary pages:

- Apple: [Build real-time neural rendering pipelines with Metal (WWDC26 359)](https://developer.apple.com/videos/play/wwdc2026/359/),
  [Optimize custom machine learning operations with Metal tensors (WWDC26 330)](https://developer.apple.com/videos/play/wwdc2026/330/),
  [Combine Metal 4 machine learning and graphics (WWDC25 262)](https://developer.apple.com/videos/play/wwdc2025/262/),
  [Machine learning passes](https://developer.apple.com/documentation/metal/machine-learning-passes),
  [MTLTensor](https://developer.apple.com/documentation/metal/mtltensor),
  [MTL4FXTemporalDenoisedScaler](https://developer.apple.com/documentation/metalfx/mtl4fxtemporaldenoisedscaler),
  [MTLFXFrameInterpolator](https://developer.apple.com/documentation/metalfx/mtlfxframeinterpolator),
  [Apple M5 newsroom](https://www.apple.com/newsroom/2025/10/apple-unleashes-m5-the-next-big-leap-in-ai-performance-for-apple-silicon/).
- Independent Apple measurements: [Rigel, M4 Max tensor path (arXiv 2606.12765)](https://arxiv.org/abs/2606.12765),
  [BaseRT, M5 neural accelerators (arXiv 2607.19438)](https://arxiv.org/abs/2607.19438),
  [example_matmul_metal4](https://github.com/liuliu/example_matmul_metal4).
- Slang: [Metal target support](http://shader-slang.org/slang/user-guide/metal-target-specific),
  [cooperative vectors](https://shader-slang.org/blog/2025/01/30/coop-vec-available/),
  [neural-shading-s26](https://github.com/shader-slang/neural-shading-s26),
  [SlangPy](https://github.com/shader-slang/slangpy).
- Microsoft: [Work Lists specification](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkLists.md),
  [Cooperative Vector proposal (superseded)](https://microsoft.github.io/hlsl-specs/proposals/0029-cooperative-vector/),
  [Agility SDK 1.721 preview and LinAlg](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/),
  [Shader Model 6.10 preview](https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/),
  [Evolving DirectX for the ML era (GDC 2026)](https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-evolving-directx-for-ml-era-on-windows/).
- Khronos: [VK_KHR_cooperative_matrix](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_cooperative_matrix.html),
  [VK_NV_cooperative_vector](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_cooperative_vector.html),
  [Vulkan Roadmap 2026](https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension),
  [KHR_gaussian_splatting](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting).
- NVIDIA/AMD/Intel: [RTX Neural Shaders](https://github.com/NVIDIA-RTX/RTXNS),
  [RTX NTC](https://github.com/NVIDIA-RTX/RTXNTC), [RTXGI](https://github.com/NVIDIA-RTX/RTXGI),
  [RTXDI](https://github.com/NVIDIA-RTX/RTXDI), [NRD](https://github.com/NVIDIA-RTX/NRD),
  [tiny-cuda-nn](https://github.com/NVlabs/tiny-cuda-nn),
  [MiniDXNN v0.4](https://gpuopen.com/learn/minidxnn-v040-interactive-neural-texture-compression/),
  [FSR Redstone](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/),
  [Open Image Denoise releases](https://github.com/RenderKit/oidn/releases).
- Studios and engines: [SIGGRAPH Advances 2024](https://advances.realtimerendering.com/s2024/index.html),
  [SIGGRAPH Advances 2025](https://advances.realtimerendering.com/s2025/index.html),
  [REAC 2024](https://www.enginearchitecture.org/2024.htm), [REAC 2025](https://www.enginearchitecture.org/2025.htm),
  [Doom: The Dark Ages visibility buffer (GPC 2025)](https://static.graphicsprogrammingconference.com/public/2025/slides/visibility-buffer-and-deferred-rendering-in-doom/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf),
  [Dawn Engine Deferred+](https://www.eidosmontreal.com/news/deferred-next-gen-culling-and-rendering-for-dawn-engine/),
  [Anvil in Assassin's Creed Shadows](https://www.ubisoft.com/en-us/game/assassins-creed/news/3aw71nNlR7kZJzoCATuNtm/inside-anvil-the-technology-powering-assassins-creed-shadows),
  [Ghost of Yōtei tech deep dive](https://blog.playstation.com/2025/10/23/ghost-of-yotei-tech-deep-dive/),
  [Upgraded PSSR](https://blog.playstation.com/2026/02/27/upgraded-pssr-upscaler-is-coming-to-ps5-pro/),
  [MegaLights](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine),
  [Virtual Shadow Maps](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine),
  [State of Unreal 2026](https://www.unrealengine.com/news/state-of-unreal-2026-top-news-from-the-show),
  [Unity 7 roadmap](https://unity.com/news/unity-7-roadmap-revealed-at-unite-seoul),
  [Unity GPU Resident Drawer](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/gpu-resident-drawer.html).
- Open implementations: [meshoptimizer](https://github.com/zeux/meshoptimizer),
  [niagara](https://github.com/zeux/niagara), [Wicked Engine graphics in 2024](https://turanszkij.wordpress.com/2024/12/10/wicked-engines-graphics-in-2024/),
  [light-system](https://github.com/usestemframework/light-system), [Nyx](https://github.com/moonlovelj/Nyx),
  [Solis](https://github.com/Vovan675/Solis), [Bevy 0.16](https://bevy.org/news/bevy-0-16/),
  [Godot 4.4](https://godotengine.org/releases/4.4/), [MetalSplatter](https://github.com/scier/MetalSplatter),
  [Brush](https://github.com/ArthurBrussee/brush), [ExtraNet](https://github.com/fuxihao66/ExtraNet),
  [STSSNet](https://github.com/ryanhe312/STSSNet-AAAI2024), [FuseSR](https://github.com/Isaac-Paradox/FuseSR).
- Papers: [Neural Irradiance Volume (arXiv 2602.12949)](https://arxiv.org/abs/2602.12949),
  [Real-Time Neural Appearance Models (arXiv 2305.02678)](https://arxiv.org/abs/2305.02678),
  [Random-Access Neural Compression of Material Textures](https://dl.acm.org/doi/10.1145/3592407),
  [Neural block texture compression (arXiv 2506.06040)](https://arxiv.org/abs/2506.06040),
  [Neural Radiance Caching (arXiv 2106.12372)](https://arxiv.org/abs/2106.12372),
  [Taming Optimization Variance in Compact Neural Shading Networks](https://research.nvidia.com/labs/rtr/publication/bitterli2026taming/),
  [SLANG.D](https://research.nvidia.com/labs/rtr/publication/bangaru2023slangd/),
  [Neural Bounding (arXiv 2310.06822)](https://arxiv.org/abs/2310.06822).
