# Training and in-shader inference toolchain

**Status**: Frozen — non-normative  
**Research date:** 2026-09-14

Evidence notebook for the [rendering direction review](../2026-09-14-rendering-direction-review.md),
collected in one web-assisted pass over SDK repositories, vendor documentation and pricing pages.
The review reconciles conflicts between notebooks and takes precedence over any judgment here.
Items marked **[UNVERIFIED]** were not confirmed against a primary source; recheck them before a
plan or a hardware purchase depends on them. Prices are snapshots and will drift.

## 1. Tool/SDK table

| Tool/SDK | Owner | Version/date | What it does | Training / Inference / Both | Targets | License | Maturity | Fit for Luminex | Source |
|---|---|---|---|---|---|---|---|---|---|
| Slang | Khronos/shader-slang (NVIDIA-led) | active, 2026.10 used by RTXNS | Shading language; single source compiles to CUDA/HLSL/SPIR-V/Metal(MSL); has built-in autodiff | Both | CUDA, D3D12, Vulkan, Metal (via MSL codegen), CPU | Apache-2.0 w/ LLVM exception | Production (core lang); ML features preview | **High** — Luminex already targets Slang→MSL; autodiff path is a native fit | https://shader-slang.org/ |
| Slang autodiff (differentiable Slang) | shader-slang / NVIDIA Research | ongoing | Automatic forward/backward-mode differentiation of arbitrary Slang functions incl. control flow | Training (in-shader) | Any Slang target | Apache-2.0 | Production for simple kernels, active research for complex graphs | High — lets training and inference share one source of truth | https://developer.nvidia.com/blog/differentiable-slang-a-shading-language-for-renderers-that-learn/ |
| SlangPy | shader-slang | active (issues open Sept 2026) | Python bridge calling Slang kernels from PyTorch training loops with near-native perf | Training (bridge) | Backends Slang supports; host is Python/PyTorch (needs CUDA GPU) | Apache-2.0 | Preview/active dev (perf-parity issues open) | High for training-box side; N/A on Mac (needs CUDA) | https://github.com/shader-slang/slangpy |
| slang-torch | shader-slang | maintained, being superseded by SlangPy | Earlier PyTorch↔Slang kernel bridge (`DiffTensorView`) | Training (bridge) | CUDA (via PyTorch) | Apache-2.0 | Being deprecated in favor of SlangPy | Med — migrate straight to SlangPy instead | https://github.com/shader-slang/slang-torch |
| neural-shading-s26 | shader-slang | SIGGRAPH 2026 course | Hands-on course: build/train MLPs in Slang, use tensor cores, neural materials | Both (teaching) | CUDA/D3D12/Vulkan examples | Apache-2.0 | Reference/teaching material | High — best available worked example set | https://github.com/shader-slang/neural-shading-s26 |
| neural-shading-s25 (predecessor) | shader-slang | SIGGRAPH 2025 | Prior year course incl. `mlp-training-coopvec` example | Both | CUDA/D3D12 | Apache-2.0 | Reference | Med (superseded by s26) | https://github.com/shader-slang/neural-shading-s25 |
| NVIDIA RTX Neural Shaders (RTXNS) | NVIDIA-RTX | v1.1.0, last update 2025-05-30, needs Slang v2026.10 | Library: create/serialize/train small MLPs, `HostNetwork`, Adam optimizer, activations, layout conversion helpers | Both | D3D12 (Windows), Vulkan (Windows/Linux) via nvrhi | NVIDIA RTX SDK license (source-available) | Preview/early production | **High** — closest existing reference impl for a Slang-based in-shader MLP; no macOS build but architecture is portable | https://github.com/NVIDIA-RTX/RTXNS ; https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/LibraryGuide.md |
| NVIDIA RTX Neural Texture Compression (RTXNTC) | NVIDIA-RTX | v0.10.0 BETA | Compresses correlated PBR texture sets into a small MLP decoder + latents; 3 inference modes (on-load/sample/feedback) | Both (CUDA-side compression; shader-side inference) | D3D12 (incl. SM6.10 LinAlg preview), Vulkan 1.3, CUDA (compress) | NVIDIA RTX SDK license | Beta | Med — concrete "bake once, infer in shader" precedent even though Luminex won't adopt NTC itself yet | https://github.com/NVIDIA-RTX/RTXNTC |
| NVIDIA OptiX 9 cooperative vectors | NVIDIA | published 2025-04-17 | Cooperative-vector intrinsics inside OptiX ray-tracing kernels (`optixCoopVecMatrixConvert`, reduce/outer-product accumulate for backward pass) | Both | CUDA / OptiX (RTX GPUs) | NVIDIA SDK license | Production (OptiX 9.0) | Low direct fit (Luminex isn't OptiX-based) but useful as intrinsics reference | https://developer.nvidia.com/blog/neural-rendering-in-nvidia-optix-using-cooperative-vectors/ |
| tiny-cuda-nn | NVlabs | actively forked/used; no 2026-specific release notes found | Fully-fused CUDA MLP/hash-encoding library; the de facto training/reference implementation for tiny networks | Training (reference/oracle) | CUDA only | BSD-3-Clause-Clear (NVIDIA) | Production/research-grade, widely depended on | High for the training-box CPU/GPU oracle role | https://github.com/NVlabs/tiny-cuda-nn |
| Dr.Jit `drjit.nn` | Mitsuba/EPFL (Wenzel Jakob) | rolling release | JIT-compiled differentiable renderer core; now has cooperative-vector matmul (tensor-core-backed) + `nn` module mirroring `torch.nn` for fused MLP eval/training | Both | CUDA (LLVM/CPU fallback) | BSD-3-Clause | Production for research use | Med — good design reference for a "renderer with a training mode," not directly reusable (no Metal/D3D12 backend) | https://drjit.readthedocs.io/en/latest/coop_vec.html ; https://github.com/mitsuba-renderer/drjit |
| Mitsuba 3 | EPFL/Mitsuba team | 3.7.0 | Differentiable physically-based renderer built on Dr.Jit; PyTorch interop documented | Both | CUDA, LLVM/CPU | BSD-3-Clause | Production (research) | Low-Med — reference for PyTorch↔renderer interop pattern | https://mitsuba.readthedocs.io/en/stable/src/inverse_rendering/pytorch_mitsuba_interoperability.html |
| D3D12 Cooperative Vector | Microsoft + IHVs | preview since ~mid-2025, HLSL proposal **rejected**/superseded 2026 | Shader-side matrix-vector ops mapped to tensor cores; 4 layouts (RowMajor/ColMajor/MulOptimal/OuterProductOptimal); fp32/fp8_e4m3/uint8 weight types; `ConvertLinearAlgebraMatrix` | Inference (training elsewhere) | D3D12 only (NVIDIA RTX, Intel Arc B/Core Ultra 2, AMD preview, WARP) | Part of Windows/Agility SDK (MS license) | **Preview, being superseded** | Med — real but a moving target; wait for LinAlg Matrix (SM6.10) before committing Luminex's D3D12 backend to it | https://devblogs.microsoft.com/directx/cooperative-vector/ ; https://microsoft.github.io/hlsl-specs/proposals/0029-cooperative-vector/ |
| D3D12 Linear Algebra Matrix (SM6.10) | Microsoft | successor spec, proposal 0035, 2026 | Formal successor to Cooperative Vector; matrix type + `ConvertLinearAlgebraMatrix`/`GetLinearAlgebraMatrixConversionDestinationInfo`; matrix-matrix ops planned | Inference (training elsewhere) | D3D12 | MS/Agility SDK license | Preview (successor spec, actively adopted e.g. by AMD MiniDXNN v0.4) | **High for the future D3D12 backend** — this, not Cooperative Vector, is the API Luminex should target when D3D12 lands | https://microsoft.github.io/hlsl-specs/proposals/0029-cooperative-vector/ (supersession note) ; https://microsoft.github.io/DirectX-Specs/d3d/D3D12LinearAlgebraRuntimeFeatureSupport.html |
| AMD MiniDXNN | AMD GPUOpen | v0.4.0 | Open-source GPU MLP training+inference in D3D12 compute shaders; migrated from Cooperative Vector (SM6.9) to LinAlg Matrix (SM6.10) for "cleaner API + driver-side layout conversion" | Both | D3D12 (AMD RDNA "RX 9000" + NVIDIA equivalent, preview driver) | Open source (GPUOpen, check repo for exact license) | Preview/research | Med-High — concrete evidence the industry is converging on LinAlg Matrix over Cooperative Vector | https://gpuopen.com/learn/minidxnn-v040-interactive-neural-texture-compression/ |
| Vulkan `VK_NV_cooperative_vector` | Khronos/NVIDIA vendor ext | current | Per-invocation cooperative-vector matrix-vector types for small-network eval; `vkConvertCooperativeVectorMatrixNV` for layout/precision conversion | Inference | Vulkan, NVIDIA only | Khronos spec (royalty-free) | Vendor preview extension | Low-Med — NVIDIA-only, not a multi-vendor Vulkan path yet | https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_cooperative_vector.html |
| Vulkan `VK_KHR_cooperative_matrix` | Khronos (multi-vendor) | ratified, widely shipped | Cross-invocation cooperative matrix type (tile-level, closer to WMMA/tensor-core primitives) for general GEMM-like workloads, not per-thread MLP eval | Both | Vulkan (NVIDIA, AMD, Intel, some mobile) | Khronos spec (royalty-free) | Production | Med — more portable than `NV_cooperative_vector` but a different (tile) programming model than Slang's CoopVec sugar targets | https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_cooperative_matrix.html |
| Metal 4 TensorOps / cooperative_tensor | Apple | WWDC26 (2026), ships with macOS 26.x / M5, A19 Pro | MSL tensor types (`tensor_handle`, `tensor_inline`, `cooperative_tensor`); `matmul` + activation chaining inside shaders at thread/SIMD-group scope; **supports both inference and true online in-shader training/backprop** | **Both** (confirmed: session demos per-frame online training of a sky-irradiance MLP) | **Metal only**, needs M5/A19 Pro neural accelerator for the fast path | Apple platform (system framework) | Preview→shipping with macOS 26.x, hardware-gated | **High — this is the native Luminex-side inference (and possibly training) surface**; but ties fast path to M5/A19 Pro-class silicon | https://developer.apple.com/videos/play/wwdc2026/359/ ; https://developer.apple.com/videos/play/wwdc2025/262/ |
| Metal 4 ML Command Encoder + MTLPackage | Apple | Metal 4 (macOS 26) | Deploys an offline-trained model (exported to `.mtlpackage`) as its own command-buffer workload alongside render/compute passes (e.g. neural tone mapper) | Inference | Metal | Apple platform | Shipping (Metal 4) | High — the right path for a *bigger* offline-trained network (vs. tiny in-shader MLP) | https://developer.apple.com/videos/play/wwdc2026/359/ |
| MTLTensor | Apple | Metal 4 | Resource type for multidimensional ML data (int8/fp16 etc.), CPU↔GPU tensor handoff | Inference (data plumbing) | Metal | Apple platform | Shipping | High — plumbing Luminex would use to hand weights to TensorOps/ML encoder | https://developer.apple.com/videos/play/wwdc2025/262/ |
| MetalFX (denoise/upscale) | Apple | shipping, used by Luminex already for temporal path context | Fixed-function-ish neural denoise/upscale, not programmable per-network | Inference only | Metal | Apple platform | Production | N/A to this workstream (Luminex already integrates MetalFX for temporal upscaling; separate from custom neural shading) | https://developer.apple.com/videos/play/wwdc2026/359/ |
| Apple MLX | Apple ML Research | MIT license, active | NumPy-like array framework with autodiff, unified-memory CPU/GPU dispatch on Apple Silicon; native `safetensors` + GGUF save/load | Both (training + inference on Mac) | Metal (Apple Silicon) | MIT | Production/actively developed | **High for a Mac-only training/oracle path** — train tiny nets on the same Mac, no CUDA box needed for small experiments | https://github.com/ml-explore/mlx ; https://ml-explore.github.io/mlx/build/html/index.html |
| PyTorch MPS backend | PyTorch/Meta + Apple | shipping since PyTorch 1.12 | Runs standard PyTorch training graphs on Apple GPU via Metal Performance Shaders | Training | Metal (MPS) | BSD-3-Clause (PyTorch) | Production | Med — good for prototyping without a CUDA box, but MPS lags CUDA kernel coverage/perf | https://pytorch.org/blog/introducing-accelerated-pytorch-training-on-mac/ |
| CoreML | Apple | shipping | Converts/deploys trained models for on-device inference (Neural Engine/GPU/CPU) | Inference | Metal/ANE/CPU (Apple platforms) | Apple platform | Production | Med — better fit for larger auxiliary models (denoisers, classifiers) than a tiny in-shader MLP | https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html |
| MPSGraph | Apple | shipping | Graph-based ML compute API on Metal, predates/overlaps Metal 4 TensorOps | Both | Metal | Apple platform | Production | Low-Med — Metal 4 TensorOps is the more direct fit for in-shader work now | general knowledge **[UNVERIFIED]** |
| ONNX Runtime + DirectML EP | Microsoft | DirectML 1.15.2, opset ≤20 | Out-of-shader inference of ONNX models accelerated via DirectML on any DX12 GPU | Inference | D3D12 (Windows) | MIT (ORT) / MS (DirectML) | **Sustained engineering only** — new feature work moved to WinML | Low-Med for Luminex's tiny in-shader nets (this is for bigger, non-shader-embedded models); relevant only if a non-shader inference path is ever needed on the D3D12 backend | https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html |
| WinML | Microsoft | current push, successor to ORT-DirectML for Windows apps | Automatically selects best EP/hardware for ONNX inference on Windows | Inference | D3D12 (Windows), NPUs | MS platform | Active development | Low — same caveat as above, and still Windows-only | https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html (mentions WinML) |
| ONNX Runtime + CoreML EP | Microsoft/Apple ecosystem | current | Out-of-shader inference of ONNX models on Apple Neural Engine/GPU/CPU | Inference | CoreML (macOS/iOS) | MIT | Production | Low-Med — same "bigger auxiliary model" use case as CoreML | https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html |
| safetensors | Hugging Face | widely adopted | Simple, safe (no arbitrary code exec), memory-mappable tensor container format; supported natively by MLX, HF ecosystem, extended by `compressed-tensors` for quantized formats | Interchange | Format, not API-bound | Apache-2.0 | Production, de facto standard | **High** — best candidate as Luminex's own "trained weights" interchange format before a custom Slang-side loader packs them | https://huggingface.co/docs/transformers/en/quantization/compressed_tensors |
| ONNX | Linux Foundation / ONNX community | opset ~20+ | General ML model interchange graph format; used by ORT/DirectML/CoreML EPs, NTC's DX12 preview path references LinAlg-preview Agility SDK | Interchange | Cross-platform | Apache-2.0 | Production | Med — overkill for a tiny hand-authored MLP but the right format if Luminex ever imports a 3rd-party model | https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html |
| NVIDIA TensorRT QAT | NVIDIA | ongoing | Quantization-aware training workflow (fake-quantize nodes, fine-tune) to preserve accuracy at INT8/FP8 | Training (quantization step) | CUDA/TensorRT | NVIDIA SDK license | Production | Med — the QAT *methodology* (not the SDK itself) is directly reusable for tiny in-shader MLP quantization | https://developer.nvidia.com/blog/achieving-fp32-accuracy-for-int8-inference-using-quantization-aware-training-with-tensorrt/ |
| TorchAO | PyTorch/Meta | arXiv 2507.16099, active | PyTorch-native training-to-serving quantization/optimization toolkit (int8/fp8/int4 etc.) | Training→inference bridge | CUDA (PyTorch-native) | BSD | Active/production-track | Med — candidate for producing quantized weights on the training box before hand-porting to Slang | https://arxiv.org/pdf/2507.16099 |
| Metal Shader Converter | Apple | shipping tool | Converts DXIL (from DXC) into Metal IR/bytecode for D3D-on-Metal-style workflows; supports a large DXIL subset | Tooling (not training/inference itself) | DXIL→Metal | Apple platform | Production | Low for SM6.9/6.10 coop-vector/LinAlg content specifically — **unverified whether it lowers those intrinsics**; general DXIL coverage does not obviously include the new tensor-core intrinsics yet | https://developer.apple.com/metal/shader-converter ; **[UNVERIFIED]** re: coopvec/LinAlg intrinsic coverage — not confirmed by any fetched source |
| Game Porting Toolkit / D3DMetal | Apple (GPTK), Codeweavers/Valve (Wine/CrossOver base) | GPTK ongoing, `d3dmetal-native` community mirror | Wine + Apple's D3DMetal translates D3D11/12 calls to Metal on macOS, for game porting evaluation | Tooling (dev testing) | D3D12→Metal translation | Mixed (Apple binary + open-source Wine/CrossOver components) | Usable for evaluation, **not validated for driver-accurate SM6.9/6.10 coop-vector/LinAlg testing** | Low-Med — plausible smoke-test for "does my D3D12 code run at all" on a Mac, but not a substitute for real D3D12 hardware/driver validation, and coop-vector intrinsic support is **[UNVERIFIED]** | https://www.macgamerhq.com/virtualization/game-porting-toolkit/ ; https://github.com/utmapp/d3dmetal-native |
| MoltenVK | Khronos (LunarG/Apple-adjacent) | current | Vulkan-on-Metal translation layer | Tooling | Vulkan (subset) → Metal | Apache-2.0 | Production (for supported subset) | Low — **no evidence of `VK_KHR_cooperative_matrix` or `VK_NV_cooperative_vector` support**; not usable for coop-vector/coop-matrix testing on Mac | https://github.com/KhronosGroup/MoltenVK |
| Falcor | NVIDIA (NVIDIAGameWorks/NVIDIA) | active repo | Research rendering framework (D3D12/Vulkan), used as a base for many neural-rendering research papers | N/A (host framework) | D3D12, Vulkan | Apache-2.0-family (NVIDIA) | Production/research | Low direct fit (Luminex has its own RHI) — useful only as a pattern reference for research-grade neural passes | https://github.com/NVIDIA/Falcor |
| Nsight Compute / Nsight Systems | NVIDIA | current (13.3 docs) | GPU kernel/system profilers; expose tensor-core throughput via `sminst_executed_pipe_tensor.sum` and similar counters | Measurement | CUDA (NVIDIA only) | Free proprietary tool | Production | Med — good for the training-box side; gives no visibility into Metal/D3D12 coop-vector utilization | https://docs.nvidia.com/nsight-compute/NsightCompute/index.html |
| GPU capture/profiling for coop-vector/TensorOps (Metal/D3D12 native tools) | Apple / Microsoft | current | Xcode GPU Frame Capture (Metal, incl. ML command encoder passes) and PIX (D3D12) are the native equivalents to Nsight for the shader-side path | Measurement | Metal / D3D12 | Platform tools | Production | High — Luminex's existing `docs/guides/gpu-debugging.md` GPU-capture workflow is the natural place to extend for TensorOps passes | general knowledge **[UNVERIFIED]** |
| RunPod | RunPod Inc. | pricing snapshot 2026-09 | On-demand/secure-cloud GPU rental, containerized (Linux) pods | Hardware (training box) | CUDA (Linux containers); **no confirmed Windows/D3D12 instance type** | Commercial SaaS | Production | High for CUDA training, **Low/unconfirmed for D3D12 testing** | https://www.runpod.io/articles/guides/top-cloud-gpu-providers |
| Lambda Labs (Lambda Cloud) | Lambda | pricing snapshot 2026-09 | On-demand H100/B200/A100 GPU cloud, aimed at ML training/clusters | Hardware (training box) | CUDA (Linux) | Commercial SaaS | Production | High for CUDA training; not evaluated for Windows/D3D12 instances | https://tech-insider.org/runpod-vs-lambda-vs-vast-ai-2026/ |
| Vast.ai | Vast.ai | pricing snapshot 2026-09 | Peer-hosted GPU marketplace, widest/cheapest GPU variety, spot pricing | Hardware (training box) | CUDA (Linux, host-dependent) | Commercial marketplace | Production but variable host quality | Med-High for cheap CUDA training; host-dependent, no confirmed Windows/D3D12 support | https://computeprices.com/compare/runpod-vs-vast |
| RTX 5090 (consumer, local) | NVIDIA/AIBs | ~$5,000–$7,370 street price, 2026-09 | Local Blackwell GPU, 32GB GDDR7, full D3D12/Vulkan + CUDA on one box | Both (local training + local D3D12/Vulkan testing) | CUDA, D3D12, Vulkan (native Windows box) | N/A (hardware) | Shipping, prices inflated vs MSRP | **High** — only rental-vs-buy option that natively gives both CUDA *and* real D3D12/Vulkan driver testing on the same physical GPU | https://www.tomshardware.com/pc-components/gpus/nvidias-top-end-rtx-5090-gaming-gpu-now-costs-at-least-usd5-000-blackwell-cards-continue-to-endure-drastic-price-hikes |
| RTX PRO 6000 Blackwell (workstation) | NVIDIA/AIBs | ~$8,000–$20,000 street price, fluctuating 2026-09 | 96GB GDDR7 ECC workstation Blackwell card | Both | CUDA, D3D12, Vulkan | N/A (hardware) | Shipping, volatile pricing | Med — same capability as 5090 but only worth the premium if VRAM-bound (large NTC-style batch training); price is currently very volatile | https://videocardz.com/newz/nvidia-flagship-rtx-pro-6000-is-now-rtx-5080-cheaper-as-card-price-drops-to-7999 ; https://tech-insider.org/ca/nvidia-rtx-pro-6000-blackwell-price-2026/ |

## 2. Recommended minimal toolchain for a Metal-first renderer with a CUDA training box

**Pipeline:** (1) Author and train the tiny MLP in PyTorch on a rented or owned CUDA box —
`tiny-cuda-nn` (or plain PyTorch + TorchAO for QAT) as the reference implementation, since it is
the most battle-tested "ground truth" for fully-fused small-MLP training and gives correctness
numbers to check everything downstream against. (2) Export weights to **safetensors** — it is the
simplest, safest, already-native format for MLX and the HF ecosystem, and is trivial to write a
Slang/C++ loader for (flat fp16/fp32 buffers + a small JSON header); avoid ONNX unless a
third-party pretrained model is ever imported, since ONNX's operator graph is unnecessary overhead
for a hand-authored 2–4 layer MLP. (3) Build a **CPU numerical oracle** in Core/Asset (matching
Luminex's existing deterministic-tooling pattern, e.g. `Tools/TextureBake`) that loads the
safetensors weights and reproduces the forward pass bit-reasonably against the PyTorch output —
this is the same role `LDR-FLIP`/offline-comparison tooling already plays for temporal image
quality, just applied to network outputs instead of pixels, and is the only way to know whether a
divergence is a training bug or a shader bug. (4) Run inference **inside the Slang shader** using
Metal 4's `TensorOps`/`cooperative_tensor` path (WWDC26) as the primary target, since Luminex is
Metal-first and this is the only in-shader path Apple ships; treat the CPU oracle, not tensor-core
availability, as the source of truth so the same shader degrades gracefully on non-M5-class Apple
Silicon. (5) When the D3D12 backend lands, target the **D3D12 Linear Algebra Matrix** feature
(SM6.10), not the rejected/superseded Cooperative Vector (SM6.9) preview — AMD's own MiniDXNN
already migrated off Cooperative Vector for this reason.

**Concrete unknowns to verify before committing any of this to code:** (a) whether Slang's Metal
(MSL) backend actually lowers to Metal 4 `cooperative_tensor`/TensorOps today, or only to plain
compute — no primary confirmation either way was found; (b) whether Metal Shader
Converter or any DXIL path can round-trip SM6.9/6.10 coop-vector/LinAlg intrinsics to Metal (found
no evidence it can); (c) the exact minimum Apple Silicon tier (M5/A19 Pro only, or does the
TensorOps *inference* path degrade gracefully to older GPUs without the neural accelerator — the WWDC26
summary implies the fast path needs M5/A19 Pro but doesn't confirm a fallback); (d) whether any
rented CUDA-cloud instance (RunPod/Lambda/Vast.ai) offers a Windows/D3D12-capable GPU instance at
all — none of the three was confirmed to (all evidence points to Linux-only
containers), meaning D3D12/LinAlg testing likely requires either a locally owned/rented dedicated
Windows box or waiting for CI-side coverage.

## 3. Hardware decision matrix

- **Buy an RTX 5090 or RTX PRO 6000 (Blackwell) Windows box** if the goal is to develop and
  *validate* D3D12 LinAlg/Cooperative-Vector shader code with a real driver, since no cloud rental
  found in this research confirms Windows/D3D12-capable instances — cloud GPU rental (RunPod,
  Lambda, Vast.ai) as surveyed is Linux-container-only, CUDA-facing. Street prices as of 2026-09:
  5090 ≈ $5,000–7,400 (well above $1,999 MSRP); RTX PRO 6000 ≈ $8,000–20,000 and volatile.
- **Rent for the training/compute side** (RunPod H200 ≈ $4.4–4.6/hr, B200 ≈ $5.9–6.8/hr; Lambda
  A100 ≈ $2.1/hr, H100 ≈ $3.0/hr, B200 clusters ≈ $8.9–9.9/hr/GPU; Vast.ai spot A100 from
  ≈ $0.47/hr, H100 from ≈ $1.60/hr, RTX 4090 from ≈ $0.13/hr) — this is far cheaper than owning
  Blackwell hardware for bursty PyTorch/tiny-cuda-nn training runs, and is Linux/CUDA-only, which
  is fine for the training step.
- **Train on the Mac itself** for small experiments via MLX (native Metal, safetensors support,
  MIT license) or PyTorch-MPS — avoids needing any rented/owned NVIDIA hardware at all for early
  iteration, at the cost of slower kernels and less coverage than CUDA.
- **Parallels/VM Windows-on-Mac is not viable** for D3D12 GPU-accelerated testing — it lacks
  native GPU passthrough for DirectX; Game Porting Toolkit/D3DMetal is a translation layer, useful
  only as a rough compatibility smoke test, not for driver-accurate SM6.9/6.10 validation.
- **Net recommendation:** rent for CUDA/PyTorch training (cheapest, elastic); buy or otherwise
  access one real Windows+NVIDIA box only once the D3D12 backend and its coop-vector/LinAlg shader
  path are actually being implemented and need real-driver validation.

## 4. Sources (all observed 2026-09-14 unless noted)

- https://github.com/shader-slang/neural-shading-s26
- https://shader-slang.org/blog/2025/01/30/coop-vec-available/
- https://shader-slang.org/machine-learning/
- https://github.com/shader-slang/neural-shading-s25
- https://developer.nvidia.com/blog/differentiable-slang-a-shading-language-for-renderers-that-learn/
- https://github.com/shader-slang/slangpy
- https://github.com/shader-slang/slang-torch
- https://github.com/NVIDIA-RTX/RTXNS
- https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/LibraryGuide.md
- https://github.com/NVIDIA-RTX/RTXNTC
- https://developer.nvidia.com/blog/how-to-get-started-with-neural-shading-for-your-game-or-application/ (published 2025-11-13)
- https://developer.nvidia.com/rtx-kit
- https://developer.nvidia.com/blog/neural-rendering-in-nvidia-optix-using-cooperative-vectors/ (published 2025-04-17)
- https://github.com/NVlabs/tiny-cuda-nn
- https://drjit.readthedocs.io/en/latest/coop_vec.html
- https://github.com/mitsuba-renderer/drjit
- https://mitsuba.readthedocs.io/en/stable/src/inverse_rendering/pytorch_mitsuba_interoperability.html
- https://devblogs.microsoft.com/directx/cooperative-vector/ (published 2025-06-02)
- https://microsoft.github.io/hlsl-specs/proposals/0029-cooperative-vector/ (rejected/superseded status observed 2026-09-14)
- https://microsoft.github.io/DirectX-Specs/d3d/D3D12LinearAlgebraRuntimeFeatureSupport.html
- https://gpuopen.com/learn/minidxnn-v040-interactive-neural-texture-compression/
- https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_cooperative_vector.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkConvertCooperativeVectorMatrixNV.html
- https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_cooperative_matrix.html
- https://interplayoflight.wordpress.com/2026/02/21/adventures-in-neural-rendering-part-2-cooperative-vectors/
- https://developer.apple.com/videos/play/wwdc2026/359/ (Build real-time neural rendering pipelines with Metal, WWDC26 2026)
- https://developer.apple.com/videos/play/wwdc2025/262/ (Combine Metal 4 machine learning and graphics, WWDC25 2025)
- https://github.com/ml-explore/mlx
- https://ml-explore.github.io/mlx/build/html/index.html
- https://pytorch.org/blog/introducing-accelerated-pytorch-training-on-mac/
- https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html
- https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html
- https://huggingface.co/docs/transformers/en/quantization/compressed_tensors
- https://developer.nvidia.com/blog/achieving-fp32-accuracy-for-int8-inference-using-quantization-aware-training-with-tensorrt/
- https://arxiv.org/pdf/2507.16099 (TorchAO)
- https://github.com/KhronosGroup/MoltenVK
- https://www.macgamerhq.com/virtualization/game-porting-toolkit/
- https://github.com/utmapp/d3dmetal-native
- https://developer.apple.com/metal/shader-converter
- https://github.com/NVIDIA/Falcor
- https://docs.nvidia.com/nsight-compute/NsightCompute/index.html
- https://www.runpod.io/articles/guides/top-cloud-gpu-providers
- https://tech-insider.org/runpod-vs-lambda-vs-vast-ai-2026/
- https://computeprices.com/compare/runpod-vs-vast
- https://www.tomshardware.com/pc-components/gpus/nvidias-top-end-rtx-5090-gaming-gpu-now-costs-at-least-usd5-000-blackwell-cards-continue-to-endure-drastic-price-hikes
- https://videocardz.com/newz/nvidia-flagship-rtx-pro-6000-is-now-rtx-5080-cheaper-as-card-price-drops-to-7999
- https://tech-insider.org/ca/nvidia-rtx-pro-6000-blackwell-price-2026/
- https://github.com/SinaMajdieh/godot-neural-network [community prior-art, UNVERIFIED maturity]
- https://github.com/mohsenph69/Godot-Neural-Networks [community prior-art, UNVERIFIED maturity]

**Not verified in this notebook:**
- MPSGraph capabilities/license
- Xcode GPU Frame Capture / PIX coverage of Metal 4 ML passes and D3D12 LinAlg passes
- Metal Shader Converter's handling (or non-handling) of SM6.9/6.10 coop-vector/LinAlg DXIL intrinsics
- Whether any cloud GPU rental provider (RunPod/Lambda/Vast.ai) offers a Windows/D3D12-capable
  instance — absence of evidence here is not proof of absence, only that it was not found in the
  sources reviewed.
