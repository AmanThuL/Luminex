# Open-source renderers, samples and portfolio projects, 2024–2026

**Status**: Frozen — non-normative  
**Research date:** 2026-09-14

Evidence notebook for the [rendering direction review](../2026-09-14-rendering-direction-review.md),
collected in one web-assisted pass over repositories, release notes and project sites. It records the
2024–2026 delta on top of the [open-source engine survey](../open-source-engines.md) rather than
repeating it. Coverage came mostly from direct reads of known projects rather than a broad sweep, so
absence here is not evidence that a project does not exist. The review reconciles conflicts between
notebooks and takes precedence over any judgment here. Items marked **[UNVERIFIED]** or
**[LOW CONFIDENCE]** were not confirmed against a primary source.

## 1. Table

| Project | Owner | Language/API | Latest activity | Paradigm techniques demonstrated | Runs on Metal/macOS? | License | What Luminex can reuse or learn | Source URL |
|---|---|---|---|---|---|---|---|---|
| **Bevy** | Bevy Foundation | Rust, wgpu | 0.19 (2026-06-19); 0.18 (2026-01-13); 0.17 (2025-09-30); 0.16 (2025-04-24) | GPU-driven rendering (multi-draw-indirect, bindless, GPU culling) since 0.16 (~3x speedup claim); meshlet virtual-geometry renderer with BVH culling (0.17: 2.2ms→1.3ms); **Bevy Solari** experimental ReSTIR DI/GI pathtracer w/ world-space irradiance cache (0.17, "not yet production ready" through 0.19); GPU light clustering ~20x speedup (0.19) | **Limited** — 0.16 notes state D3D12/OpenGL/Metal/WebGPU have "limited or no support" for multi-draw-indirect/bindless; DLSS integration explicitly Vulkan Win/Linux only, "no macOS"; Solari-on-Metal [UNVERIFIED] | MIT/Apache-2.0 | Dated benchmark numbers for a BVH-culled meshlet renderer and a ReSTIR GI roadmap to compare against; a cautionary example of Metal-as-second-class in a cross-API GPU-driven path | https://bevy.org/news/bevy-0-16/ (+0-17/0-18/0-19) |
| **Godot** | Godot Foundation | C++, Vulkan/D3D12/Metal/GLES3 | 4.7 (2026); 4.6; 4.5 (2026-03-19); 4.4.1 (2025-03-26); 4.3 (2024-08-15) | **Native Metal backend** replacing MoltenVK (4.4), claimed "at least as fast as Vulkan and in many cases much faster on Apple hardware"; ubershader fallback for compile stutter (4.4); shader baker giving a measured **20x load-time reduction on Metal/D3D12** (4.5); HDDAGI (SDFGI successor) still **unmerged** draft PR since Dec 2023 (revived May 2026, draft again Aug 2026); Vulkan HW ray-tracing plumbing **merged Jan 2026** (BLAS/TLAS, hit-shader-only, Metal/D3D12 explicitly deferred as placeholder); no mesh-shader support found | **Yes, native**, since 4.4 — the strongest verified native-Metal story of any engine surveyed | MIT | Directly comparable native-Metal RHI case study plus a documented ubershader/shader-baker stutter strategy; HDDAGI's 2.5-year-unmerged draft is a realistic timeline datapoint for ambitious GI rewrites | https://godotengine.org/releases/4.4/ (+4.5/4.6/4.7); https://github.com/godotengine/godot/pull/99119 |
| **O3DE** | Open 3D Foundation (Linux Foundation) | C++, Atom, Vulkan/DX12 | v26.05.0 (2026-05-27); daily commits through 2026-09-08 | No meshlet/virtual-geometry/GPU-driven visibility-buffer evidence found in the Atom Gem tree; nothing new-paradigm substantiated | Unknown; no Metal evidence found | Apache-2.0 | Governance-stable (Adobe/AWS/Epic/Microsoft/Tencent members) but not a technique source for this delta | https://github.com/o3de/o3de |
| **Flax Engine** | Wojciech Figat | C#/C++, DX12/Vulkan | Active | DDGI via **custom software ray tracing** (no confirmed hardware RT); no GPU-driven/meshlet claims found | macOS listed as platform; Metal specifics [UNVERIFIED] | **Source-available, proprietary EULA** (not OSI open source) | Software-RT DDGI is a lighter non-HW-RT GI reference point, but license blocks source reuse | https://flaxengine.com/features |
| **Stride** (ex-Xenko) | .NET Foundation | C#, DX12/Vulkan | v4.3 (2025-11-14) | Clustered forward, light probes, PBR; no GPU-driven/RT/meshlet features found | **No** — no macOS/Metal anywhere in docs/site/CI (Win/Linux/iOS/Android/UWP) | MIT | Low relevance; confirms no paradigm movement through 2025 | https://www.stride3d.net/ |
| **Wicked Engine** | Turánszki János | C++, DX12/Vulkan/**Metal** | features.txt current; devblog post 2026-05-24; graphics retrospective 2024-12-10 | **Visibility-buffer renderer** (32-bit meshlet+primitive IDs, on-demand shading); optional mesh-shader path with the author's own **published negative result**: "in real scenes, the performance was always worse than the vertex-shader based rendering," hurting async-compute parallelism; full GI stack — VXGI, DDGI, Surfel GI, SSGI; hardware RT (DXR/Vulkan RT) for AO/shadows/reflections/path-traced lightmap baking; **Gaussian Splat rendering** listed as an engine feature | **Yes** — Metal listed alongside DX12/Vulkan with Xcode/.xcodeproj macOS/iOS builds | MIT | The single best technique reference: a solo/small-team C++ engine with a real Metal backend, meshlet visibility buffer, a full GI menu, and a citable counter-data-point on mesh shaders in production scenes | https://raw.githubusercontent.com/turanszkij/WickedEngine/master/features.txt; https://turanszkij.wordpress.com/2024/12/10/wicked-engines-graphics-in-2024/ |
| **Fyrox** | Fyrox Foundation | Rust | Active (8,454+ commits) | Nothing paradigm-relevant surfaced; conventional deferred PBR | [UNVERIFIED] | MIT | Low relevance — checked, nothing found | https://github.com/FyroxEngine/Fyrox |
| **Hazel** | Studio Cherno (TheCherno) | C++, Vulkan | **Dormant** — last commit 2023-10-27 | None — inactive through this window | N/A | Apache-2.0 | No longer a relevant 2024–2026 learning reference; drop from an updated survey | https://github.com/TheCherno/Hazel/commits/master |
| **jMonkeyEngine** | community | Java, LWJGL/OpenGL | v3.8.0 | None found — OpenGL-centric | Unknown | BSD | Not relevant to this delta | https://github.com/jMonkeyEngine/jmonkeyengine |
| **RavEngine** *(bonus find)* | RavEngine org | C++23, Metal/DX12/Vulkan | Active | GPU-driven renderer explicitly targeting Metal (macOS 12+/iOS/tvOS/visionOS); self-labeled "early alpha… do not use in serious projects"; no meshlets/RT found | **Yes, explicit target** | Apache-2.0 | Same-language, same-API (C++/Metal) peer worth watching, though pre-production | https://github.com/RavEngine/RavEngine |
| **SynapseEngine** *(bonus find)* | Tamás Petii | C++, Vulkan-only | Active through 2026-09 (736 commits) | Task-shader meshlet culling with dynamic LOD, full bindless architecture, hierarchical frustum/occlusion/cone culling, multi-draw-indirect GPU-driven visibility, virtualized shadow mapping, claims 1M+ entities real-time; uses **xmake** like Luminex | No (Vulkan-only) | AGPLv3 + Commercial | Genuinely Nanite-adjacent technique reference despite zero Metal relevance; same build system | https://github.com/TamasPetii/SynapseEngine |
| **Falcor** | NVIDIAGameWorks | C++/Slang, DX12+Vulkan | v8.0 (2024-08-19); last commit 2025-01-07 — **dormant ~20 months** | Render-graph renderer, unbiased path tracer, differentiable Slang | No | Custom NVIDIA source-available | Render-graph design parallels Luminex's own; treat as historical, not a moving target | https://github.com/NVIDIAGameWorks/Falcor |
| **RTXPT** | NVIDIA-RTX | C++/HLSL, DX12+Vulkan | v1.8.1 (2026-03-05) | NEE-AT, RayCones, SER, ReSTIR DI/GI via RTXDI, NRD ReLAX/ReBLUR denoising, DLSS-RR, OMM; explicitly "derives from Falcor… ported to Donut" | No | NVIDIA RTX SDKs License | Composable ReSTIR+denoise+upscale passes mirror Luminex's declarative render-graph philosophy | https://github.com/NVIDIA-RTX/RTXPT |
| **Donut** | NVIDIA-RTX | C++, DX12/DX11/Vulkan via NVRHI | Active, last commit 2026-08-25 | Thin RHI (NVRHI) + reusable passes/scene graph, explicitly "not a game engine" | No | MIT | Closest philosophical peer to Luminex's own thin-RHI approach | https://github.com/NVIDIA-RTX/Donut |
| **RTXDI** | NVIDIA-RTX | C++/HLSL, DX12+Vulkan | v3.1.0 (2026-09-03) | ReSTIR DI (v1) → ReSTIR GI (v2) → ReSTIR PT (v3, full path-traced indirect resampling) | No | NVIDIA RTX SDKs License | ReSTIR PT is the GI technique to track for a future Luminex GI slice | https://github.com/NVIDIA-RTX/RTXDI |
| **RTXGI** | NVIDIA-RTX | C++/HLSL/Slang, DX12+Vulkan | v2.7.0 (2026-03-01) | v2.0 **dropped DDGI entirely**, replaced by NRC (neural, Tensor-Core-gated) and **SHaRC** (spatially-hashed, vendor-neutral, any DXR GPU) | No | NVIDIA RTX SDKs License | SHaRC needs no ML hardware — the portable world-space GI-caching idea if Luminex explores this | https://github.com/NVIDIA-RTX/RTXGI |
| **NRD** | NVIDIA-RTX | C++/HLSL, API-agnostic | v4.18.0 (readme)/v4.17.3 (release); sample pushed 2026-09-04 | REBLUR/RELAX/SIGMA denoisers plus a new non-AI "SH" mode competitive with DLSS-RR; used in 15+ shipped AAA titles | No | NVIDIA RTX SDKs License | Published perf (REBLUR_DIFFUSE_SPECULAR 2.55ms @ RTX4080/1440p) is citable if Luminex ever adds path-traced GI | https://github.com/NVIDIA-RTX/NRD |
| **RTX Neural Shaders (RTXNS)** | NVIDIA-RTX | C++/Slang, DX12 CoopVec-preview + Vulkan CoopVec | v1.4.0 (2026-07-29) | MLP shader/texture inference-in-shader, SlangPy training, Slang autodiff | No | NVIDIA RTX SDKs License | Confirms CoopVec-based neural shading is real, shipping code via Slang — **no Metal equivalent exists** | https://github.com/NVIDIA-RTX/RTXNS |
| **RTX Neural Texture Compression (RTXNTC)** | NVIDIA-RTX | C++, DX12/Vulkan | v0.10.0-beta (2026-08-04) | Joint MLP-decoder compression, up to 16 PBR channels/material; 32MB raw → 12MB BC7/BC5 → **2.5MB NTC** (~5 bits/texel); 2–4x CoopVec inference speedup on Ada/Blackwell | No | NVIDIA RTX SDKs License | Concrete, citable compression numbers for a future texture-pipeline roadmap note | https://github.com/NVIDIA-RTX/RTXNTC |
| **Neural Radiance Cache (NRC)** | NVIDIA-RTX | C++/HLSL | v0.15.0.0 (2026-07-23) | Tensor-Core world-space radiance cache for indirect lighting, now a standalone open sample | No | NVIDIA RTX SDKs License | Real standalone sample code now, not just a paper | https://github.com/NVIDIA-RTX/NRC |
| **Slang** | shader-slang | Compiler | v2026.17.1 (2026-09-11), near-weekly cadence | CoopVec/CoopMat linear-algebra intrinsics, `neural.slang` stdlib module, autodiff/training; experimental Metal backend covers vertex/fragment/compute/task/mesh shaders (DispatchMesh) — **Metal ray-tracing codegen not yet implemented** (tracking issue #12241) | **Partial/experimental** | Apache-2.0 w/ LLVM exception | Luminex already authors Slang shaders — the Metal-backend RT gap and the CoopVec-has-no-Metal-analog fact both directly affect roadmap feasibility | https://github.com/shader-slang/slang |
| **vk_gaussian_splatting** | nvpro-samples | C++/Vulkan 1.4 | Active 2026 | 5 render paths (raster/RT/hybrid/unscented-transform/billboard-RT), stochastic transparency, sort-free variants | No | Apache-2.0 | Reference if Luminex ever adds a splat viewer mode; sort-free rasterization math is portable even without RT | https://github.com/nvpro-samples/vk_gaussian_splatting |
| **vk_mini_samples** | nvpro-samples | C++/Vulkan | Ongoing (NVIDIA copyright 2024–2026) | ~30 focused samples: mesh_shaders/mesh_task_shaders GPU-driven culling, ray_trace_clusters, `VK_EXT_descriptor_heap` bindless, shader_printf + Nsight Aftermath; **confirmed to NOT contain cooperative-vector/matrix samples** (those live in RTXNS) | No | Apache-2.0 | shader_printf/crash-Aftermath in-shader debug pattern is a good model alongside Luminex's own gputrace tooling | https://github.com/nvpro-samples/vk_mini_samples |
| **vk_device_generated_cmds** | nvpro-samples | C++/Vulkan | ~21 commits | GPU-driven indirect command generation; `VK_NV` vs `VK_EXT` device-generated-commands comparison; state sorting/batching; CAD stress test | No | Apache-2.0 | State-sorting/batching lesson is a direct conceptual analogue for a future GPU-driven-culling stage | https://github.com/nvpro-samples/vk_device_generated_cmds |
| **RTX Mega Geometry (RTXMG)** | NVIDIA-RTX | C++/HLSL, DX12+Vulkan | Active, changelog present | Cluster LOD (pre-baked, VRAM-streamed) + Cluster Tessellation (runtime Catmull-Clark, per-frame AS rebuild), shared TLAS, hierarchical Z-buffer, DLSS-RR — NVIDIA's Nanite-class GPU-driven-geometry reference; **15.5ms/frame @ 4K DLSS-Q on RTX 5090, 56M unique/778M instanced triangles** | No | Open source (LICENSE.txt) | Concrete Nanite-class GPU-driven-geometry architecture to cite in the M7+ roadmap, independent of API | https://github.com/NVIDIA-RTX/RTXMG |
| **vk_lod_clusters** | nvpro-samples | C++/Vulkan (SDK 1.4.341.0+/driver 572.16+) | Active 2026 | Vulkan-only companion to RTXMG: cluster-based continuous LOD, GPU-driven RAM→VRAM streaming; explicitly "inspired by the Nanite rendering system," cites the 2021 GDC Nanite talk | No | Apache-2.0 | meshoptimizer-based cluster-generation pipeline is reusable even without the hardware AS extension | https://github.com/nvpro-samples/vk_lod_clusters |
| **vk_animated_clusters** | nvpro-samples | C++/Vulkan (SDK 1.4.309.0+/driver 572.16+) | Active | CLAS + "cluster templates" for animating meshes without full topology rebuild; RTX 6000 Ada: BLAS build 5.22ms→0.80ms, total 6.61ms→2.32ms for 8.43M animated triangles | No | Apache-2.0 | "Reuse topology, rebuild only vertices" pattern generalizes to any acceleration-structure update strategy | https://github.com/nvpro-samples/vk_animated_clusters |
| **WorkGraphsHelloMeshNodes** | AMD GPUOpen | C++/HLSL, D3D12 | ~2025-11 [LOW CONFIDENCE] | Recursive work graph, 2 compute + 2 mesh nodes rendering a Koch snowflake — minimal mesh-nodes teaching sample; RX7000/9000 preview feature | No | MIT | Purely conceptual reference; D3D12-only | https://github.com/GPUOpen-LibrariesAndSDKs/WorkGraphsHelloMeshNodes |
| **FidelityFX SDK** ("AMD FSR SDK") | AMD GPUOpen | C++/HLSL, D3D12+Vulkan | v2.3.0 "Redstone" [dates LOW CONFIDENCE] | FSR Ray Regeneration 1.2.0 (ML denoiser incl. AO/specular-occlusion), FSR Upscaling(ML) 4.1.1, FSR Frame Gen(ML) 4.0.1, FSR Radiance Caching (Preview, ML path-tracing cache) | No | MIT | Renamed at the v2.x line — a roadmap doc citing "FidelityFX SDK 2.x" should double-check current branding | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK |
| **Brixelizer / Brixelizer GI** | AMD GPUOpen (part of FidelityFX SDK) | C++/HLSL/GLSL compute, D3D12+Vulkan | Shipping since SDK v1.1.0 | Cascaded sparse-SDF "bricks" (64³ voxel grids) in a texture atlas for compute-only ray marching, **no hardware RT dependency**; Brixelizer GI adds screen-space probes + world-space SH irradiance cache for multi-bounce diffuse+specular GI | No (algorithm is hardware-agnostic) | MIT | **Highest-value AMD item for Luminex** — a compute-only, no-hardware-RT dynamic GI architecture directly portable to Metal 4 compute without needing MPS ray tracing | https://gpuopen.com/fidelityfx-brixelizer/ |
| **Cauldron** | AMD GPUOpen | C++, Vulkan/D3D12 | [LOW CONFIDENCE — conflicting fetch data; "Cauldron 2" name unverified] | Rapid-prototyping framework underlying FidelityFX-SPD/SSSR/FSR/FSR2 samples | No | MIT | Don't cite a "Cauldron 2" repo name without a second source | https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron |
| **XeGTAO** | Intel (GameTechDev) | HLSL/DirectX | **Archived 2024-04-22**, read-only | GTAO screen-space AO with bent normals, depth MIP hierarchy, Hilbert-curve/R2 sampling | Algorithm portable (screen-space compute) | MIT | Still a useful reference AO algorithm despite archival; portable to Metal compute | https://github.com/GameTechDev/XeGTAO |
| **XeSS** | Intel | C++/HLSL, DX11/12+Vulkan | SDK 3.0.2 (2026-07-24); public headers relicensed MIT 2026-09-09 | XeSS-SR (DP4a path, SM6.4+), XeSS-FG with **3x/4x multi-frame generation** on Arc, XeLL low-latency | No | Custom; public headers now MIT | Multi-frame-generation approach is relevant if Luminex extends past current TAA/TAAU/MetalFX temporal work | https://github.com/intel/xess |
| **D3D12MeshShaders** (DirectX-Graphics-Samples) | Microsoft | C++/HLSL, D3D12 | Long-standing core sample | Basic mesh + amplification shader geometry pipeline | No | MIT | Canonical mesh-shader sample structure to compare against Apple's Metal 4 mesh-shader LOD sample | https://github.com/microsoft/DirectX-Graphics-Samples |
| **Work Graphs (mesh nodes) spec** | Microsoft (DirectX-Specs) | D3D12 spec | v1.012 (2026-02-04) | GPU-driven recursive node-graph dispatch; mesh-launch nodes emit `DispatchMesh()`-equivalent work; **draw/draw-indexed graphics nodes explicitly "[CUT]" in favor of mesh-node-only experimentation** | No | N/A (spec) | Confirms Work Graphs mesh nodes are still experimental/in-flux as of Feb 2026 — not a stabilized porting target yet | https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkGraphs.md |
| **HLSL Shader Model 6.9** | Microsoft | HLSL spec | Current | Long vectors (5–1024 elements) for ML vector-matrix workloads; mandatory 16-bit/wave/64-bit-int ops; Opacity Micromaps; Shader Execution Reordering | No | N/A | Note: Cooperative Vectors ship as a separate Agility SDK preview layered on SM6.9's long-vector DXIL support, not itself named in the base spec | https://github.com/microsoft/DirectX-Specs/blob/master/d3d/HLSL_ShaderModel6_9.md |
| **DirectSR** | Microsoft | D3D12 API + spec | Spec present [exact date UNVERIFIED] | Unified upscaling abstraction (`IDSRDevice`/`IDSRSuperResEngine`/`IDSRSuperResUpscaler`) over native vendor metacommands, plus an MS extension library for NPU/cross-device SR | No | N/A | Direct analog to Luminex's own `--temporal <taa\|metalfx>` abstraction for two backends — worth comparing API shape | https://github.com/microsoft/DirectX-Specs/tree/master/DirectSR |
| **Vulkan-Samples mesh shading** (mesh_shading, mesh_shader_culling, hpp_mesh_shading, gshader_to_mshader) | Khronos Group | C++/Vulkan | Ongoing | Basic mesh pipeline; task+mesh culling; C++ bindings variant; geometry-shader→mesh-shader migration guide | No | Apache-2.0 | `gshader_to_mshader` is a useful "porting guide" pattern | https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/extensions |
| **Vulkan-Samples tensor_and_data_graph** | Khronos Group | C++/Vulkan | Present in current extensions tree | Vulkan's ML/tensor operations + data-graph pipeline extension sample | No | Apache-2.0 | Closest Vulkan analog to Metal 4's `MTLTensor` + `MTL4MachineLearningCommandEncoder` — worth reading side by side | https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/extensions/tensor_and_data_graph |
| **Vulkan-Samples cooperative-matrix / DGC** | Khronos Group | — | **Not found** — checked full `samples/extensions` (57 folders) and `samples/performance` (25 folders) listings directly | — | — | **[UNVERIFIED/absent]** — DGC is instead covered by nvpro-samples' `vk_device_generated_cmds`; coop-matrix/vec by NVIDIA-RTX/RTXNS | https://github.com/KhronosGroup/Vulkan-Samples |
| **Vulkan-Samples ray tracing** (ray_tracing_basic/extended/invocation_reorder/position_fetch, ray_queries) | Khronos Group | C++/Vulkan | Ongoing | `ray_tracing_invocation_reorder` is Vulkan's Shader Execution Reordering analog; `position_fetch` avoids storing extra vertex data | No | Apache-2.0 | Comparison point for NVIDIA's SER usage in RTXPT | https://github.com/KhronosGroup/Vulkan-Samples |
| **"Running inline ML operations in a shader with Metal 4"** | Apple | Metal 4/MSL, macOS 26 | WWDC 2025 ("Combine Metal 4 machine learning and graphics") | `#include <metal_tensor>` + MetalPerformancePrimitives; `tensor<T,dextents<int,rank>>`; `tensor_ops::matmul2d` inline inference in a fragment shader; **Neural Material Compression demo: 50% texture-memory reduction vs. block compression, no perceived quality loss** | **Yes, native** | Apple sample-code license | Direct API template (`matmul2d`, tensor types) for a future Luminex neural-material-compression pass | https://developer.apple.com/metal/sample-code/ |
| **"Running a machine learning model on the GPU timeline"** | Apple | Metal 4, macOS 26 | Same WWDC25 session | `MTL4MachineLearningCommandEncoder`, CoreML→Metal-package via `metal-package-builder`, `dispatchNetworkWithIntermediatesHeap:`, `MTLStageMachineLearning` barriers enabling parallel non-dependent render/compute; **Neural Ambient Occlusion demo** predicts per-pixel AO from depth+normals | **Yes, native** | Apple sample-code license | Directly relevant template given Luminex already has an AO/lighting pipeline | https://developer.apple.com/metal/sample-code/ |
| **"Adjusting the level of detail using Metal mesh shaders"** | Apple | Metal 4 mesh shaders | Current | Meshlet-based LOD selection via object/mesh shader pair, varying primitive/vertex counts per meshlet | **Yes, native** | Apple sample-code license | Canonical Metal meshlet/LOD pattern for a future GPU-driven-geometry milestone | https://developer.apple.com/documentation/metal/adjusting-the-level-of-detail-using-metal-mesh-shaders |
| **Game-porting samples (HLSL/DXR → Metal IR)** | Apple | Metal shader converter | Current, Metal 4 era | Converts DXR ray-tracing pipelines and ray-query shaders to Metal IR with DXR shader-table emulation; converts HLSL tessellation/geometry/instancing to Metal mesh shading | Yes (via translation) | Apple sample-code license | Apple's own official path for porting NVIDIA/AMD DX12/Vulkan RT sample shaders into a Metal codebase if ever desired | https://developer.apple.com/metal/sample-code/ |
| **MetalFX Frame Interpolation** | Apple | MetalFX, macOS 26 | WWDC25 "Discover Metal 4" (session 205) | `MTLFXFrameInterpolator` + `MTLFXTemporalScaler` — frame interpolation is **new in Metal 4**, joining existing upscaling/denoising | Yes, native | Apple SLA | Directly relevant to Luminex's `--temporal metalfx` path — frame interpolation is a capability not yet used | https://developer.apple.com/documentation/metalfx |
| **Metal 4 command/resource model** | Apple | Metal 4, macOS 26 | WWDC25 session 205 | `MTL4ArgumentTable` (bindless), Residency Sets, `MTL4CommandAllocator`, unified compute encoder (blit + accel-structure commands merged), placement sparse resources, `MTL4Compiler` w/ QoS prioritization | Yes, native | N/A | Validates that Luminex's own argument-table + residency-set RHI design already tracks Apple's Metal 4 idioms closely | https://developer.apple.com/videos/play/wwdc2025/205/ |
| **gsplat** | nerfstudio-project | Python/CUDA | v1.6.0 (main, as of Aug 2026) | CUDA-accelerated 3DGS rasterization/training; sparse 3DGS (active-tile, Jul 2026), multi-GPU dense 3DGS (Jul 2026), NVIDIA 3DGUT integration (Apr 2025); "up to 4x less GPU memory, up to 15% less time" vs. reference impl | **No** — CUDA-only, no CPU/Metal/MPS path found | Apache-2.0 | The reference training/rasterization library the rest of the ecosystem builds on, but not usable on Apple Silicon directly | https://github.com/nerfstudio-project/gsplat |
| **Brush** | Arthur Brussee | Rust, WebGPU (via Burn ML framework) | Active, 1,223+ commits | Full 3DGS **training and rendering** without CUDA; "works on a wide range of systems: macOS/windows/linux, AMD/Nvidia/Intel cards, Android, and in a browser"; author claims training/rendering generally faster than gsplat | **Yes — macOS/Apple Silicon confirmed via WebGPU** | Apache-2.0 | The best cross-platform, Metal-capable splat *training* reference if Luminex ever wants on-device reconstruction, not just playback | https://github.com/ArthurBrussee/brush |
| **PlayCanvas SuperSplat** | PlayCanvas | JavaScript, WebGL/WebGPU | Active | Free, open, in-browser 3DGS inspector/editor/optimizer/publisher | Likely yes via Safari WebGPU/WebGL [not directly confirmed] | MIT | Turnkey browser-based splat viewer/editor to point at rather than build in-engine | https://github.com/playcanvas/supersplat |
| **SOGS** (Self-Organizing Gaussian Splats) | PlayCanvas (fork of Fraunhofer HHI research) | Python/CUDA | **Archived 2025-09-10**, read-only — superseded by `playcanvas/splat-transform` | Compresses 3DGS scenes via self-organizing sort into a PlayCanvas-compatible bundle | No (CUDA-only compressor; consumed by web viewers) | See repo | Compression concept is relevant even though this exact tool is deprecated; check `splat-transform` for the current version | https://github.com/playcanvas/sogs |
| **Nerfstudio** | nerfstudio-project | Python, CUDA | Active (12k★, 2,063 commits) | Modular NeRF/3DGS training studio with web-based visualization | **No** — "You must have an NVIDIA video card with CUDA installed," no Mac/MPS path documented | Apache-2.0 | Ecosystem hub reference, not directly usable on Apple Silicon | https://github.com/nerfstudio-project/nerfstudio |
| **UnityGaussianSplatting** | Aras Pranckevičius | C#/HLSL, Unity | Author called it feature-complete Dec 2023, but commits continued through **Oct 2025** | Real-time 3DGS rendering inside Unity, D3D12/Metal/Vulkan required (no OpenGL/WebGPU) | **Yes** — measured **21.5ms/46fps on Apple M1 Max** (6.1M-splat bicycle scene, Medium quality) vs. 6.8ms/147fps on RTX 3080 Ti | MIT | The only splat project in this survey with a **published Apple-Silicon performance number** — useful as a rough perf anchor | https://github.com/aras-p/UnityGaussianSplatting |
| **MetalSplatter** | scier | Swift/Metal | Active (93 commits, open issues/PRs) | Native Metal 3DGS renderer for iOS/macOS/visionOS; loads PLY/SPZ/.splat; stereo rendering via amplification for Vision Pro | **Yes — native Metal, Apple-first project** | MIT | The single best **Metal-native** reference implementation to study first if Luminex adds splat support | https://github.com/scier/MetalSplatter |
| **KHR_gaussian_splatting** | Khronos Group (Cesium, Niantic Spatial, Esri, NVIDIA, Huawei, Autodesk, Khronos) | glTF 2.0 extension spec | **Ratified** (Complete, Khronos Group) | Standardizes storing 3DGS data (position/rotation/scale/opacity + SH coefficients) in glTF as point primitives, gracefully degrading to sparse point clouds in unsupported viewers | Format-level; any Metal glTF viewer implementing it would work | Khronos spec license | A real ratified interchange format — worth tracking for a future glTF-based splat import path | https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting |
| **niagara** | Arseny Kapoulkine (zeux) | C++, Vulkan | Active, streamed weekly-ish, 33 episodes through 2026 | GPU-driven culling and scene submission, cone/automatic occlusion culling, task/mesh shading, ray tracing integration, bindless textures, meshlet occlusion culling — built from scratch on a public livestream | No | MIT | The canonical, heavily-cited from-scratch GPU-driven-culling reference with a full video walkthrough of every design decision | https://github.com/zeux/niagara |
| **meshoptimizer** | Arseny Kapoulkine (zeux) | C/C++ | v1.2, 4,207+ commits, active | Meshlet building (`meshopt_buildMeshlets` + Scan/Flex/Spatial variants), mesh-shading support (NVIDIA Turing+/AMD RDNA2+), clustered ray-tracing spatial partitioning, vertex/index/meshlet compression, attribute-aware simplification | Library — usable from any Metal C++/Swift project | MIT | The meshlet-generation library nearly every GPU-driven sample in this survey (niagara, Solis, SynapseEngine, light-system) builds on; directly reusable in Luminex's asset pipeline | https://github.com/zeux/meshoptimizer |
| **IDKEngine** | BoyBaykiller | C#/OpenGL | 397★, active through 2026-08 | Wavefront path tracer w/ ray sorting + OIDN denoising, voxel-cone-traced GI, mesh shaders, bindless textures, VRS, FSR2 upscaling, SweepSAH BVH w/ GPU refitting | No — author notes it "doesn't work on Mesa radeonsi or Intel driver"; OpenGL, no Metal | Unlicensed in fetched content | A well-documented single-author GI+path-tracing hybrid with real BVH-construction perf data — good architectural reading even off-platform | https://github.com/BoyBaykiller/IDKEngine |
| **RealEngine** | Zhai Jialong | C++, DirectX 12 | 316★, 1,074 commits, active through 2026-02 | Two-phase GPU-driven occlusion culling (Ubisoft-inspired), "always two indirect DispatchMesh draws per PSO" regardless of mesh/texture variety, mesh shaders, hybrid ray tracing, bindless SM6.6, render-graph architecture w/ automatic barriers, shader-level `Print`/`DrawLine` debug tools | No — DX12 Ultimate required | MIT | A DX12 GPU-driven-rendering architecture reference with a specific, quotable culling-batching strategy | https://github.com/zhaijialong/RealEngine |
| **Solis** | Vovan675 | C++, Vulkan + DX12 | 5★, 104 commits, active | **Nanite-style meshlet pipeline** — continuous LOD, mesh-shader rasterization with compute fallback, two-pass HiZ occlusion culling; hardware-RT DDGI via probe cascades; cascaded shadow maps + optional RT shadows; DLSS via Streamline; FrameGraph architecture; VRAM-budgeted geometry streaming (25 copies of a 10GB scene using 1GB VRAM) | No — Windows-only | [UNVERIFIED] | A compact, well-scoped Nanite+DDGI reference implementation despite low star count — concrete streaming-under-budget numbers are directly relevant to a future geometry-streaming slice | https://github.com/Vovan675/Solis |
| **Lotus-Engine** | Juan Peke | C++, OpenGL 4.6 | 3★, 242 commits | GPU-driven rendering via indirect draws | No — OpenGL, minimal detail published | [UNVERIFIED] | Minor reference; low maturity/adoption | https://github.com/Juanxpeke/Lotus-Engine |
| **Substrate** | Thomas Roughton (troughton) | Swift, Metal+Vulkan | 166★, active through 2026-02 | Cross-platform render-graph rendering system in Swift | **Yes — Metal is a first-class target** | [UNVERIFIED] | A Swift-native render-graph architecture to compare against Luminex's own C++ validating render graph | https://github.com/troughton/Substrate |
| **Render-Tech-Lab** | pasquelin | TypeScript, WebGPU + Three.js TSL | Active through 2026-09-13 | A deliberately evidence-gated R&D lab: GPU-driven indirect draws, meshlets (64–512 tri variants), virtual shadow maps, dynamic GI, temporal upscaling, virtual texturing, render graph, Hi-Z occlusion culling, visibility-buffer deferred shading, geometry streaming — organized as 16 modules with an explicit S0–S5 load-curve benchmark protocol; module 15 (virtualized geometry) has partial physical validation on Emerald Square (149,998 triangles) | Likely yes via Safari WebGPU [not directly confirmed] | MIT | The methodology is the real takeaway: "no major infrastructure is adopted because it's standard… only when a reproducible benchmark proves the current architecture is the limiting factor" — a discipline directly applicable to Luminex's own milestone-gating approach | https://github.com/pasquelin/render-tech-lab |
| **Nyx** | moonlovelj | C++, DirectX 12 | Active through 2026-09-14 | Nanite-style virtualized geometry: mesh shaders (SM6.6), meshlet DAG/BVH, two-pass HZB occlusion culling, visibility buffer, streamed compressed geometry pages; claims **18.9B instanced triangles at 4K/144Hz on an RTX 4070 Ti SUPER** | No — DX12-only | [UNVERIFIED] | A from-scratch Nanite-class research demo with a headline throughput claim worth citing as an upper bound for what a solo dev has achieved | https://github.com/moonlovelj/Nyx |
| **ue5-nanite-macos** | Philip Turner | C++ (UE5 engine fork), Metal | **Archived 2024-08-16**, read-only | Ports UE5's actual Nanite to Metal by replacing 64-bit texture atomics with 32-bit buffer-based atomics; Nanite debug views rendered correctly but caused GPU freezes requiring reboot; author states "I do not plan to finish this project myself" | Yes — the whole point, tested on 32-core M1 Max | MIT | A documented, abandoned-but-instructive attempt at the hardest possible version of this problem (porting Nanite itself, not reimplementing it) — the 64-bit-atomic obstacle is a concrete Metal limitation to plan around | https://github.com/philipturner/ue5-nanite-macos |
| **light-system** | usestemframework | C++, Vulkan 1.2+ (+ Godot importer) | Phase 1 (offline builder) complete, Phase 2 in progress | Standalone Nanite-class virtualized geometry renderer: offline `.vgeo` builder (meshlet gen, hierarchical LOD, page packing) + GPU-driven per-frame pipeline (frustum culling, hierarchical cluster selection, visibility-buffer rasterization, cascaded shadows); vendors meshoptimizer | **Yes, tested on Apple M4 via MoltenVK**: Stanford Dragon (871K tri) ~15.4ms/65fps; generated city (1M tri) ~30.6ms/33fps | MIT | **The most directly relevant portfolio project for Luminex** — a from-scratch Nanite-class renderer with real measured numbers on current Apple Silicon, even though it goes through MoltenVK rather than native Metal | https://github.com/usestemframework/light-system |
| **Chimera (HybridRenderer)** | CadenXc | C++20, Vulkan 1.3 | Active through 2026-08-18, 167 commits | Forward/hybrid/full-RT rendering paths with hardware-RT shadows/AO/reflections/diffuse GI, SVGF denoising, TAA, bindless materials; began as a graduation project | No — Windows-only | MIT | A clean three-tier (raster/hybrid/full-RT) architecture worth studying for how to structure an optional RT upgrade path without forking the renderer | https://github.com/CadenXc/HybridRenderer |
| **DaveH355/clustered-shading** | DaveH355 | C++, OpenGL 4.3 | 22 commits | Tutorial-grade clustered forward shading; measured on integrated GPU: 4,096 lights at 12×12×24 clusters cost ~0.29ms build + 5.15ms assignment + 3.28ms shading | No (algorithm portable) | MIT | The clearest small, readable open reference for how clustered-lighting build/assign/shade costs scale with light count | https://github.com/DaveH355/clustered-shading |
| **Cluster (bgfx)** | pezcode | C++, bgfx | 472★ | Clustered shading implementation with PBR + HDR tonemapping on top of bgfx | Yes, via bgfx's Metal backend | [UNVERIFIED] | A second, more production-shaped clustered-lighting reference alongside DaveH355's tutorial | https://github.com/pezcode/Cluster |

*(67 rows.)*

---

## 2. Bar for a 2026 solo portfolio renderer

The clearest 2024–2026 delta is that GPU-driven virtualized geometry has moved from "one Epic talk"
to a genre a solo developer is expected to attempt. At least four individually authored projects
surveyed here — **Nyx** (DX12, 18.9B instanced triangles claimed), **Solis** (Vulkan/DX12, meshlet
LOD + hardware DDGI + VRAM-budgeted streaming), **light-system** (Vulkan, tested on Apple M4 via
MoltenVK with real ms/frame numbers), and the abandoned-but-documented **ue5-nanite-macos** — treat
Nanite-class rendering as the headline feature of a personal renderer, not a stretch goal. Alongside
that, mesh-shader-based GPU-driven culling (**niagara**, **RealEngine**, **SynapseEngine**) and hybrid
rasterization+hardware-RT GI (**Chimera/HybridRenderer**, Solis's DDGI, IDKEngine's voxel-cone GI)
have become table stakes for a renderer meant to read as "current." A render graph and temporal
upscaling — Luminex's own M4–M6.5 foundation — are now assumed infrastructure in this cohort rather
than differentiators; most of the top projects here have a render graph, and several already ship
TAA/FSR2/DLSS.

Against that bar, Luminex's foundation-heavy first six milestones compare well on *rigor* — a
validating render graph with a `CompiledFrameRecord` observer contract, native TAA/TAAU with a
frozen-constants ADR trail, and dedicated GPU-debugging tooling (render-graph visualizer, capture
tooling) exceed what any of the ~10 solo projects surveyed publish about their own internals. None of
them document an equivalent to Luminex's ADR discipline, portability checkpoints, or its M6.5
display-domain work. But none of that rigor is *visible* in a screenshot or a 30-second clip, and
that is exactly the currency this cohort trades in — niagara's value is inseparable from its 33-episode
video series, Wicked Engine's from its blog retrospectives with citable negative results, and
Nyx/Solis/light-system's from a single headline triangle-count or frame-time number.

The next visible feature that would let Luminex "stand out" in this specific company is a GPU-driven
meshlet path with occlusion culling — the one paradigm technique that appears in nearly every notable
2024–2026 solo project and that Luminex's own roadmap has deliberately deferred past gate B. Given
Luminex is Metal-4-first, shipping even a modest meshlet/visibility-buffer milestone with a measured
frame-time table (the way light-system did on Apple Silicon, or DaveH355 did for clustered lighting)
would be a differentiated, undersupplied data point — genuinely native-Metal GPU-driven rendering
numbers are close to absent from the current open corpus.

---

## 3. Reusable reference implementations by technique

- **GPU-driven culling**: **niagara** (zeux) — the canonical from-scratch Vulkan reference for
  indirect draw submission, GPU frustum/cone/occlusion culling, and bindless textures, with a full
  public video walkthrough of every decision. Pair with **vk_device_generated_cmds** (nvpro-samples)
  for the state-sorting/batching lesson specific to device-generated command buffers.

- **Meshlets**: **meshoptimizer** (zeux) is the library layer nearly everything else in this survey
  is built on (`meshopt_buildMeshlets` + LOD simplification), directly usable from Metal/C++. For a
  full pipeline including streaming and hierarchical LOD selection, **light-system**'s `.vgeo`
  offline builder + GPU-driven per-frame traversal is the closest thing to a Metal-adjacent (via
  MoltenVK) Nanite-class reference with measured numbers.

- **Clustered lighting**: **DaveH355/clustered-shading** — small, readable, with measured
  build/assign/shade timings across light counts; **Cluster** (pezcode, bgfx-based) for a more
  production-shaped variant that already runs through a Metal backend via bgfx.

- **Virtual shadow maps**: no mature dedicated open reference exists outside closed engines (UE5);
  **Render-Tech-Lab**'s module is the most concrete open attempt, evidence-gated and still partial.

- **RT GI**: **NVIDIA RTXDI/RTXGI** (ReSTIR DI→GI→PT, plus the hardware-agnostic SHaRC world-space
  cache) is the deepest, best-documented open reference; for something with no hardware-RT dependency
  at all, **AMD Brixelizer GI**'s compute-only sparse-SDF approach is the most directly portable to
  Metal 4 compute.

- **Neural shaders**: **NVIDIA RTXNS/RTXNTC** (Slang + CoopVec) is the most complete open
  implementation, but CoopVec has no Metal equivalent today; Apple's own **WWDC25 "inline ML
  operations" and "ML model on GPU timeline"** samples (`MTLTensor`, `tensor_ops::matmul2d`,
  `MTL4MachineLearningCommandEncoder`) are the only *native-Metal* neural-shading reference and the
  one to actually build against.

- **Splats**: **MetalSplatter** (Swift/Metal, native) for rendering on Apple platforms; **Brush**
  (Rust/WebGPU) if on-device training/reconstruction on Apple Silicon is ever wanted.

---

## 4. Sources

All URLs fetched or seen during this research, 2026-09-14 (observation date for all entries below
unless noted):

**Engines**
- https://bevy.org/news/bevy-0-16/ , bevy-0-17/, bevy-0-18/, bevy-0-19/ — Bevy GPU-driven/Solari release notes
- https://bevyengine.org/news/ (redirects to bevy.org)
- https://github.com/bevyengine/bevy , /releases , /tree/main/crates/bevy_solari
- https://docs.rs/bevy_solari/latest/bevy_solari/
- https://godotengine.org/releases/4.7/, 4.6/, 4.5/, 4.4/, 4.3/
- https://godotengine.org/blog/ — no GI/Metal-specific posts found
- https://en.wikipedia.org/wiki/Godot_(game_engine)
- https://github.com/godotengine/godot/pull/99119 — Vulkan RT plumbing, Metal deferred
- https://github.com/godotengine/godot/pull/119869, #86267, #122999 — HDDAGI unmerged draft PRs
- https://github.com/godotengine/godot/releases — 4.6-stable (2026-01-26), 4.6.1–4.6.3 patches
- https://github.com/godotengine/godot-proposals (mesh-shader search, no results)
- https://github.com/o3de/o3de , /commits/development , /tree/development/Gems/Atom
- https://en.wikipedia.org/wiki/O3DE — v26.05.0, member list
- https://flaxengine.com/features ; https://github.com/FlaxEngine/FlaxEngine/blob/master/LICENSE.md
- https://www.stride3d.net/ , /features ; https://github.com/stride3d/stride
- https://github.com/turanszkij/WickedEngine ; raw features.txt ; /wiki
- https://turanszkij.wordpress.com/category/devblog/ ; /2024/12/10/wicked-engines-graphics-in-2024/
- https://github.com/FyroxEngine/Fyrox ; https://fyrox.rs/
- https://github.com/TheCherno/Hazel/commits/master — dormant since 2023-10-27
- https://github.com/jMonkeyEngine/jmonkeyengine
- https://github.com/topics/virtual-geometry ; https://github.com/topics/gpu-driven-rendering
- https://github.com/RavEngine/RavEngine ; https://github.com/TamasPetii/SynapseEngine
- https://github.com/google/filament/releases — v1.76.1 (2026-09-09), no GPU-driven/RT/meshlet news
- https://github.com/ConfettiFX/The-Forge — active, Release 1.63 (2025-03-20), testing GPU Work Graphs, note re: possible Codeberg continuation [UNVERIFIED]
- https://github.com/DiligentGraphics/DiligentEngine — mesh-shader and RT tutorials, Metal backend for macOS/iOS/tvOS/visionOS confirmed

**Vendor/research frameworks & samples**
- https://github.com/NVIDIAGameWorks/Falcor — v8.0, dormant since 2025-01-07
- https://github.com/NVIDIA-RTX/{RTXPT,Donut,RTXDI,RTXGI,NRD,RTXNS,RTXNTC,NRC,RTX-Kit,RTXMG}
- https://github.com/NVIDIAGameWorks/RTXGI — confirms v2.0 dropped DDGI for NRC/SHaRC
- https://github.com/shader-slang/slang , /releases — v2026.17.1 (2026-09-11); Metal experimental, RT codegen unimplemented (#12241)
- https://github.com/nvpro-samples/{vk_gaussian_splatting,vk_mini_samples,vk_device_generated_cmds,vk_lod_clusters,vk_animated_clusters}
- https://github.com/GPUOpen-LibrariesAndSDKs/{FidelityFX-SDK,WorkGraphsHelloMeshNodes,Cauldron}
- https://gpuopen.com/fidelityfx-brixelizer/
- https://github.com/GameTechDev/XeGTAO — archived 2024-04-22
- https://github.com/intel/xess , /releases , /commits/main — v3.0.2 (2026-07-24), MIT header relicense 2026-09-09
- https://github.com/microsoft/DirectX-Graphics-Samples
- https://github.com/microsoft/DirectX-Specs (WorkGraphs.md, HLSL_ShaderModel6_9.md, DirectSR)
- https://github.com/KhronosGroup/Vulkan-Samples (samples/extensions, samples/performance)
- https://developer.apple.com/metal/sample-code/
- https://developer.apple.com/documentation/metal/adjusting-the-level-of-detail-using-metal-mesh-shaders
- https://developer.apple.com/documentation/metalfx , /mtlfxframeinterpolator
- https://developer.apple.com/videos/play/wwdc2025/205/ — "Discover Metal 4"
- https://developer.apple.com/videos/graphics-games/ — WWDC25/26 session listing (WWDC26 neural-rendering session titles found but not verified with direct URLs [UNVERIFIED])

**Splats**
- https://github.com/nerfstudio-project/gsplat — v1.6.0, CUDA-only, Apache-2.0
- https://github.com/ArthurBrussee/brush — Rust/WebGPU, macOS/Metal confirmed, Apache-2.0
- https://github.com/playcanvas/supersplat — MIT
- https://github.com/playcanvas/sogs — archived 2025-09-10, superseded by splat-transform
- https://github.com/nerfstudio-project/nerfstudio — Apache-2.0, CUDA-only
- https://github.com/aras-p/UnityGaussianSplatting , /commits/main — Metal/M1 Max benchmark, commits through Oct 2025
- https://github.com/scier/MetalSplatter — native Swift/Metal, MIT
- https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting — ratified extension

**Portfolio renderers**
- https://github.com/BoyBaykiller/IDKEngine
- https://github.com/zhaijialong/RealEngine
- https://github.com/Vovan675/Solis
- https://github.com/Juanxpeke/Lotus-Engine
- https://github.com/troughton/Substrate
- https://github.com/pasquelin/render-tech-lab
- https://github.com/moonlovelj/Nyx
- https://github.com/philipturner/ue5-nanite-macos — archived 2024-08-16
- https://github.com/usestemframework/light-system — Apple M4/MoltenVK benchmarks
- https://github.com/CadenXc/HybridRenderer ("Chimera")
- https://github.com/zeux/niagara ; https://github.com/zeux/meshoptimizer
- https://github.com/topics/{meshlet,render-graph,metal-renderer,virtual-shadow-maps,virtual-shadow-map,nanite,clustered-shading}
- https://github.com/DaveH355/clustered-shading ; https://github.com/pezcode/Cluster [Cluster stats via topic listing only, not independently fetched — UNVERIFIED detail]
- https://github.com/sebbbi — Sebastian Aaltonen's repos (NoGraphicsAPI, OffsetAllocator, LimitedDetail voxel SVO/DAG)

**Education / reference material**
- https://www.realtimerendering.com/ — no 5th edition announced; page last updated 2024-09-02, still promoting the 2018 4th edition
- https://www.jendrikillner.com/ — Graphics Programming Weekly issue 454 (2026-09-06), actively publishing
- https://bartwronski.com/ — fetch only surfaced posts through Jan 2024; recent 2025–2026 activity **[UNVERIFIED]**, likely a stale/partial fetch
- https://lisyarus.github.io/blog/ — active through mid-2026 (e.g. "Can you find a vector not orthogonal to N others?", June 2026); relevant older posts include a CPU rasterizer tutorial series and WebGPU work
- https://c0de517e.blogspot.com/ — Angelo Pesce's old blog, migrated away Dec 2024
- https://www.c0de517e.com/ — new site, active through 2026-09-01 ("Fielty"), relevant posts: "A Taxonomy for Rendering Engines," "Hallucinations on the future of real-time rendering," "Nvidia, DLSS5 and breaking rendering"
- https://alain.xyz/ — Alain Galvan (AMD ray-tracing driver engineer); most recent visible content Sep 2024, nothing 2025–2026 found on render graphs/architecture specifically
- https://gpuzen.blogspot.com/ — insufficient content to confirm or deny a GPU Zen 3; **[UNVERIFIED]**
- https://www.raytracinggems.com/ — not reachable; Ray Tracing Gems III status **[UNVERIFIED]**
- https://github.com/PacktPublishing/Mastering-Graphics-Programming-with-Vulkan — no second edition found, ~2022–2023 publication, 48 commits, no recent update evidence
