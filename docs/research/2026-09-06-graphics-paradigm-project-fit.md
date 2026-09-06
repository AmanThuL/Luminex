# Graphics paradigm projects: fit after the editor and graph work

**Status**: Frozen — non-normative

**Date:** 2026-09-06. This assessment uses the implemented M5.4/M5.5 baseline, including the
detached graph window and its lifetime fixes. The [roadmap](../roadmap.md) owns current sequencing;
the proposals below are evidence for that decision, not additional accepted milestones.

## Merged-baseline review

M5.4 and M5.5 are now merged into main. Their final window changes maximize the editor by default
(`--windowed` retains 1280×720) and centre the detached graph on first use. They do not change
RHI execution or the ranking below. Fixed offscreen extents remain necessary for comparable runs;
interactive window size is not a benchmark resolution policy.

Designing the first experiment exposes an additional comparison constraint: public `Buffer`
supports readback but no persistent CPU write interface, while the direct draw API has no explicit
instancing overload. A private Metal host can compare all submission modes with the same mapped
slot storage and binding path, with the unchanged RHI as a correctness anchor. Such native timings
do not measure production RHI overhead or promise M7's speedup; the later production benchmark
must establish that separately. The proposed [design](../specs/2026-09-06-m5.6-gpu-work-submission-design.md)
makes that distinction explicit. This is a refinement of experiment control, not a new RHI decision.

## Recommendation and ranking

Build a GPU work-submission lab first, then consider a bounded neural-shader evaluation. Luminex's
strongest reusable assets are its explicit execution substrate, correctness tests, image-formation
reference, and inspectable frames. Those support a graphics-systems portfolio with publishable
measurements. The five ideas should not become five simultaneous renderer subsystems.

The ranking weighs current code reuse, distance to a useful independently verifiable result,
maintenance burden, and a credible differentiated contribution. It assumes that another machine
or a rented server is available when justified. It does not penalize an idea merely for needing
non-Apple hardware, and it is not a prediction of which paradigm will dominate graphics.

| Rank | Idea from the proposal | Fit now | Useful first result | Main cost that hardware does not remove |
|---|---|---|---|---|
| 1 | #2 GPU Autonomy Benchmark | Very high | Controlled Metal submission/culling break-even report with a CPU oracle | Fair workloads, ICB integration, robust measurement |
| 2 | #1 Neural Shader Interop Lab | High as a bounded study | One tiny network, scalar shader and accelerated path, numerical and full-pass comparison | Execution scope, weight layout, compiler and device capabilities |
| 3 | #5 Neural Material Distiller | Moderate; follows inference evidence | One material function evaluated in MaterialLab against an analytic reference | Sampling/training/export, useful approximation, generalization |
| 4 | #4 Splat Conformance Lab | Low renderer reuse, good standalone scope | Pinned uncompressed fixtures, validator, declared CPU rendering profile | Normative interpretation, reference independence, ecosystem integration |
| 5 | #3 Hybrid Primitive Runtime | Low near-term readiness, high long-term interest | One supplied mesh/splat pair with controlled transitions | Paired assets, appearance equivalence, compositing and error estimation |

If the objective becomes an independent standards tool, splat conformance rises substantially.
If it becomes long-horizon renderer research, hybrid primitives rise. For the next increment of
this repository, the missing dependencies put both behind execution and shader evaluation.

## What the current repository actually provides

| Existing surface | Reusable value | Limit |
|---|---|---|
| [CommandList](../../RHI/Include/RHI/CommandList.h), [indirect arguments](../../RHI/Include/RHI/Indirect.h), [GPU tests](../../Tests/GpuIndirectTests.cpp) | Compute-written arguments, explicit hazards, GPU correctness checks | Each indirect call still encodes one command; no public ICB/batched execution API |
| [Renderer](../../Source/Render/Renderer.cpp), [frame-data decision](../decisions/0010-execution-model-partial-reshape.md) | Retained object model with efficient frame-owned data | Production iterates draw items; GPU addresses are not persistent GPU scene ownership |
| [FrameData benchmark](../../Benchmarks/FrameData/Runner.cpp), [paired driver](../../Tools/Bench/frame_data_paired.py) | Existing discipline for repeatable paired CPU measurements | Must add visibility, GPU preparation, execution, and workload-specific accounting |
| [Render graph](../../Source/Render/RenderGraph.h), [graph model](../../Source/App/GraphNodeModel.h), [layout](../../Source/App/GraphLayout.h) | Pass/resource/barrier inspection and exact retired-frame evidence | No device execution graph, command-processor counter, or causal bottleneck diagnosis |
| [Lighting](../../Shaders/Lighting.slang), [BRDF oracle](../../Tests/BrdfOracle.h), [scene library](../../Source/Engine/SceneLibrary.h) | Analytic PBR and controlled material/image checks | No training, neural weights, or MaterialX compiler |
| [glTF loader](../../Source/Engine/GltfLoader.cpp), [pipeline descriptor](../../RHI/Include/RHI/GraphicsPipeline.h) | Mesh assets and conventional depth-tested raster | Loader accepts triangle primitives; pipeline exposes no configurable alpha blend state |

M5.4/M5.5 make a complex experiment easier to understand and demonstrate. They do not implement
GPU scheduling, mesh shaders, tensor operations, or a benchmark database. The milestone records
also distinguish automated smoke evidence from outstanding manual visual sign-off; this assessment
does not upgrade that evidence into a completed manual review.

## External premises checked

These are dated source observations, not guarantees about a future driver or the repository's
pinned compiler. Recheck the actual feature queries when an experiment starts.

| Premise | Verified qualification and consequence |
|---|---|
| Vulkan 1.4 guarantees cooperative matrices | Incorrect: [Vulkan 1.4's promoted extensions](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_4.html) do not include [VK_KHR_cooperative_matrix](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_cooperative_matrix.html). Query support and supported shapes. |
| SM 6.10 LinAlg is broadly available DX12 functionality | It is a preview feature with device/toolchain requirements; [Microsoft's SDK page](https://devblogs.microsoft.com/directx/directx12agility/) lists the preview separately. [LinAlg](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/) defines Thread, Wave and ThreadGroup scopes; these are not interchangeable performance models. |
| Shader-level neural portability needs a new common IR | [Slang already supports cooperative vectors](https://shader-slang.org/blog/2025/01/30/coop-vec-available/) and has merged a [CoopVec-like path using CoopMat](https://github.com/shader-slang/slang/pull/9512). Test existing support and concrete missing targets before designing an IR. |
| Metal must be left out of neural shader work | Apple supplies an [inline Metal 4 ML sample](https://developer.apple.com/documentation/metal/running-inline-ml-operations-in-a-shader-with-metal-4). API support does not establish hardware acceleration or Slang lowering on a particular device. |
| MaterialX-to-network is an unoccupied niche | [NVLabs neuralappearance](https://github.com/NVlabs/neuralappearance) already trains neural representations of MaterialX/MDL materials. Budget-constrained search is a possible research angle, not established novelty. |
| KHR_gaussian_splatting is still a release candidate | The [live specification](https://raw.githubusercontent.com/KhronosGroup/glTF/main/extensions/2.0/Khronos/KHR_gaussian_splatting/README.md) says ratified; the repository recorded the [status update on September 3](https://github.com/KhronosGroup/glTF/pull/2642). Older indexed RC text is stale. |
| Splat testing would start a new ecosystem | Khronos already tracks [compliance-oriented assets](https://github.com/KhronosGroup/glTF/issues/2562) and exposes splat functionality in its [Sample Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/API.md). Find a specific missing test or independent oracle; this check does not establish a complete official CTS. |
| DGC comparison and memory measurement are absent | NVIDIA's [DGC sample](https://github.com/nvpro-samples/vk_device_generated_cmds) already compares execution strategies and preprocessing costs. Luminex must contribute controlled workloads or additional targets rather than claim the first benchmark. |

Pin Work Graphs separately from neural-shader experiments: a recent [DXC development change](https://github.com/microsoft/DirectXShaderCompiler/pull/8798)
disables Work Graphs for SM 6.10. That change is not evidence of removal from earlier shader
models or every released compiler; it is a reason to record compatible compiler/runtime pairs
instead of assuming that one newest configuration covers every execution model.

## 1. GPU Autonomy Benchmark

This is the shortest route from existing implementation to a distinctive result. It asks a useful
question even on one GPU: at what object size, visible fraction, and material distribution does
moving work generation onto the GPU save more than it costs? Negative results and break-even
regions are valuable outputs.

Start with seeded opaque synthetic geometry, a fixed shading function, and one material before
bounded material bins. Sponza remains a visual regression scene, not the sole stress workload.
Compare CPU direct, CPU indirect, and GPU-written argument paths, then investigate GPU-encoded
Metal ICBs. Apple's [ICB example](https://developer.apple.com/documentation/metal/encoding-indirect-command-buffers-on-the-gpu)
demonstrates GPU-side command encoding; it is not proof that Luminex's pinned Slang compiler and
Metal 4 backend already support that route. Verify them before committing to an abstraction.

The baseline needs special care: zeroing an indirect draw's instance count can avoid geometry
work while leaving every CPU draw-encoding call intact. Repeated geometry also deserves an
instanced/batched control. A deliberately unbatched CPU loop cannot establish that a new API is
better than competent conventional rendering.

Use two separate comparisons:

- Submission only: every path consumes the same predetermined visible set. Hold geometry,
  material evaluation, camera and output constant.
- End to end: include culling, binning/compaction, argument or command generation, preprocessing,
  synchronization, and drawing. Compare visible IDs and final output with a CPU oracle.

Record repeated paired samples, uncertainty, warm/cold behavior, CPU encoding and waits,
preparation and raster GPU time, memory, and the full experiment configuration. Disable editor
and detached-window rendering for headline timing. Validate separately with the debug layer.
Publish unavailable counters as unavailable; logical allocation requests are not measured resident
memory. Extend the existing measurement discipline instead of treating the rolling UI average as
an experiment report.

Mesh/task shaders change geometry processing; DGC changes command generation; Work Graphs change
work scheduling. They are not five interchangeable backends with identical costs. Later studies
must isolate those axes or state the changed algorithm. Compare APIs on the same physical device
where possible; a Metal-on-Apple versus D3D12-on-another-GPU result compares systems, not API
overhead alone. No universal score or claim of an empty benchmark ecosystem is justified here.

The initial experiment can precede temporal reconstruction because it excludes HZB/history and
uses local instance tables. Production IDs, bindless resources and indirect visibility remain
subject to the shared interface gate. Preserve experiment evidence and adopt only useful results.

## 2. Neural Shader Interop Lab

The useful Luminex-shaped version is a shader evaluation lab. Start with one fixed small
Linear/ReLU network, such as 8–32–32–4, one weight format, fixed input data, and a CPU numerical
reference. Compare an ordinary shader implementation with one supported accelerated path, then
evaluate that network inside a representative rendering pass. Do not start by writing a general
IR, operator library, quantization stack, or three-API backend suite.

Separate tensor throughput from usefulness at the shading call site. Workgroup/subgroup
cooperation, invocation divergence, supported shapes, accumulation precision, packing, and
conversion overhead can dominate a tiny network. A compute microbenchmark alone cannot justify
fragment-stage inference. Preserve the same weights, inputs and numerical tolerance, and include
data movement plus the whole consuming pass in the comparison.

The first portability result needs two measured implementations, but the first useful report can
be one-device capability and performance evidence. A Windows adapter may be a small benchmark
host; it need not port the editor. It must not be presented as Luminex's production D3D12 backend.
Native capability probes stay isolated; production shader-language or compilation changes still
respect the binding foundation decisions and require an explicit architectural decision.

Differentiation should come from reproducible cross-target error/cost evidence and actual shading
workloads. The [Slang neural-shading examples](https://github.com/shader-slang/neural-shading-s26/blob/main/README.md)
distinguish Metal-compatible examples from NVIDIA Vulkan cooperative-vector examples; use that
capability distinction rather than assuming a common source already maps every accelerated path.

## 3. Neural Material Distiller

MaterialLab and the BRDF oracle give this idea a useful evaluation home. First approximate one
bounded analytic material function, train offline, and test held-out roughness, angles, lighting,
and grazing configurations. Keep the original analytic shader, a lookup-table/texture alternative
where appropriate, and a rendered comparison. Measure absolute and relative response errors,
energy behavior, memory, and whole-pass cost; image PSNR alone can hide a poor BRDF.

The current GGX shader may already be cheaper than an MLP. If it is, publish that result and stop
that candidate. A more complex procedural or layered material is a later justified workload, not
a reason to first build a complete material graph compiler. Existing training/export tools should
be reused where suitable. Broad MaterialX coverage, architecture search, and a budget-aware
compiler come only after a concrete approximation shows a useful tradeoff.

A server can supply training capacity; it cannot establish shader inference performance on a
different target GPU. Treat training time, export/packing time, runtime cost and amortization as
different measurements. This direction follows the neural-shader feasibility result rather than
requiring an unrelated renderer rebuild.

## 4. Splat Conformance Lab

This can be a worthwhile standalone contribution, but almost all of its distinctive work is new:
schema semantics, covariance/SH evaluation, sorting, reference compositing, and adversarial test
assets. Start with a small uncompressed corpus and a pinned specification revision. Separate
structural validity, normative rendering requirements, numerical tolerances, implementation
choices, and heuristics such as unusually large scales. A warning is not a conformance failure.

Use an independent CPU reference for a declared profile, and avoid generating both fixtures and
expected results through the same unchecked math. Undefined transforms or unsupported extensions
need an explicit category. A useful corpus can be bounded; a trustworthy reference renderer and
community adoption cannot be promised in two weekends. Check existing Khronos tooling and
contribute a demonstrated gap before creating a competing validator or implying official CTS status.

The [current extension](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting)
is ratified. It uses point primitives and display-referred color encodings, with blending before
color-space conversion. Luminex's loader and scene-linear pre-exposed PBR pipeline therefore are
not drop-in implementations of its semantics. Keep the reference tool separate until an explicit
composition contract exists; do not change the renderer's color rules to make a splat demo fit.

## 5. Hybrid Primitive Runtime

This is an interesting longer-term question with the most prerequisites. A meshlet is a grouping
of mesh geometry, not inherently a lower-detail or appearance-equivalent representation. A captured
Gaussian field may encode illumination that a relightable mesh material does not. Distance alone
does not establish an interchangeable representation or a monotonic performance advantage.

The smallest convincing experiment uses one externally supplied, licensed mesh/splat pair,
matched cameras and a declared lighting condition. Sweep the transition manually and measure
screen-space error, memory, GPU time and artifacts. Establish depth ordering, blending, exposure
and temporal behavior before adding an automatic selector. The final selector needs a calibrated
cost/error model, switching hysteresis and costs for residency or conversion.

Buying more hardware does not provide the paired assets or an error oracle. GPU visibility,
geometry controls, transparency and temporal contracts are more useful prerequisites than a
larger GPU. A display-only hybrid demo is possible earlier, but would not yet answer the proposed
runtime-selection research question.

## Hardware and host strategy

Treat hardware as an enabling investment, chosen against a frozen experiment:

1. Use the existing Apple Silicon environment for the first Metal measurements and validation.
2. Add one Windows GPU host for the second execution or neural implementation. Before purchase
   or rental, verify the exact GPU, driver, OS, SDK and required feature query with a minimal sample;
   ensure repeatable runs and capture/debug access. Do not choose a device from a marketing family
   name or assume a server rental exposes the required graphics API.
3. Add another vendor only when the same workload and validation suite run on the first two
   implementations. Cross-vendor coverage brings continuing driver and measurement maintenance.
4. Rent training compute separately if material distillation warrants it; CUDA/model-training
   access and native D3D12/Vulkan graphics access are different requirements. Even training with
   [neuralappearance](https://github.com/NVlabs/neuralappearance) requires its documented Vulkan
   and cooperative-vector support. [NVIDIA's MIG restrictions](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/deployment-considerations.html)
   exclude graphics APIs for most MIG configurations; verify any exception for the exact profile.

Record remote-host sharing, power/clock policy and run variance when controllable, and disclose
unknowns. Hardware cost is not the deciding disadvantage of the neural or hybrid ideas; their
software scope and time to trustworthy evidence are.

## Architectural consequences

The recommendation preserves [ADR 0007](../decisions/0007-d3d12-backend-target.md) and
[ADR 0010](../decisions/0010-execution-model-partial-reshape.md). An isolated Vulkan or native
capability probe is evidence, not a maintained backend or public RHI commitment. A production
backend change needs its own decision and conformance work. Current object ownership, graph
semantics, three-frame retirement, and the scene/display boundary remain the validation floor.

The main change in direction is to bring a small execution experiment forward and remove the
blanket requirement to finish every traditional rendering feature before neural research. It is
not a commitment to implement all five proposals, replace Slang, or grow Luminex into a general
engine before producing a useful result.
