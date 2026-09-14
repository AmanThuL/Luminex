# PC vendor and API landscape, 2024–2026

**Status**: Frozen — non-normative  
**Research date:** 2026-09-14

Evidence notebook for the [rendering direction review](../2026-09-14-rendering-direction-review.md),
collected in one web-assisted pass that updates the premises checked in the
[2026-09-06 project-fit assessment](../2026-09-06-graphics-paradigm-project-fit.md). The review
reconciles conflicts between notebooks and takes precedence over any judgment here. Items marked
**[UNVERIFIED]** were not confirmed against a primary source; recheck them before a plan depends on
them. Relevance judgments are inputs to the review, not roadmap decisions.

## 1. Table

### NVIDIA

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| DLSS 4 (2nd-gen transformer SR, Ray Reconstruction, MFG) | NVIDIA | Jan 2025, CES 2025 | Transformer-model super resolution + frame generation, replacing CNN models | GA | RTX 20–50 series; MFG needs RTX 40/50 | Vulkan/D3D12 via NGX/Streamline plugin; no Metal path | Low — competing-engine evidence only, not portable | https://www.nvidia.com/en-us/geforce/news/ces-2026-nvidia-geforce-rtx-announcements/ |
| DLSS 4.5 (2nd-gen SR v2, Dynamic MFG up to 6x) | NVIDIA | Jan 2026, CES 2026 | Revised transformer model, dynamic multi-frame-gen scaling | GA (SR/MFG); Ray Reconstruction in Early Access Aug 2026 | RTX 20–50 series, current GeForce driver | NGX plugin model, Vulkan/D3D12 only | Low | https://www.nvidia.com/en-us/geforce/news/dlss-4-5-dynamic-multi-frame-gen-6x-2nd-gen-transformer-super-res/ |
| DLSS 4.5 Ray Reconstruction | NVIDIA | Aug 2026, Gamescom 2026 | 2nd-gen transformer denoiser for ray/path-traced output | Early Access → rolling out to ~30 games | RTX GPUs, NGX SDK | n/a | Low | https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/ |
| DLSS 5 | NVIDIA | ~2026, reported at GTC 2026 keynote | Frame-generation model generating whole frames, not just upscaling | Announced/preview [UNVERIFIED — secondary source, not confirmed on nvidia.com] | RTX GPUs, Blackwell emphasized | Vulkan/D3D12 via Streamline | Low | https://www.pcgamer.com/hardware/live/news/nvidia-gtc-2026-keynote-live/ |
| RTX Kit (umbrella) | NVIDIA | Jan 2025 CES; quarterly releases (latest 2026.3, Sep 9 2026) | Suite of neural-rendering SDKs: Neural Shaders, NTC, Neural Materials, Mega Geometry, RTXGI/RTXDI, RTXCR, OMM, NRD, RTX Path Tracing, Streamline | GA (mixed; some components beta) | RTX 20–50 series; several 50-series-optimized | Vulkan (Cooperative Vectors) + DirectX Agility SDK preview path; no Metal | Med — architecture reference for neural-shading roadmap comparison | https://developer.nvidia.com/rtx-kit |
| RTX Neural Shaders SDK (RTXNS) | NVIDIA | v1.0.0 Feb 2025 → v1.4.0 Sep 2026 | Train/deploy small NNs inside shaders via Slang + Cooperative Vectors | GA, actively versioned | Vulkan Cooperative Vectors; D3D12 Cooperative Vectors (Agility SDK preview) | Uses Slang; Slang CoopVec support confirmed still experimental (unchanged from prior finding) | Med — same concept Slang could someday expose to Metal, but no Metal HW path exists | https://github.com/NVIDIA-RTX/RTXNS |
| RTX Neural Texture Compression (RTXNTC) | NVIDIA | Feb 2025 → v0.10.0 Beta (Sep 2026) | Per-material joint NN compression of PBR texture sets, up to 8x smaller than BCn | Beta | RTX GPUs w/ Cooperative Vectors (Tensor Cores) | Vulkan/DirectX only; decode uses coop-vector matmul HW | Low-Med — interesting compression concept, hardware-gated | https://github.com/NVIDIA-RTX/RTXNTC |
| RTX Neural Materials | NVIDIA | Announced Feb 2025 | AI-compressed shader/material code, ~5–8x faster material eval | Notify-only / not yet a public SDK component as of Sep 2026 | RTX GPUs, Cooperative Vectors | Vulkan/DirectX only | Low | https://developer.nvidia.com/rtx-kit |
| RTX Texture Filtering SDK | NVIDIA | 2025 → v1.3 (2026.3) | Stochastic texture filtering sampled post-shading | GA | RTX GPUs | Vulkan/DirectX | Low | https://developer.nvidia.com/rtx-kit |
| RTX Texture Streaming SDK | NVIDIA | Mar 2025 (2025.2) | Tile-based on-demand texture streaming | GA | RTX GPUs | Vulkan/DirectX | Low | https://github.com/NVIDIA-RTX/RTX-Kit/releases |
| RTXGI incl. Neural Radiance Cache / SHaRC | NVIDIA | v2.0 (2025) adds NRC+SHaRC → v2.7 (Mar 2026) | World-space radiance caching (NN-based NRC, hashed SHaRC) replacing probe GI; shipped in Portal RTX, coming to RTX Remix | GA | RTX GPUs; NRC needs Cooperative Vectors/Tensor Cores | Vulkan/DirectX | Med — radiance-caching concepts are portable even without NN acceleration | https://github.com/NVIDIA-RTX/RTXGI |
| RTXDI incl. ReSTIR PT | NVIDIA | v3.0 adds ReSTIR PT, Mar 2026 (2026.2) | ReSTIR-based direct/indirect/path-traced light sampling, effectively unlimited lights | GA | RTX GPUs, DXR/Vulkan RT | ReSTIR algorithm itself is API-agnostic and reimplementable on Metal 4 raytracing | High — directly portable design pattern independent of the SDK | https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md |
| RTX Path Tracing (reference app/SDK) | NVIDIA | ongoing → v1.8.1 (Mar 2026) | Reference path tracer combining RTXDI/RTXGI/NRD/OMM/SER; added public DXR 1.2 support | GA | RTX GPUs, DXR 1.2 | D3D12 (DXR 1.2), Vulkan RT | Med — reference architecture, relevant once a D3D12 backend lands | https://github.com/NVIDIA-RTX/RTX-Kit/releases |
| NVIDIA Real-Time Denoisers (NRD: ReBLUR/SIGMA/ReLAX) | NVIDIA | ongoing → v4.17.3 (Sep 2026) | Low-rpp denoising library for RT signals | GA | Any DXR/VK RT GPU, not NN-gated | Algorithmically portable, open-source-ish; realistically portable to Metal RT | High — non-NN denoising is realistically adoptable on Metal 4 raytracing | https://github.com/NVIDIA-RTX/RTX-Kit/releases |
| RTX Mega Geometry SDK | NVIDIA | Mar 2025 (GDC) → v2.0.0 (Sep 2026); foliage system at GDC 2026 (Witcher 4) | Cluster-based BVH acceleration for ~100x more ray-traced triangles (Alan Wake 2) | GA | RTX GPUs; OptiX 9 exposes it | DirectX/Vulkan/OptiX; conceptually adjacent to mesh-shader-based geometry clustering | Med — cluster-BVH concept relevant to Luminex's GPU-driven roadmap, implementation is vendor-specific | https://videocardz.com/newz/nvidia-releases-optix-9-with-rtx-mega-geometry-and-neural-texture-compression-sdks |
| RTX Character Rendering SDK (RTXCR): RTX Skin, RTX Hair/LSS | NVIDIA | 2025 → v1.4.0 (Sep 2026); Vulkan RTX Hair support added Jan 2026 | Path-traced strand hair (Chiang BSDF + far-field model) using Linear Swept Spheres HW primitive; subsurface skin model | GA | RTX 50-series HW LSS primitive (4th-gen RT Cores); RTX 20–40 software fallback | Vulkan + DirectX (NVRHI/UE5 branch); LSS is a HW primitive, no Metal equivalent | Low-Med — BSDF math portable, HW primitive is not | https://github.com/NVIDIA-RTX/RTXCR |
| Opacity Micromaps (OMM) | NVIDIA | Ada-era, ongoing SDK updates | Encodes per-triangle opacity into a micromap to skip any-hit invocations for masked geometry | GA | RTX 40/50 (Ada+), DXR/VK RT extension | D3D12 DXR extension + Vulkan VK_EXT_opacity_micromap; no Metal equivalent | Med — direct comparison point for Luminex's own CPU/shader-side AlphaMode::Mask coverage | https://developer.nvidia.com/rtx-kit |
| Shader Execution Reordering (SER) | NVIDIA | Ada-era (2022), current on Blackwell | Reorders divergent RT shader invocations for coherence/perf | GA | RTX 40/50 series | D3D12 NVAPI extension + Vulkan VK_NV_ray_tracing_invocation_reorder; vendor-specific, no cross-vendor standard, no Metal equivalent | Low — no cross-API standardization to draw on | https://developer.nvidia.com/rtx-kit |
| Streamline SDK | NVIDIA | ongoing → v2.14.1 (Sep 2026) | Pluggable framework for integrating DLSS/frame-gen/denoisers into a renderer's frame graph | GA | Any D3D12/Vulkan title | D3D12 + Vulkan only | Low — architecture resembles Luminex's own TemporalResolve/VendorTemporalScaler design, informative not portable | https://github.com/NVIDIA-RTX/RTX-Kit/releases |
| Cooperative Vectors — NVAPI/D3D12 | NVIDIA + Microsoft | Jun 2, 2025 (AgilitySDK 1.717.1-preview) | Vector-matrix multiply intrinsics for small NN inference in ordinary shader threads; FLOAT32/FLOAT8_E4M3/FLOAT16/UINT8 types | Preview | RTX GPUs (dedicated driver); Intel Arc B-Series/Core Ultra Series 2 also listed; AMD via preview driver; WARP software fallback | D3D12 preview + Vulkan VK_NV_cooperative_vector; folding toward SM 6.10 "LinAlg" naming | Med — same capability needed if Luminex pursues neural-shader work under a future D3D12 backend | https://devblogs.microsoft.com/directx/cooperative-vector/ |
| Cooperative Vectors — Vulkan (VK_NV_cooperative_vector) | NVIDIA | 2024 proposal → VK_NV_cooperative_matrix_decode_vector added Vulkan 1.4.352, May 2026 | SIMT matrix-vector multiply for small NN eval, distinct from subgroup-wide VK_KHR_cooperative_matrix | Vendor extension (NV), not KHR-ratified as of Sep 2026 — confirmed unchanged | RTX GPUs; NVIDIA beta drivers (596.54 Win / 595.44.08 Linux) required for decode-vector | Vulkan-only NV extension; no Metal analog | Med — shows how far ahead Vulkan/NVIDIA are on the extension surface vs. Metal | https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_cooperative_vector.html |
| Slang Cooperative Vector support (CoopVec) | NVIDIA-origin, now Khronos-hosted Slang | Jan 30, 2025 blog → still experimental in 2026 | Slang emits SPV_NV_cooperative_vector (and SPV_NV_cooperative_matrix2) from a `coopVecMatMul`-style API | Experimental (open compiler issues Q2 2026) — confirms/updates prior finding: still evolving, not stabilized | Requires coop-vector-capable HW (NVIDIA) to accelerate; Slang frontend itself is HW-agnostic | Lowers to SPIR-V (Vulkan) and DXIL (D3D12); no Metal AIR backend for CoopVec today | High — concrete state of Luminex's shading-language direction's NN-shader lowering path | https://shader-slang.org/blog/2025/01/30/coop-vec-available/ |
| RTX Remix | NVIDIA | ongoing, updates at GDC 2026 | Modding platform for path-traced remasters of classic games, hosts RTXGI NRC integration | GA/open-source runtime | RTX GPUs, remastering D3D8/9 fixed-function games via Vulkan | Vulkan-based runtime | Low — not applicable to Luminex's own scope | https://www.nvidia.com/en-us/geforce/news/gdc-2026-nvidia-geforce-rtx-announcements/ |
| Blackwell architecture (RTX 50 / RTX PRO Blackwell) | NVIDIA | Jan 2025 (RTX 50 launch) | 4th-gen RT Cores (Triangle Cluster Intersection engine, LSS support), 5th-gen Tensor Cores (FP4/FP6, Cooperative Vector HW) | GA | RTX 5070–5090 consumer, RTX PRO 4000–6000 Blackwell workstation | N/A (hardware) | Med — sets the hardware bar for a "neural-rendering-ready" GPU purchase | https://hothardware.com/reviews/nvidia-rtx-blackwell-architecture-overview |
| GTC 2026 path-tracing performance claims | NVIDIA | Mar 2026, GTC keynote | Jensen Huang claimed ~10,000x path-tracing perf gain vs. pre-RT, roadmap toward "1,000,000x" via neural rendering | Marketing claim, not independently benchmarked [UNVERIFIED figure] | N/A | N/A | Low — directional marketing only | https://www.tomsguide.com/computing/gpus/nvidia-says-pc-gaming-will-look-like-a-film-how-gpus-will-get-to-1-million-times-better-path-tracing-and-why-its-closer-than-you-think |
| vk_device_generated_cmds sample (nvpro-samples) | NVIDIA | Ongoing repo; no material 2026 update found beyond a Feb 2025 forum perf thread | Compares reused/threaded/GPU-generated command-buffer strategies via VK_NV/EXT_device_generated_commands | Sample code over a GA extension | RTX GPUs, Vulkan DGC extension | Confirms prior finding unchanged: sample still compares strategies | Med — direct precedent for Luminex's own indirect-draw/GPU-driven roadmap comparison work | https://github.com/nvpro-samples/vk_device_generated_cmds |

### AMD

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| FSR 4 (Upscaling) | AMD | Shipped w/ RX 9070/9070XT (Mar 2025); GPUOpen release blog Aug 20, 2025 | ML transformer-based spatial/temporal upscaler, successor to FSR 3.1 analytical upscaling | GA (FSR SDK 2.0; technique v4.1.1 in SDK 2.3, Jun 2026) | Initially RDNA4-only; D3D12, Windows 10/11 | D3D12 confirmed; no Vulkan/Metal mention in GPUOpen docs | Med — technique pattern portable in concept; Luminex already has native TAAU+MetalFX | https://gpuopen.com/learn/amd-fsr4-gpuopen-release/ |
| FSR Upscaling 4.1 on RDNA3 | AMD | FSR SDK 2.3 update Q2 2026; Adrenalin 26.6.2 driver | ML-based FSR4 upscaling extended to RDNA3 (RX 7000) dGPUs, previously RDNA4-exclusive | GA | RDNA3 dGPUs, D3D12, Adrenalin 26.6.2+; INT8 path supported on RDNA3 as of 4.1.1 | D3D12 only per sources | Low-Med — shows ML upscalers can run on older ML-accel hardware | https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/ |
| FSR Redstone (umbrella) | AMD | Dec 10, 2025, GPUOpen blog | Neural-rendering initiative bundling ML Upscaling, Frame Generation, Ray Regeneration, Radiance Caching | Mixed: Upscaling/FrameGen/RayRegen GA, Radiance Caching technical preview (2026 prod target) | RDNA4 full ML support; RDNA3.5 and earlier get analytical fallback for Upscaling/FrameGen | D3D12 confirmed; Vulkan/Metal not mentioned | Med — the 4-technique bundle is a useful neural-rendering reference architecture | https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/ |
| FSR Frame Generation 4.0.1 | AMD | Bundled in FSR SDK 2.3 (mid-2026) | Neural frame interpolation using optical flow + motion vectors | GA | RDNA4 (ML path); older RDNA get non-ML analytical FG | D3D12; no Metal/Vulkan info found | Low — Luminex has no frame-gen feature yet | https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/ |
| FSR Ray Regeneration 1.2 | AMD | FSR SDK 2.3 (2026) | Standalone ML denoiser for ray/path-traced inputs, engine-agnostic | GA (v1.2.0) | RDNA4 preferred; D3D12 | D3D12; no Vulkan/Metal confirmation | Med — an ML-denoiser pattern relevant vs. hand-tuned SVGF-style denoising | https://gpuopen.com/amd-fsr-rayregeneration/ |
| FSR Radiance Caching | AMD | Announced Dec 2025 with Redstone; v0.9.0 preview in SDK 2.3 | Online ML model predicting multi-bounce GI light propagation from sparse path-traced samples, no baking | Technical preview (GA targeted later 2026) | RDNA4-class ML accel; D3D12 | D3D12 only | High — directly relevant to Luminex's planned GPU-driven hybrid GI roadmap (M7+) as a reimplementable technique | https://gpuopen.com/amd-fsr-radiancecaching/ |
| FidelityFX SDK v2.0 | AMD | ~Aug 2025 | Major SDK rework bundling FSR4 with FidelityFX; prebuilt/signed DLL loader model | GA | D3D12 (older FidelityFX techniques historically had Vulkan support too) | Loader/DLL model is D3D12-centric | Med — SDK packaging pattern (loader + technique DLLs) is a useful RHI-agnostic-effect reference | https://gpuopen.com/amd-fidelityfx-sdk/ |
| FidelityFX SDK v2.3 | AMD | Jun 2026 | Current SDK: FSR Upscaling 4.1.1, Frame Gen 4.0.1, Ray Regen 1.2.0, Radiance Caching 0.9.0 (preview), legacy FSR 2.3.4/3.1.5/3.1.6 | GA (current as of report date) | D3D12, UE5 plugin | D3D12 primary | Low-Med — version tracking | https://gpuopen.com/amd-fidelityfx-sdk/ |
| FidelityFX Brixelizer / Brixelizer GI | AMD | GDC 2024 talk + SDK 1.1 (Jul 2024) | Compute-based real-time dynamic GI using sparse distance fields (SDF), software RT without HW RT | GA (v1.0.1 GI, still shipped) | No hardware RT required — pure compute; D3D12/Vulkan, MIT-licensed HLSL/GLSL source | Fully portable — MIT source, no RDNA-ML dependency; most portable AMD GI technique found | High — no-HW-RT SDF GI directly reimplementable in Metal 4 compute, genuine open reference implementation | https://gpuopen.com/fidelityfx-brixelizer/ |
| GPU Work Graphs + Mesh Nodes (D3D12, AMD driver) | AMD/Microsoft | Preview: Adrenalin 24.9.1 (Sep 2024); full driver support Adrenalin 24.10.30.01 (Oct 2024) | Draw calls as native work-graph nodes, executed parallel to compute nodes | Preview at driver level (see Microsoft section: Microsoft deprioritized SM6.10 Work Graphs, Sep 2026) | RDNA3 (RX 7000) and RDNA4 (RX 9000); D3D12 Agility SDK | AMD also published a parallel Vulkan mesh-nodes-in-work-graphs article | High — one of the only vendors with real driver-level Work Graphs mesh-node support to benchmark, though now a deprioritized MS feature | https://gpuopen.com/learn/work_graphs_mesh_nodes/work_graphs_mesh_nodes-intro/ |
| GPU Work Graphs mesh nodes (Vulkan, AMD) | AMD | GPUOpen dev article (2024–2025) | AMD's Vulkan-side mesh-nodes-in-work-graphs writeup, mirroring the D3D12 feature | Status parity with D3D12 article [UNVERIFIED exact VK extension name] | RDNA3/4, Vulkan extension(s) | A genuine cross-API (D3D12+Vulkan) work-graphs story from one vendor | Med-High — reference for what a fair cross-API GPU-driven comparison looks like | https://gpuopen.com/learn/gpu-workgraphs-mesh-nodes-vulkan/ |
| RDNA4 architecture — 3rd-gen Ray Accelerators | AMD | Feb 28, 2025 press release; RX 9070 launch Mar 2025 | 3rd-gen RT accelerators: ~2x RT throughput vs RDNA3, dual ray intersection, BVH compression, OBB node support | GA (shipped) | RDNA4 silicon only (4nm TSMC) | Hardware feature exposed via standard DXR / Vulkan RT extensions | Med — informs what a purchased AMD GPU exercises for RT-heavy work | https://www.amd.com/en/newsroom/press-releases/2025-2-28-amd-unveils-next-generation-amd-rdna-4-architectu.html |
| RDNA4 architecture — 2nd-gen AI Accelerators | AMD | Feb 28, 2025 | Matrix/AI accelerator units add FP8/INT4 support, better scheduling; up to 2x AI perf vs RDNA3 (8x with sparsity) | GA | RDNA4 silicon (RX 9070 series) | Exposed via D3D12 cooperative vectors / WMMA-style intrinsics and Vulkan coop-matrix where driver supports it; FP8/INT4 formats align with neural-shading quantization | High — the hardware class needed to exercise cooperative-vector/coop-matrix work if buying an AMD card for D3D12/Vulkan experiments | https://www.tomshardware.com/pc-components/gpus/amd-rdna-4-radeon-rx-9000-series-gpus-revealed-targeting-mainstream-price-and-performance-with-improved-ai-and-ray-tracing |
| RDNA5 / UDNA — unified architecture plan | AMD | Ongoing statements since 2024; still unconfirmed as of Sep 2026 | Stated intent to merge consumer RDNA and datacenter CDNA into one "UDNA" line | Roadmap/rumor — no official name/spec; conflicting reports place next-gen Radeon Q2 2026–2H 2027/2028 [UNVERIFIED specifics] | Unknown | Unknown | Low — too speculative to plan against; monitor only | https://www.igorslab.de/en/amd-rdna5-architectural-restart-or-strategic-tour-de-force/ |
| Adrenalin 26.3.1 / 26.6.2 driver releases | AMD | Mar 2026 / Q2 2026 | Windows driver releases carrying FSR4.1.1/RDNA3 support and Redstone updates | GA | Windows driver | N/A | Low — versioning detail | https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-26-3-1.html |

### Intel

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| XeSS 2 | Intel | Jan 2025, CES | 2nd-gen SR SDK adding XeSS Frame Generation and XeSS Low Latency alongside SR | GA, SDK 2.x | Native ML path needs Intel Arc (XMX); FG/LL work on non-Intel GPUs (AMD RX 6000+, NVIDIA RTX 30/40/50) via DP4a fallback, SM 6.4 | D3D12/Vulkan game-integrated SDK; no Metal port | Med — FSR4/DLSS4 comparison point, not a Luminex target itself | https://www.intel.com/content/www/us/en/developer/articles/technical/xess2-whitepaper.html |
| XeSS-FG (Frame Generation) | Intel | Apr 2025, XeSS 2 SDK 2.1.0 | Neural intermediate-frame interpolation | GA | SM 6.4-class GPU; engine-supplied motion vectors/depth | Integration shape mirrors DLSS-FG/FSR3-FG | Med — same integration pattern Luminex's temporal stack would need if frame-gen were added | https://videocardz.com/newz/intel-announces-xess-2-with-xess-frame-generation-and-xess-low-latency |
| XeLL (Xe Low Latency) | Intel | 2025, with XeSS 2 | Input-to-photon latency reduction, analogous to Reflex/Anti-Lag | GA | Intel + non-Intel GPUs | n/a | Low — no Luminex equivalent need yet | https://www.windowscentral.com/gaming/pc-gaming/intel-xess-2-now-available-with-frame-generation-low-latency |
| XeSS 3 / Multi-Frame Generation (MFG) | Intel | Announced Oct 2025; shipped in Arc drivers Jan 27, 2026 | Extends MFG (2x/3x/4x) to all XeSS 2 titles without code changes | GA (driver-level) | Any Intel Core Ultra CPU or Arc GPU from Alchemist onward | D3D12/Vulkan; no Metal | Low-Med — evidence of vendor convergence toward MFG as default | https://www.tomshardware.com/pc-components/gpu-drivers/intel-enables-xess-3-multi-frame-generation-in-latest-drivers-expanding-frame-generation-across-arc-gpus-and-core-ultra-igpus-mfg-can-be-enabled-across-any-title-with-xess-2-support |
| Xe2 "Battlemage" (Arc B-series) | Intel | Dec 2024 launch | Discrete GPU, 2nd-gen RT engine: 20 Xe2 cores/RT units, 160 XMX engines (B580) | GA | PCIe discrete card, Arc driver stack | n/a (hardware) | Low — informative for a potential PC rig purchase only | https://www.intel.com/content/www/us/en/newsroom/news/intel-launches-arc-b-series-graphics-cards.html |
| Xe2 RT unit improvements | Intel | Dec 2024 | Each RTU: 16KB BVH cache, 3 traversal pipelines (up from 2), 18 box intersections/clock (+50%) | GA | Battlemage/Lunar Lake only | n/a | Low | https://www.hwcooling.net/en/batttlemage-details-of-intel-xe2-gpu-architecture-analysis/ |
| Xe3 (Panther Lake integrated GPU) | Intel | Jan 2026, CES 2026 | 3rd-gen integrated GPU, up to 12 Xe3 cores (8 vector + 8 matrix/XMX each) + 1 RT unit; up to 96 XMX units, 12 RT engines; >50% perf vs Lunar Lake | GA | Panther Lake (Core Ultra 300) mobile SoC, Intel 18A | n/a | Low — Intel iGPU trajectory only | https://videocardz.com/newz/intel-details-xe3-gpu-architecture-for-panther-lake-up-to-12-xe-cores-and-50-performance-vs-lunar-lake |
| "Celestial" discrete GPU (Xe3P) | Intel | Reported mid-2025–2026 | Next planned discrete Arc generation | [UNVERIFIED] Reportedly cancelled for consumer gaming per multiple outlets; Xe4 "Druid" (2027) fate also uncertain | n/a | n/a | Low | https://www.tomshardware.com/pc-components/gpus/intel-has-reportedly-killed-discrete-gaming-gpus-for-the-upcoming-xe3p-arc-celestial-family-gaming-gpu-remains-uncertain-even-for-the-next-gen-xe4-druid-lineup-that-lands-in-2027 |
| Intel–NVIDIA partnership (x86 RTX SoCs) | Intel/NVIDIA | Sep 2025 announced; products expected late 2026 | Intel to build x86 SoCs integrating NVIDIA RTX GPU chiplets via NVLink; $5B NVIDIA investment in Intel | Announced/in development | x86 SoC + NVLink-attached RTX chiplet | Signals RTX-class HW reaching Intel-CPU laptops | Low — background industry context, no direct API relevance | https://nvidianews.nvidia.com/news/nvidia-and-intel-to-develop-ai-infrastructure-and-personal-computing-products |
| Open Image Denoise (OIDN) 2.0 | Intel | Jul 2023 | Cross-vendor GPU denoising library: SYCL (Intel), CUDA (NVIDIA), HIP (AMD) backends plus CPU | GA, v2.0 | Intel Arc/Xe (Alchemist+), CUDA-capable NVIDIA, HIP-capable AMD | Cross-vendor via separate compiled backends per API, no single shader IR | High — a cross-vendor-authored denoiser Luminex could adopt offline (see next row for the direct Metal path) | https://www.phoronix.com/news/Open-Image-Denoise-2.0 |
| OIDN 2.2 — Metal backend added | Intel | 2024 | Native Metal device support for Apple Silicon GPUs (macOS Ventura+) | GA, v2.2.0 | macOS Ventura+, Apple Silicon Metal-capable GPU | Runs natively on Metal — directly usable from a Metal 4 app, no CUDA/SYCL/HIP needed | High — the one Intel technology directly consumable by Luminex today (offline/temporal-lab denoising, GPU capture comparisons) | https://www.techpowerup.com/318651/intel-open-image-denoise-v2-2-adds-metal-support-aarch64-improvements |
| OIDN 2.3 — Metal API maturity | Intel | 2025 | Adds `oidnIsMetalDeviceSupported`, async CPU execution, lazy module loading, Metal buffer edge-case fixes, non-Apple-Clang CMake fix | GA, v2.3.0/2.3.2 | Same Metal requirement as 2.2; also adds Arrow Lake/Lunar Lake/Battlemage Xe backends | Metal path now hardened for production robustness | High — directly usable and now production-hardened for a Metal-only engine | https://github.com/RenderKit/oidn/releases/tag/v2.3.0 |
| Open Image Denoise 3 (upcoming) | Intel | HPG 2025 talk; CG Channel report Jan 2026 | Next-gen neural network architecture for higher quality/perf; adds **temporal denoising** (multi-frame accumulation-aware) | Preview/research, not released as of 2026-09-14 [UNVERIFIED release date] | Expected to carry forward CPU/SYCL/CUDA/HIP/Metal backends | If Metal backend is retained (likely), temporal denoising could pair with Luminex's own TAA/TemporalHistory for reference-quality comparisons | High — directly overlaps Luminex's temporal/TAAU domain, worth tracking for `docs/guides/temporal-comparison.md`-style offline references | https://www.cgchannel.com/2026/01/open-image-denoise-3-will-support-temporal-denoising/ |
| Intel resampled importance sampling / path-tracing research | Intel | SIGGRAPH 2025 | Research improving real-time path-tracing sampling/denoising quality (RIS enhancements) | Research paper | n/a | Algorithmic, API-agnostic — could inform any renderer's ReSTIR-style importance sampling | Med — algorithmic ideas portable to Luminex's own future path-tracing/ReSTIR work regardless of vendor SDK | https://www.intel.com/content/www/us/en/developer/articles/news/gpu-research-generative-ai-update.html |

### Microsoft DirectX / HLSL

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| Shader Model 6.10 | Microsoft | Apr 27, 2026 devblog (AgilitySDK 1.720-preview) | New SM adding linalg::Matrix, Group Wave Index, Variable Group Shared Memory, new RT intrinsics (TriangleObjectPositions, ClusterID) | **Preview**, not GA (unchanged since 2026-09-06 baseline) | AMD RX 7000/9000 (varies), Intel Arc B-Series (limited), NVIDIA all RTX (dev-relations driver access) | No direct Vulkan/Metal equivalent yet; conceptually parallel to VK cooperative matrix/vector work | High — D3D12 is Luminex's planned 2nd backend; SM6.10 previews the neural/matrix path Luminex would need to mirror | https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/ |
| AgilitySDK 1.721-preview | Microsoft | May 28, 2026 | Adds VectorAccumulate (completes LinAlg op set for SM6.10 preview), Partial Programs, GUID Texture Layout, UAVs of Depth | Preview (DXC 1.10.2605.4) | Same as SM6.10 preview | n/a | Med — incremental API surface to watch before backend work starts | https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/ |
| **Work Graphs deprioritized for SM 6.10** | Microsoft | **Sep 4, 2026** | Microsoft is not advancing Work Graphs into SM6.10 due to low game-developer adoption and weak driver/GPU implementations; base Work Graphs remains functional on SM6.8/6.9 | Deprioritized (base Work Graphs 1.0 was GA at AgilitySDK 1.613.0 in 2024, frozen at SM6.9) | N/A | N/A | High — updates the 2026-09-06 baseline; Luminex should not invest further in Work Graphs as a forward D3D12 GPU-driven path | https://www.ginjfo.com/actualites/composants/cartes-graphiques/directx-12-work-graphs-est-mis-de-cote-faute-dadoption-dans-les-jeux-20260904 |
| Work Lists (Work Graphs successor concept) | Microsoft | Spec published ~Sep 2026 | New feature: GPU selects a different PSO per draw/dispatch/RT-dispatch from a GPU-resident program table; an extension of ExecuteIndirect, not the Work Graphs node model | Spec-only, pre-preview ("work in progress, not in preview yet"); possible preview ~2027 | TBD | TBD; no Vulkan/Metal equivalent announced | Med-High — likely successor GPU-driven primitive Luminex should track before committing to Work-Graphs-style designs | https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkLists.md |
| D3D12 Cooperative Vector | NVIDIA/Microsoft | Jun 2, 2025 devblog (AgilitySDK 1.717.1-preview) | HLSL vector-matrix ops for per-pixel/thread ML inference (NTC, neural materials, etc.); `linalg` namespace types (FLOAT32/FLOAT8_E4M3/FLOAT16/UINT8) | Preview | NVIDIA all RTX (dedicated driver), Intel Arc B-Series/Core Ultra Series 2, AMD via preview driver, WARP software fallback | Parallels VK_NV_cooperative_vector; Slang has a CoopVec path already noted in prior assessment | High — direct analog to what a Luminex D3D12/Metal neural-texture-or-material path would need | https://devblogs.microsoft.com/directx/cooperative-vector/ |
| SM 6.9 GA / DXR 1.2 / SER / OMM | Microsoft/NVIDIA | ~Q1 2026 (AgilitySDK 1.619 retail) | SM6.9 (long vectors, native DXIL vectors) reached GA; DXR 1.2 added Opacity Micromaps and Shader Execution Reordering | **GA** (retail, not preview) | OMM/SER need RTX-generation hardware for acceleration; broader compatibility via fallback | DXR 1.2 OMM/SER mirror NVIDIA's OMM extension and SER (VK_NV equivalents exist) | High — DXR 1.2 (OMM, SER) is now GA and directly relevant to a future D3D12 RT backend | https://www.igorslab.de/en/directx-12-agility-sdk-1-619-introduces-shader-model-6-9-microsoft-is-bringing-modern-gpu-features-out-of-preview-and-into-everyday-use/ |
| Agility SDK 1.616-retail (OMM + Tiled Resource Tier 4) | Microsoft | prior to 2026 (retail) | HW-accelerated alpha testing via OMM (up to 2.3x in path-traced titles); Tiled Resource Tier 4 adds full mip chains to tiled texture arrays | GA | Requires RT-capable HW for OMM benefit | Vulkan has an NV OMM-equivalent extension; no Metal equivalent | Med-High — Tiled Resource Tier 4 and OMM both matter if Luminex extends sparse/tiled and RT masked-alpha work to D3D12 | https://devblogs.microsoft.com/directx/agility-sdk-1-717-preview-and-1-616-retail/ |
| DirectSR | Microsoft | First previewed ~2024 (AgilitySDK 1.714.0-preview) | Unified upscaler-abstraction API: one integration surface driving FSR/DLSS/XeSS via driver-supplied implementations | Preview at launch (built-in FSR 2.2; driver-level DLSS/XeSS), full GA timeline unclear | Depends on installed vendor driver components; app supplies motion/depth/color | No Vulkan/Metal equivalent; conceptually similar to what MetalFX abstracts natively | Med — D3D12-side analog of Luminex's own TemporalScaler (native TAA/MetalFX) abstraction, worth studying before backend design | https://www.techpowerup.com/319793/microsoft-directsr-super-resolution-api-brings-together-dlss-fsr-and-xess |
| "Evolving DirectX for the ML Era on Windows" (neural rendering pillar) | Microsoft | Mar 12, 2026, GDC 2026 / Xbox Dev Summit | Official positioning: NTC and neural radiance caching as workloads executing "inline with traditional shading"; DX Linear Algebra public preview Apr 2026; DirectX Compute Graph Compiler private preview summer 2026 | Mixed — cooperative vectors (SM6.9) GA-adjacent; LinAlg still preview per this talk's own roadmap | RTX/Arc/Radeon current-gen per feature | HLSL positioned as the shader-ML authoring language; no Vulkan/Metal parity claim | High — Microsoft's clearest "neural rendering" pillar statement, frames what a Luminex D3D12 backend would eventually expose | https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-evolving-directx-for-ml-era-on-windows/ |
| DirectX Compute Graph Compiler | Microsoft | Announced Mar 2026 GDC talk | New compiler component for compute-graph/ML shader authoring | Private preview ("this summer" 2026) | Unclear | Unclear | Med — bears on Slang/DXC toolchain pairing for future ML-shader work | https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-evolving-directx-for-ml-era-on-windows/ |
| HLSL 202x language track | Microsoft | Ongoing 2025–2026; DXC 1.8.2405 (first Clang-built Windows binaries) | Narrowly scoped HLSL syntax/semantics modernization (e.g. literal-type conformance to C/C++); aligning DXC and Clang HLSL frontends | In progress, incremental releases | N/A (compiler-level) | Explicit cross-platform goal — Offload Test Suite validates DirectX/Vulkan/Metal shader output | Med — relevant to Slang-vs-HLSL toolchain decisions, though Luminex already committed to Slang | https://www.abolishcrlf.org/2026/02/10/HLSLState.html |
| Clang-based HLSL frontend | Microsoft/Google | Ongoing, per Feb 2026 status post | Clang implementing HLSL resource types ([RW]Buffer, [RW]StructuredBuffer); new root-signature parser with multi-error diagnostics vs. DXC's single-error abort | In development, not feature-complete | N/A | Explicit cross-platform intent (DirectX/Vulkan/Metal via Offload Test Suite) | Med — DXC remains reported as locked to an old LLVM base, a toolchain-debt risk worth tracking before depending on DXC longevity | https://www.abolishcrlf.org/2026/02/10/HLSLState.html |
| HLSL standardization (Ecma TC57) | Microsoft/Ecma International | Announced Feb 2026 | Formation of Technical Committee 57 under Ecma International to standardize HLSL | Committee formation stage | N/A | Could eventually parallel Khronos's Slang hosting | Low-Med — long-horizon signal only | https://www.abolishcrlf.org/2026/02/10/HLSLState.html |
| Mesh Nodes (Work Graphs leaf type) | Microsoft/AMD | Preview since 2024, still experimental in 2026 | Work Graphs leaf node that dispatches a mesh-shader graphics pipeline instead of a compute shader | Experimental preview (D3D12_WORK_GRAPHS_TIER_1_1, lib_6_9); stalled alongside the base Work Graphs SM6.10 freeze | Requires Developer Mode in Windows; driver support was incomplete for months after launch and still not universal | AMD GPUOpen has parallel Vulkan mesh-nodes-in-work-graphs samples; no Metal mesh/task-shader analog yet in Luminex | Med — effectively dead-ended by the Work Graphs SM6.10 freeze; deprioritize relative to Work Lists | https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/ |
| D3D12 tight alignment / Windows-on-Arm / Xbox next-gen API | Microsoft | — | Not independently verified | **[UNVERIFIED]** — not verified in this notebook | — | — | Unknown — flagged as a research gap, not a negative finding | — |

### Khronos / Vulkan

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| Vulkan 1.4 | Khronos | Dec 3, 2024 | Core spec bump: mandates push descriptors, dynamic rendering local reads, scalar block layout, maintenance1-6 folded in, guaranteed 8K/8-target rendering | GA | Any Vulkan 1.4-conformant driver | No cooperative-matrix/mesh-shader/DGC promotion to core — confirmed unchanged from prior baseline | Med — Vulkan is research-only for Luminex | https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4 |
| VK_KHR_cooperative_matrix | Khronos (NVIDIA/AMD/Arm-authored) | Ratified, shipped Vulkan 1.3.255, Jun 2023 | Subgroup-cooperative SPIR-V matrix-multiply types for GPU ML kernels | Ratified KHR extension, **not** promoted into 1.4 core or Roadmap 2026 | NVIDIA/AMD/Arm implementations exist; per-device shape/type query required | Confirms prior finding: still extension-only, not core | High — the extension a Luminex coop-vector path would sit alongside on a future Vulkan backend | https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_cooperative_matrix.html |
| VK_NV_cooperative_vector | NVIDIA | Vendor extension, active | SIMT-style matrix-vector multiply for small NN, Vulkan analog of D3D12 SM6.9/6.10 LinAlg | Vendor-only (NV prefix); **no VK_KHR_cooperative_vector exists yet** — confirmed via Vulkan docs/registry | NVIDIA RTX (coop-vector-capable) hardware only | Confirms prior finding exactly | High — directly determines whether a future Vulkan backend can share Slang neural-shading code with Metal/D3D12 today (it currently can't, cross-vendor) | https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cooperative_vector.html |
| VK_NV_cooperative_matrix2 | NVIDIA | Vendor extension; covered at Vulkanised 2025 | 2nd-gen cooperative matrix with more flexible tiling/types for ML workloads | Vendor extension (NV) | NVIDIA RTX hardware | Not cross-vendor | Med | https://www.vulkan.org/user/pages/09.events/vulkanised-2025/T47-Jeff-Bolz-NVIDIA.pdf |
| VK_QCOM_cooperative_matrix_conversion | Qualcomm | Recent point release (Vulkan 1.4.342) | Matrix-type conversion ops for Qualcomm's cooperative-matrix implementation (mobile) | Vendor extension (QCOM) | Qualcomm Adreno mobile GPUs | Mobile-only, not relevant to desktop backends | Low | https://www.phoronix.com/news/Vulkan-1.4.342-Released |
| VK_EXT_device_generated_commands | Khronos (successor to NVIDIA's NV_device_generated_commands) | Shipped Vulkan 1.3.296 | GPU-generated/indirect command-buffer recording, successor to vendor DGC extensions | Ratified EXT extension | AMD RADV: supported since Mesa 24.3 (GFX8/Polaris+); Intel ANV: merge request in progress; NVIDIA: originated the predecessor NV extension | Directly comparable to D3D12 ExecuteIndirect/Work Graphs and Metal4 indirect — good 3-way comparison candidate | High — Luminex's argument-table/indirect-draw RHI design sits exactly in this space | https://www.phoronix.com/news/RADV-VK-EXT-DGC |
| Mesh shading (VK_EXT_mesh_shader) | Khronos (cross-vendor) | Extension since 2022; adoption maturing through 2025–2026 | Cross-vendor task+mesh shader stages, aligned with D3D12 mesh shaders for portability | Ratified EXT; adoption maturing, not universal | AMD RADV: supported (RDNA2+); NVIDIA NVK (Mesa open driver): merged for Mesa 26.2; Intel ANV: experimental only on Arc Alchemist, gated behind an env var, off by default | Explicit design goal of parity with D3D12 mesh shaders | Med — relevant if Luminex's GPU-driven roadmap adopts mesh shading; no Metal 4 mesh-shader analog implemented yet | https://www.khronos.org/blog/mesh-shading-for-vulkan |
| VK_EXT_shader_object | Khronos | Introduced Vulkan 1.3.246 (Mar 2023); driver support broadened 2024–2025 | Compiled single-stage shader objects as an alternative to monolithic pipeline objects (avoids pipeline-compile stutter) | Ratified EXT, not core; SDK ships an emulation layer for drivers lacking native support | RADV: default-on since Mesa 24.1; NVK: early 2024; Intel ANV (Linux): landed Mesa 25.3-devel; NVIDIA proprietary: beta driver support | No direct Metal/D3D12 equivalent concept — both already avoid the monolithic-pipeline problem differently | Low-Med | https://www.phoronix.com/news/Intel-ANV-VK_EXT_shader_object |
| Vulkan Roadmap 2026 | Khronos | Published Jan 23, 2026 | Annual milestone raising baseline required features beyond 1.4: variable rate shading, shader clock queries, host image copies, compute shader derivatives, swapchain/limit increases | Published; SDK support targeted Q1 2026 | Applies to "high-end" Vulkan implementations | **Does not** mandate cooperative matrix/vector, mesh shaders, DGC, or ray tracing — those remain opt-in extensions | Med | https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension |
| VK_EXT_descriptor_heap | Khronos | Announced with Roadmap 2026, Jan 2026 | Full redesign of Vulkan's descriptor system for direct descriptor-memory access, keeping legacy descriptor-set compatibility | New extension, SDK support expected Q1 2026 | Vulkan 1.4+ implementations | Conceptually closer to D3D12's descriptor-heap/bindless model and Metal argument buffers — worth watching for cross-API bindless convergence | Med | https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension |
| Slang — Khronos-hosted project | Khronos / originally NVIDIA | Initiative launched Dec 2024; governance confirmed stable (Khronos Operational Guidelines v26, Mar 2026) | Open-source, open-governance shading language/compiler hosted at Khronos: Apache-2.0 GitHub project + Khronos member oversight working group | GA / actively governed — a genuine multi-company oversight structure, not just a code contribution | Cross-compiles to SPIR-V, DXIL, Metal IR, CUDA, WGSL | Directly the toolchain Luminex already depends on; governance stability is good news for a long-term bet | High — Luminex ships Slang shaders today | https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia |
| Vulkan gaussian-splatting extension activity | Khronos | Checked, no dedicated ratified extension found | No formal `KHR_gaussian_splatting` extension located via primary Khronos sources | [UNVERIFIED] — no primary-source confirmation of a formal Khronos splat extension as of this check | — | — | Low — out of scope for this notebook | — |

### Consoles

| Item | Vendor | When (date/venue) | What it is | Status | Hardware/API/driver requirements | Cross-API portability notes | Relevance to Luminex | Source URL |
|---|---|---|---|---|---|---|---|---|
| PS5 Pro PSSR (original) | Sony | Shipped with PS5 Pro, Nov 2024 | ML-based checkerboard/temporal upscaler exclusive to PS5 Pro | GA since Nov 2024 | PS5 Pro custom RDNA hardware only | Closed platform, no portability | Low — closed console platform | (background fact, not re-verified) |
| Upgraded PSSR / "Project Amethyst" | Sony + AMD (confirmed joint R&D) | Announced Feb 27, 2026 (PlayStation Blog); rollout through Mar 2026 | **Confirmed directly by Sony**: "The algorithm and neural network used in the new PSSR stem from our Project Amethyst partnership with AMD" — a genuinely new network/algorithm co-developed alongside PC FSR 4, with "six months further refinement" for PS5 Pro | GA rollout in progress; first title Resident Evil Requiem (Feb 27, 2026), broader rollout (Silent Hill f, Monster Hunter Wilds, FF7 Rebirth, Crimson Desert) by mid-Mar 2026 | PS5 Pro custom AMD silicon | Sony explicitly states this is **not** a literal FSR4 port — a custom PlayStation-tuned model sharing Project Amethyst R&D lineage with FSR4/FSR Redstone | Low-Med — closed platform, but the FSR4↔PSSR shared-R&D lineage is a useful data point for how AMD's ML-upscaler family converges across PC/console | https://blog.playstation.com/2026/02/27/upgraded-pssr-upscaler-is-coming-to-ps5-pro/ |
| PS5 Pro "PSSR 2" / Multi-Frame Super Resolution 2 | Sony (rumored) | Leak reports, early–mid 2026 | Claimed larger follow-on upgrade beyond the Feb 2026 upgraded PSSR, said to cut memory/GPU time via a "Multi-Frame Super Resolution 2" approach | **[UNVERIFIED]** — leak/rumor-tier sources only, no Sony/AMD primary confirmation found | — | — | Low | https://thegamepost.com/ps5-pro-leak-pssr2-2026-multi-frame-super-resolution-2-memory-use-and-gpu-time/ |
| Nintendo Switch 2 custom NVIDIA chip: DLSS + hardware RT | NVIDIA / Nintendo | Console shipped Jun 2025; NVIDIA/Nintendo confirmations pre-launch (2025) | Custom NVIDIA SoC (Ampere-generation) with dedicated RT Cores and Tensor Cores; DLSS confirmed to upscale to 1080p handheld / 4K docked; HW ray-traced reflections/shadows/dynamic lighting confirmed by NVIDIA | GA — shipped, officially confirmed by both companies (not rumor) | Switch 2 custom Ampere-class NVIDIA SoC only | Closed platform; DLSS version used is NVIDIA-proprietary, no portability | Low — closed console, but confirms DLSS/RT tech scaling down to mobile-class Ampere silicon | https://www.tweaktown.com/news/104426/nvidia-confirms-the-switch-2-can-handle-ray-traced-reflections-shadows-and-dynamic-lighting/index.html |

## 2. Neural-shader execution landscape as of 2026-09

D3D12's neural-shading surface remains **preview, not GA**. Cooperative Vectors (`linalg`
namespace: FLOAT32/FLOAT8_E4M3/FLOAT16/UINT8 types) shipped in preview via AgilitySDK
1.717.1-preview (Jun 2, 2025); Shader Model 6.10 — which folds cooperative vectors into a formal
"LinAlg" op set (adding VectorAccumulate in AgilitySDK 1.721-preview, May 2026) with
Thread/Wave/ThreadGroup scopes — is still preview as of the latest AgilitySDK 1.721-preview (May
2026), unchanged from the 2026-09-06 baseline. On Sep 4, 2026 Microsoft additionally announced it
is **not** advancing base Work Graphs into SM6.10 at all, citing low game-developer adoption and
weak driver/GPU implementations — so the neural-shading (LinAlg/cooperative-vector) and
GPU-driven-scheduling (Work Graphs) tracks of SM6.10 have effectively decoupled, with only the
former still moving forward. SM6.9 (long/native vectors, DXR 1.2 with OMM+SER) did reach GA
(~AgilitySDK 1.619 retail).

Vulkan's picture is narrower and unchanged in its key constraint: `VK_KHR_cooperative_matrix` is a
ratified, cross-vendor (NVIDIA/AMD/Arm) subgroup-cooperative extension, but has **not** been
promoted into core (not in 1.4, not in Roadmap 2026). The SIMT-style, simpler cooperative-*vector*
model that D3D12's LinAlg mirrors exists only as `VK_NV_cooperative_vector` — an NVIDIA-only
extension with **no ratified `VK_KHR_cooperative_vector`** as of Sep 2026. NVIDIA added
`VK_NV_cooperative_matrix2` (finer tiling/type control) and a `VK_NV_cooperative_matrix_decode_vector`
extension in Vulkan 1.4.352 (May 2026, beta drivers required) — all still vendor-only.

Slang (now Khronos-hosted with stable multi-company governance since Dec 2024) is the only
practical cross-API authoring path: it lowers cooperative-vector code to `SPV_NV_cooperative_vector`
(Vulkan) and DXIL LinAlg intrinsics (D3D12) from one `coopVecMatMul`-style source API. This remains
**experimental**, not stabilized — open compiler issues persist into Q2 2026. Critically, **no
Metal AIR/MSL backend for coop-vector lowering exists**; Slang's cross-compilation to Metal is for
ordinary shaders, not this neural-shading path, so a Metal 4 renderer has no vendor- or
Slang-mediated route into cooperative-vector-style in-shader ML inference today.

Hardware acceleration: NVIDIA RTX (Turing+ for coop-vector via driver, best on Blackwell's 5th-gen
Tensor Cores with native FP4/FP6/FP8), AMD RDNA4 (2nd-gen AI accelerators, FP8/INT4, up to 2x AI
perf vs RDNA3), Intel Arc (XMX units, Alchemist through Xe3/Panther Lake). Quantization formats in
active use: FP8 (E4M3), INT8, FP16 baseline, INT4 (RDNA4), FP4/FP6 (Blackwell tensor path).
Cooperative-matrix types require per-device queried M/N/K shapes (subgroup-cooperative); cooperative
vectors use a simpler per-thread/SIMT model without subgroup cooperation, which is why they map more
directly onto ordinary shader threads for small per-pixel MLPs (texture/material decompression).

## 3. GPU-driven execution landscape

D3D12 Work Graphs (base feature) reached GA in 2024 (AgilitySDK 1.613.0) but is now frozen at SM6.8/
6.9 — Microsoft deprioritized further Work Graphs investment for SM6.10 (Sep 4, 2026) after weak
adoption and driver quality. Mesh nodes, the more experimental leaf-node type dispatching mesh-shader
pipelines from within a work graph, never left `D3D12_WORK_GRAPHS_TIER_1_1` experimental/preview and
is now stalled with it. In its place, Microsoft has published a spec-only, pre-preview concept called
**Work Lists** — closer in spirit to `ExecuteIndirect` (GPU picks a PSO per draw/dispatch from a
GPU-resident table) than to the Work Graphs node-execution model; no preview build exists yet
(possible ~2027).

AMD is the vendor with the most mature real driver-level Work Graphs/mesh-nodes support (preview
Adrenalin 24.9.1, full support 24.10.30.01, both Oct 2024) and has published parallel D3D12 and
Vulkan mesh-nodes-in-work-graphs developer articles — making it the best single-vendor reference for
a cross-API GPU-driven comparison, even though the underlying D3D12 feature is now deprioritized.

Vulkan's own GPU-driven primitive, `VK_EXT_device_generated_commands` (ratified, successor to
NVIDIA's `NV_device_generated_commands`), has asymmetric driver maturity: AMD RADV has supported it
since Mesa 24.3 (GFX8/Polaris+), Intel's ANV driver has a merge request in progress, and NVIDIA
(which originated the predecessor extension) has not been confirmed to expose the EXT variant.
`VK_EXT_mesh_shader` adoption is similarly uneven: solid on AMD RADV (RDNA2+) and newly
landed on NVIDIA's open NVK driver (Mesa 26.2), but only experimental and env-var-gated on Intel ANV
for Arc Alchemist.

NVIDIA's RTX Mega Geometry (cluster-based BVH, ~100x more ray-traced triangles) is a separate,
vendor-specific answer to GPU-driven geometry scaling, exposed through OptiX 9/DXR/Vulkan RT rather
than through Work Graphs or DGC.

A fair cross-API comparison would need: matched hardware spanning all three PC vendors, an identical
indirect-draw/dispatch workload, and an explicit accounting that D3D12 Work Graphs/mesh nodes,
Vulkan DGC/mesh shaders, and Metal 4's own indirect command buffers are architecturally distinct
primitives with different scheduling models — not drop-in equivalents — which the
`vk_device_generated_cmds` NVIDIA sample already partially demonstrates for the Vulkan side alone.

## 4. Hardware purchase/rental implications

For a single Windows GPU purchase, an **NVIDIA RTX 50-series (Blackwell) card, e.g. RTX 5080/5090**,
exercises the broadest slice of what this report covers: it is the primary dev-relations target for
D3D12's Cooperative Vector/SM6.10 LinAlg preview, has a native Vulkan cooperative-vector/matrix path
(`VK_NV_cooperative_vector`, `VK_NV_cooperative_matrix2`), exposes RTX Mega Geometry, Opacity
Micromaps, SER, 4th-gen RT Cores with the LSS hardware primitive, and 5th-gen Tensor Cores with
native FP4/FP6/FP8 — the widest hardware feature surface of any single card checked in this
research. It is the only realistic option for **CUDA-based training** work (e.g. training a custom
neural texture compressor or radiance-cache network from scratch) — AMD's ROCm/HIP training
ecosystem exists but is far less mature outside Linux/datacenter contexts, and none of the vendor
SDKs surveyed here require the developer to train their own models versus reimplementing published,
already-trained techniques.

An **AMD RDNA4 card (RX 9070/9070XT)** is the better choice specifically for exercising D3D12 Work
Graphs/mesh-nodes and Vulkan DGC/mesh-shader work (AMD has the most mature real driver support for
both, and its open Mesa/RADV Linux driver stack is ahead of NVIDIA's for DGC/mesh shaders), and for
FSR4/FSR Redstone/Brixelizer, which are AMD's genuinely open-source (MIT), directly reimplementable
reference techniques — arguably more useful to a solo Metal-first developer than closed NVIDIA SDKs
regardless of which card is purchased.

Net recommendation: buy or rent an RTX 5080-class Blackwell card for the single highest feature
coverage (graphics API breadth **and** CUDA training access); a secondary or rented RDNA4 card adds
value narrowly for Work-Graphs/DGC/mesh-shader driver-behavior comparison and for exercising AMD's
open-source technique references. Neither substitutes for Metal 4 development work — both are
purely for cross-API research and roadmap-comparison evidence gathering.

## 5. Sources

Observed 2026-09-14 unless noted otherwise.

NVIDIA:
- https://www.nvidia.com/en-us/geforce/news/ces-2026-nvidia-geforce-rtx-announcements/
- https://www.nvidia.com/en-us/geforce/news/dlss-4-5-dynamic-multi-frame-gen-6x-2nd-gen-transformer-super-res/
- https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/
- https://www.nvidia.com/en-us/geforce/news/computex-2026-nvidia-geforce-rtx-announcements/
- https://www.nvidia.com/en-us/geforce/news/gdc-2026-nvidia-geforce-rtx-announcements/
- https://www.pcgamer.com/hardware/live/news/nvidia-gtc-2026-keynote-live/ [UNVERIFIED secondary source, DLSS 5 claim]
- https://www.tomsguide.com/computing/gpus/nvidia-says-pc-gaming-will-look-like-a-film-how-gpus-will-get-to-1-million-times-better-path-tracing-and-why-its-closer-than-you-think [UNVERIFIED marketing figure]
- https://github.com/NVIDIA-RTX/RTXNS
- https://github.com/NVIDIA-RTX/RTXNTC
- https://github.com/NVIDIA-RTX/RTXGI
- https://github.com/NVIDIA-RTX/RTXDI (+ /Doc/RestirPT.md)
- https://github.com/NVIDIA-RTX/RTXCR
- https://github.com/NVIDIA-RTX/RTX-Kit/releases
- https://developer.nvidia.com/rtx-kit
- https://videocardz.com/newz/nvidia-releases-optix-9-with-rtx-mega-geometry-and-neural-texture-compression-sdks
- https://www.tomshardware.com/pc-components/gpus/benchmarking-nvidias-rtx-neural-texture-compression-tech-that-can-reduce-vram-usage-by-over-80-percent
- https://www.phoronix.com/news/Vulkan-1.4.352-Released
- https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_cooperative_vector.html
- https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cooperative_vector.html
- https://www.hwcooling.net/en/cooperative-vectors-in-directx-to-use-blackwell-neural-shaders/
- https://shader-slang.org/blog/2025/01/30/coop-vec-available/
- https://github.com/shader-slang/slang/issues/10979
- https://hothardware.com/reviews/nvidia-rtx-blackwell-architecture-overview
- https://www.nvidia.com/en-us/products/workstations/professional-desktop-gpus/rtx-pro-6000-family/
- https://github.com/nvpro-samples/vk_device_generated_cmds
- https://forums.developer.nvidia.com/t/extremely-poor-vk-ext-device-generated-commands-performance/324189

AMD:
- https://gpuopen.com/learn/amd-fsr4-gpuopen-release/
- https://gpuopen.com/amd-fsr-sdk/
- https://gpuopen.com/fidelityfx-super-resolution-4/
- https://gpuopen.com/amd-fidelityfx-sdk/
- https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/
- https://gpuopen.com/amd-fsr-rayregeneration/
- https://gpuopen.com/amd-fsr-radiancecaching/
- https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/
- https://gpuopen.com/fidelityfx-brixelizer/
- https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/brixelizer-gi.md
- https://gpuopen.com/learn/work_graphs_mesh_nodes/work_graphs_mesh_nodes-intro/
- https://gpuopen.com/learn/gpu-workgraphs-mesh-nodes-vulkan/
- https://www.amd.com/en/newsroom/press-releases/2025-2-28-amd-unveils-next-generation-amd-rdna-4-architectu.html
- https://www.tomshardware.com/pc-components/gpus/amd-rdna-4-radeon-rx-9000-series-gpus-revealed-targeting-mainstream-price-and-performance-with-improved-ai-and-ray-tracing
- https://www.igorslab.de/en/amd-rdna5-architectural-restart-or-strategic-tour-de-force/ [conflicting/rumor content, UNVERIFIED on specifics/dates]
- https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-26-3-1.html
- https://egpu.io/forums/pc-gaming/amd-releases-fsr-4-for-rdna-3-cards-rdna-3-5-apus/ [secondary/forum, UNVERIFIED]
- https://www.techpowerup.com/320547/amd-posts-super-early-work-graphs-render-time-numbers-posts-39-render-time-improvements [secondary]

Intel:
- https://www.intel.com/content/www/us/en/developer/articles/technical/xess2-whitepaper.html
- https://www.windowscentral.com/gaming/pc-gaming/intel-xess-2-now-available-with-frame-generation-low-latency
- https://videocardz.com/newz/intel-announces-xess-2-with-xess-frame-generation-and-xess-low-latency
- https://www.techpowerup.com/341767/intel-xess-3-expands-multi-frame-generation-support-to-all-xess-2-titles
- https://www.tomshardware.com/pc-components/gpu-drivers/intel-enables-xess-3-multi-frame-generation-in-latest-drivers-expanding-frame-generation-across-arc-gpus-and-core-ultra-igpus-mfg-can-be-enabled-across-any-title-with-xess-2-support
- https://www.intel.com/content/www/us/en/newsroom/news/intel-launches-arc-b-series-graphics-cards.html
- https://www.hwcooling.net/en/batttlemage-details-of-intel-xe2-gpu-architecture-analysis/
- https://videocardz.com/newz/intel-details-xe3-gpu-architecture-for-panther-lake-up-to-12-xe-cores-and-50-performance-vs-lunar-lake
- https://www.tomshardware.com/pc-components/gpus/intels-xe3-graphics-architecture-breaks-cover-panther-lakes-12-xe-core-igpu-promises-50-percent-better-performance-than-lunar-lake
- https://www.tomshardware.com/pc-components/gpus/intel-has-reportedly-killed-discrete-gaming-gpus-for-the-upcoming-xe3p-arc-celestial-family-gaming-gpu-remains-uncertain-even-for-the-next-gen-xe4-druid-lineup-that-lands-in-2027
- https://nvidianews.nvidia.com/news/nvidia-and-intel-to-develop-ai-infrastructure-and-personal-computing-products
- https://www.phoronix.com/news/Open-Image-Denoise-2.0
- https://www.techpowerup.com/318651/intel-open-image-denoise-v2-2-adds-metal-support-aarch64-improvements
- https://github.com/RenderKit/oidn/releases/tag/v2.3.0
- https://www.cgchannel.com/2026/01/open-image-denoise-3-will-support-temporal-denoising/
- https://www.intel.com/content/www/us/en/developer/articles/news/gpu-research-generative-ai-update.html

Microsoft DirectX:
- https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/
- https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/
- https://devblogs.microsoft.com/directx/cooperative-vector/
- https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-evolving-directx-for-ml-era-on-windows/
- https://www.ginjfo.com/actualites/composants/cartes-graphiques/directx-12-work-graphs-est-mis-de-cote-faute-dadoption-dans-les-jeux-20260904
- https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkLists.md (seen in search, not directly fetched)
- https://www.abolishcrlf.org/2026/02/10/HLSLState.html
- https://devblogs.microsoft.com/directx/dxc-1-8-2405-available/ (seen in search)
- https://www.igorslab.de/en/directx-12-agility-sdk-1-619-introduces-shader-model-6-9-microsoft-is-bringing-modern-gpu-features-out-of-preview-and-into-everyday-use/ (seen in search)
- https://devblogs.microsoft.com/directx/agility-sdk-1-717-preview-and-1-616-retail/ (seen in search)
- https://www.techpowerup.com/319793/microsoft-directsr-super-resolution-api-brings-together-dlss-fsr-and-xess (seen in search)
- https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/ (seen in search)
- https://github.com/microsoft/DirectX-Specs/blob/master/d3d/WorkGraphs.md (seen in search, not directly fetched)

Khronos/Vulkan and Consoles:
- https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4
- https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_cooperative_matrix.html
- https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cooperative_vector.html
- https://www.vulkan.org/user/pages/09.events/vulkanised-2025/T47-Jeff-Bolz-NVIDIA.pdf
- https://www.phoronix.com/news/Vulkan-1.4.342-Released
- https://www.phoronix.com/news/RADV-VK-EXT-DGC
- https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_device_generated_commands.html
- https://www.khronos.org/blog/mesh-shading-for-vulkan
- https://daily.dev/posts/mesa-26-2-released-with-nvk-mesh-shader-support-many-other-vulkan-improvements-hjunmu9bt
- https://www.phoronix.com/news/Intel-ANV-VK_EXT_shader_object
- https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension
- https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia
- https://www.khronos.org/files/working-group-guidelines.pdf (Khronos Operational Guidelines v26, Mar 2026)
- https://blog.playstation.com/2026/02/27/upgraded-pssr-upscaler-is-coming-to-ps5-pro/
- https://blog.playstation.com/2026/03/16/upgraded-pssr-rolling-out-to-silent-hill-f-monster-hunter-wilds-final-fantasy-vii-rebirth-crimson-desert-and-more/
- https://www.techpowerup.com/338569/playstation-5-pro-to-gain-full-amd-fsr-4-integration-in-2026
- https://thegamepost.com/ps5-pro-leak-pssr2-2026-multi-frame-super-resolution-2-memory-use-and-gpu-time/ [UNVERIFIED leak tier]
- https://www.tweaktown.com/news/104426/nvidia-confirms-the-switch-2-can-handle-ray-traced-reflections-shadows-and-dynamic-lighting/index.html
- https://www.gamespot.com/articles/switch-2-will-support-dlss-upscaling-and-hardware-ray-tracing-nintendo-confirms/1100-6530613/
- https://www.kitguru.net/gaming/matthew-wilson/nintendo-confirms-switch-2s-custom-nvidia-chip-supports-dlss-and-ray-tracing/
