# Pipeline state of the art for the M7–M11 areas

**Status**: Frozen — non-normative
**Research date:** 2026-09-14

Evidence notebook for the [rendering direction review](../2026-09-14-rendering-direction-review.md),
collected in one web-assisted pass over conference pages, engine documentation and open
repositories for the nine pipeline areas the [GPU-Driven Hybrid Rendering](../../roadmap/gpu-driven-hybrid-rendering.md)
part covers. The review reconciles conflicts between notebooks and takes precedence over any
judgment here; the "Verdict" section is an input to the review, not a roadmap decision. Items marked
**[UNVERIFIED]** were not confirmed against a primary source; recheck them before a plan depends on
them.

## 1. GPU-driven scene/visibility

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Nanite two-phase HZB occlusion culling + software rasterizer | UE5.0+ (2022–2026): Fortnite, Black Myth Wukong | "Nanite: A Deep Dive," SIGGRAPH 2021 (Karis et al.) [UNVERIFIED live] | None official; community reimplementation notes only | Any compute GPU (raster path) | **Med** — the two-phase-HZB algorithm is portable; the cluster-DAG software rasterizer + streaming is a multi-month subsystem | https://cs418.cs.illinois.edu/website/text/nanite.html ; https://trickybitsblog.github.io/2024/04/20/nanite.html |
| Meshlet/mesh-shader GPU-driven culling (task+mesh, per-meshlet frustum/cone culling) | Alan Wake 2 (Remedy Northlight, 2023, native mesh shaders confirmed) | Interplay of Light, "Meshlets and Mesh Shaders" (2025); GPUOpen meshlet-compression series | zeux/niagara, Thefefe/orbit, KhronosGroup/Vulkan-Samples `mesh_shader_culling`, nvpro-samples `gl_vk_meshlet_cadscene` | Mesh-shader GPU: NVIDIA Turing+, AMD RDNA2+, **Apple mesh shading only from M3/A17 Pro** | **Med** — Metal exposes mesh/object shaders (confirmed), but narrows the Apple-Silicon floor to M3+ | https://metalbyexample.com/mesh-shaders/ ; https://interplayoflight.wordpress.com/2025/05/05/meshlets-and-mesh-shaders/ ; https://gpuopen.com/learn/mesh_shaders/mesh_shaders-meshlet_compression/ |
| NVIDIA RTX Mega Geometry (cluster acceleration structures / CLAS for RT) | Tech demos, path-traced UE5 integrations (2025) | "Scale Up Ray Tracing With RTX Mega Geometry," GDC 2025 | github.com/NVIDIA-RTX/RTXMG | RTX GPU, DXR1.1+/`VK_NV_cluster_acceleration_structure`, best on Blackwell | **Low** — proprietary NVIDIA extension, no Metal analog | https://github.com/NVIDIA-RTX/RTXMG ; https://schedule.gdconf.com/session/scale-up-ray-tracing-in-games-with-rtx-mega-geometry-presented-by-nvidia/911194 |
| Classic GPU-driven indirect rendering (compute-culled draw lists → ExecuteIndirect/MDI) | Industry baseline since ~2015 (AC-lineage), most AAA engines today | D3D12 `ExecuteIndirect` sample (Microsoft) | github.com/microsoft/DirectX-Graphics-Samples | Any DX12/Vulkan GPU with compute + indirect draw | **High** — functionally what Metal Indirect Command Buffers already provide | https://github.com/microsoft/DirectX-Graphics-Samples |
| Apple Metal Indirect Command Buffers (ICB), Metal 4 unified compute encoder + low-overhead barriers | Apple's own recommended GPU-driven path for macOS/iOS | Apple WWDC25, "Discover Metal 4" | Apple sample code ("Encoding Indirect Command Buffers on the GPU") | Apple GPU family 3+ (broad support, below M3 too) | **High** — native, mature Metal path, directly matches M7's planned indirect submission | https://developer.apple.com/videos/play/wwdc2025/205/ |
| D3D12 Work Graphs (GPU mesh-node scheduling) | Emerging AMD/Microsoft demos (2024–2025) | AMD `WorkGraphsMeshNodeSample` (GPUOpen) | github.com/GPUOpen-LibrariesAndSDKs/WorkGraphsMeshNodeSample | D3D12 Work-Graphs-capable GPU | **Low** — D3D12-only, no Metal equivalent, bleeding-edge even natively; Metal's own ICB already covers the GPU-driven-command use case | https://github.com/GPUOpen-LibrariesAndSDKs/WorkGraphsMeshNodeSample |
| Bevy virtual geometry (Nanite-style Rust/wgpu meshlet renderer) | Bevy 0.14→0.16 (2024–2025), open source | jms55 blog, "Virtual Geometry in Bevy" (0.14/0.15/0.16) | github.com/bevyengine/bevy (meshlet module) | GPU with atomic storage-texture support; software-raster fallback for sub-pixel tris | **Med** — validates a solo/small-team Nanite-lite is feasible in ~1–2 years, but author reports BVH-based culling still unfinished at 0.16 — a scope caution | https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/ ; https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/ |
| Unity GPU Resident Drawer (SRP-Batcher successor, GPU instancing + occlusion) | Unity 6, default in URP/HDRP (2023–2026) | Unity manual [UNVERIFIED — page not reachable] | Closed source | DX12/Vulkan/Metal via SRP | N/A (proprietary) — cited only to confirm the category is now standard even in mid-tier engines | [UNVERIFIED] |
| Godot 4 Forward+ (clustered) renderer over MoltenVK | Godot 4.0+ (2023–2026), default desktop renderer | Godot docs [UNVERIFIED — page not reachable] | github.com/godotengine/godot | Vulkan/D3D12/Metal(via MoltenVK)-class compute GPU | **High** (existence proof) — clustered/GPU-driven techniques already run acceptably on Apple GPUs via Vulkan translation | [UNVERIFIED] |

**Area take:** M7's GPU scene tables + frustum culling + indirect submission + HZB occlusion is exactly the well-proven "2015-era GPU-driven pipeline," natively supported by Metal ICBs (High feasibility). Full Nanite-style meshlet virtual geometry (independent-research item) is Med feasibility at best and should stay narrowly scoped if ever attempted — Bevy's multi-release struggle with BVH culling is a real signal. Mesh-shader-based culling specifically requires M3+/A17 Pro — flag this against Luminex's currently unqualified "Apple Silicon" hardware baseline.

---

## 2. Local lighting

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| MegaLights (stochastic many-light direct lighting, fixed ray budget + denoiser) | UE5, experimental ~5.5, production by UE 5.8 (2024–2026) | Epic MegaLights documentation | Closed (UE source) | **Requires hardware RT**; Epic lists PS5/XSX/RT-PC — **no Mac/Metal listed** | **Low** — needs HW RT (M3+ only) and isn't even a platform Epic targets with this feature | https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine |
| ReSTIR DI (spatiotemporal reservoir resampling for many-light direct illumination) | RTXDI-based titles: Cyberpunk 2077 RT Overdrive, Portal RTX, Alan Wake 2 (partial), 2023–2026 | Bitterli et al., SIGGRAPH 2020 [UNVERIFIED live] | github.com/NVIDIA-RTX/RTXDI | Hardware ray tracing (BVH traversal) | **Low** — CUDA/DX12/Vulkan-only SDK; a Metal reimplementation is a large ground-up effort, gated to M3+ | https://developer.nvidia.com/rtx/ray-tracing/rtxdi |
| Clustered forward/deferred shading (3D light-cluster grid) | Doom Eternal (idTech), Frostbite-family (Battlefield) — industry baseline since ~2013 | Olsson/Billeter/Assarsson, "Clustered Deferred and Forward Shading," HPG 2012 [UNVERIFIED live] | Widely reimplemented (Filament, Bevy, Godot Forward+) | Any compute-capable GPU | **High** — exactly matches M7's planned clustered Forward+; no RT dependency; proven on Metal via Godot/Unity | Not independently re-confirmed live |
| Tiled forward+/deferred light culling (2D screen-tile bins) | Battlefield 3 (Frostbite 2, 2011) and many 2010s titles; still common in mobile-class engines | Harada/McKee/Yang, "Forward+," Eurographics 2012 [UNVERIFIED live] | Many public samples | Compute shaders | **High** — simpler stepping-stone than full clustering; low implementation risk | Not verified live |
| RT contact/soft shadows folded into local-lighting pipelines (SMRT, Shadow Map Ray Tracing) | UE5 Virtual Shadow Maps use SMRT for soft local-light shadows (2022–2026) | Epic VSM documentation | Closed | Same as RT rows above | **Med** — see Area 3 | https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine |
| Unity Forward+ rendering path | Unity 2022.2+/Unity 6 URP (2023–2026) | Unity manual [UNVERIFIED] | Closed | DX12/Vulkan/Metal | **High** (existence proof) — clustered Forward+ runs fine under Metal | [UNVERIFIED] |

**Area take:** Clustered Forward+ (M7) is the correct, well-trodden, High-feasibility choice with zero HW-RT dependency. MegaLights/ReSTIR-DI stochastic many-light rendering is the genuine 2024–2026 frontier, but it's RT-gated and, per Epic's own docs, not even Mac-targeted — correctly kept as independent research, not promoted into the core plan.

---

## 3. Shadows

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Virtual Shadow Maps (VSM) — 16k virtual res, 128×128px page cache, directional clipmaps, SMRT soft shadows | UE5.0+ default (2022–2026) | Epic VSM documentation | Closed (UE source-available) | **NVIDIA Maxwell+, AMD GCN+, Apple M2+, PS5/XSS, DX12 SM6.6 atomics or Vulkan** | **High** — Epic's own docs certify **Apple M2** as supported minimum, directly validating a VSM-style page-cached system on Luminex's full Metal 4 Apple-Silicon target | https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine |
| Cascaded Shadow Maps (CSM) | Ubiquitous baseline, 15+ years, still current AAA | No single canonical modern talk | Countless open implementations | Any GPU with basic shadow mapping | **High** — exactly M8's planned cascade+atlas shadows; lowest-risk, proven path | General knowledge |
| Shadow atlasing / cached shadows for many local lights | UE5 VSM page-cache system; Unity shadow-atlas allocator | Epic VSM docs (page-caching detail) | Closed/partial | Same as VSM row | **High** — directly complements CSM for point/spot lights | https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine |
| Percentage-Closer Soft Shadows (PCSS) | Default non-RT soft-shadow technique across UE/Unity/Godot | Fernando, NVIDIA, 2005 [UNVERIFIED live] | Numerous public shader implementations | Any shadow-map-capable GPU | **High** — implementable purely in a raster RHI, zero HW-RT dependency | https://en.wikipedia.org/wiki/Shadow_mapping (partial) |
| Ray-traced shadows (hardware BVH shadow rays) | Alan Wake 2 (2023, RT+path tracing+native mesh shaders); Cyberpunk 2077 RT Overdrive (2023) | Not independently confirmed live | — | Hardware RT GPU | **Med** — Metal RT exists since Metal 3, confirmed in Metal 4 (WWDC25), but **hardware-accelerated RT on Apple Silicon exists only from M3/A17 Pro** — M1/M2 lack RT cores | https://en.wikipedia.org/wiki/Alan_Wake_2 ; https://developer.apple.com/videos/play/wwdc2025/205/ |
| MegaLights + SMRT combined soft RT shadows for local lights | UE 5.5+/5.8 (2024–2026) | Epic MegaLights docs | Closed | HW RT; Epic lists PS5/XSX/RT-PC only, no Mac | **Low** for Metal — same HW-RT + non-Mac caveat as Area 2 | https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine |
| Apple/Metal RT-shadow hardware constraint | N/A (hardware constraint) | Apple M3 architecture disclosure (hardware-accelerated RT + mesh shading, implying absence on M1/M2) | — | M3/A17 Pro+ required for HW BVH traversal | Critical finding: since AGENTS.md targets "Apple Silicon" broadly, any RT-shadow item should be explicitly scoped M3+-only, with CSM/PCSS/VSM as the M1/M2-compatible fallback | https://en.wikipedia.org/wiki/Apple_M3 |

**Area take:** Most actionable finding of the whole review — Epic's own VSM docs certify Apple **M2** as the supported minimum, so M8's cascade+atlas+PCSS plan (and a VSM-style evolution of it) is High feasibility across the *entire* Metal 4 Apple-Silicon target, not just newest chips. RT shadows/MegaLights/SMRT/ReSTIR are all hard-gated to M3+ RT cores and should stay an explicit M3+-only extension, never a baseline requirement.

---

## 4. AO / SSR / screen-space

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| GTAO (Ground Truth AO) | Baseline AO in UE4/5, Unity HDRP, most AAA engines since ~2018 | Jimenez et al., SIGGRAPH course 2016 | github.com/GameTechDev/XeGTAO | Any compute GPU, no RT needed | **High** — pure compute shader, no RT dependency | https://github.com/GameTechDev/XeGTAO |
| XeGTAO (Intel reference implementation) | Reference used by several indie/hobby engines; informed Godot's GTAO | Engineering repo | github.com/GameTechDev/XeGTAO | Compute-capable GPU | **High**, but repo **archived/unmaintained since 2024-04-22** — reference only | https://github.com/GameTechDev/XeGTAO |
| VBAO / VBGTAO (visibility-bitmask AO) | Bevy, merged 2024-10-02, default since Bevy 0.15 | Therrien/Levesque/Gilet, "Screen Space Indirect Lighting with Visibility Bitmask," Vis Comput 2022 / arXiv 2301.11376 | bevyengine/bevy PR #13454 (Rust/WGSL) | Compute GPU, no RT | **High** — same compute-shader class as GTAO, better thin-geometry handling; a credible upgrade target over XeGTAO given the latter's archival | https://github.com/bevyengine/bevy/pull/13454 ; https://arxiv.org/abs/2301.11376 |
| SSR first pass, RT/software-trace fallback tier | UE5 Lumen, all UE5 Lumen titles (2022–2026) | Epic, "Lumen Technical Details" | Proprietary | Software RT: SM6-class GPU; HW RT: RTX2000+/RX6000+/PS5/XSX | **Med** — the screen-trace-first pattern is portable; a full software-RT mesh-tracing fallback is a large undertaking | https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine |
| RT reflections with fallback chain | Unity HDRP (v17.0.4, current 2026) | HDRP manual, "Ray-Traced Reflections" | Proprietary; documented "reflection hierarchy" fallback | DXR-capable GPU | **Med** — same hybrid-fallback pattern, moderate engineering cost | https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Ray-Traced-Reflections.html |
| SSGI (screen-space GI) | Common cheap GI layer in UE5 (non-Lumen) and Unity HDRP | No canonical paper confirmed | Various engine-internal | Compute-capable GPU | **High** — same screen-space compute family as SSR/GTAO | [UNVERIFIED] |
| Spatially-hashed / sparse RT-assisted AO (research direction) | Not shipped in any named AAA title; research-stage late 2025 | Interplay of Light blog, "Spatial hashing for raytraced AO" (2025-11-23) | No confirmed public repo | Hardware RT GPU | **Low** — requires RT hardware, still a research technique | https://interplayoflight.wordpress.com/ |
| "RT is replacing screen-space effects" claim | **False as a blanket claim** — Lumen and HDRP both keep screen-space traces/SSR as the *first* pass or fallback even with HW RT enabled (covers RT-mesh/full-mesh geometry mismatches) | Epic Lumen docs (2026); Unity HDRP docs (2026) | N/A | N/A | Assessment: keep screen-space as primary/fallback tier rather than betting solely on RT | Same two URLs above |

**Area take:** M8's GTAO + SSR-with-probe-fallback plan matches shipped practice closely; VBGTAO is a credible near-term upgrade over GTAO given XeGTAO's archival status. The "RT replaces screen-space" narrative doesn't hold even in engines with mature HW RT — hybrid screen-space-first is still the norm.

---

## 5. Transparency / OIT / volumetrics

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Weighted Blended OIT | Widely used as a cheap approximate OIT technique, no per-pixel storage | McGuire & Bavoil, JCGT 2013 — still the reference | Public reference shaders widely mirrored | Any GPU with blending | **High** — trivial to implement, no exotic hardware features | https://en.wikipedia.org/wiki/Order-independent_transparency |
| Per-pixel linked lists (PPLL) | Practical since ~2011 driver atomics improvements; used in some UE4-era custom passes | No single canonical 2023–2026 paper found | Well-documented pattern, portable to Metal storage buffers + atomics | GPU with fast atomic ops on UAV/storage buffers | **Med** — implementable on Metal 4 storage buffers/atomics, bandwidth-costly, needs careful bounds handling | https://en.wikipedia.org/wiki/Order-independent_transparency |
| MBOIT (moment-based OIT) | Research-grade; **no confirmed shipped AAA title** | Münstermann et al., JCGT 2018; no confirmed 2023–2026 follow-up shipped | Reference implementations exist (not independently verified) | Any GPU with multi-target blending | **Med** — algorithmically simple but approximate, extra render targets add cost for one dev | https://en.wikipedia.org/wiki/Order-independent_transparency |
| "Spatially-hashed OIT" | **Could not confirm as an established technique** — Nvidia Research's current publication list (2023–2026) shows no matching title; likely conflated with spatial-hashing RT-AO research (Area 4) | **[UNVERIFIED]** | **[UNVERIFIED]** | — | — | Flag: treat this specific term as unconfirmed until a primary source is found |
| Froxel-based volumetric fog | UE5 Volumetric Fog, camera-frustum-aligned density/lighting grid, shipped since UE4.19+, carried into UE5 | Bevington/Wronski-style froxel fog (GDC-era, no single Epic-cited paper) | Well-documented publicly by multiple engines | Compute shader + 3D texture support | **High** — well-understood compute pattern, portable to Metal 4/Slang; matches M8's plan directly | https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine |
| Sky Atmosphere / physically based sky | UE5 Sky Atmosphere; Unity HDRP Physically Based Sky (v17.0.4, current 2026) | Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique," SIGGRAPH 2020 course; also Bruneton & Neyret 2008 | Public reference implementations (Hillaire's own demo, widely mirrored) | Compute-capable GPU, precomputed 3D LUTs | **High** — precomputed-LUT technique fits Luminex's existing compute-pass architecture | https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/physically-based-sky-volume-override-reference.html |
| Volumetric clouds (ray-marched) | UE5 Volumetric Clouds, integrated with Sky Atmosphere | Same Hillaire lineage; also Schneider, "Horizon Zero Dawn Volumetric Clouds," SIGGRAPH 2015 | No official Epic open reference; Schneider's slides public | Compute-capable GPU, ray marching through noise volumes | **Med** — well-documented but a significant standalone feature to tune solo | [UNVERIFIED — Epic documentation page not reachable] |
| Stochastic/any-hit transparency in RT pipelines | Standard DXR/Metal RT guidance: mark opaque geometry OPAQUE, keep any-hit shaders minimal (expensive, runs per-candidate-hit) | Nvidia, "RTX Best Practices" blog (ongoing, current 2026) | N/A (guidance) | RT-capable GPU with any-hit support | **Low** for Luminex today — depends on RT being implemented first (M10) | https://developer.nvidia.com/blog/rtx-best-practices/ |

**Area take:** M8's "sorted premultiplied transparency" (rather than a full OIT scheme) is the pragmatic, industry-standard choice — no AAA title was found shipping MBOIT or a "spatially-hashed OIT" in production, so simple sorted blending is not a weak spot, it's what the field actually does. Froxel fog and Hillaire-style Sky Atmosphere are both well-trodden and match M8's plan closely.

---

## 6. Geometry / LOD

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Nanite (core virtualized geometry) | All UE5 Nanite titles, 2022–2026 (docs current for UE 5.8) | Karis et al., "Nanite: A Deep Dive," SIGGRAPH 2021 | Proprietary | Any Nanite-capable GPU (SM5+, HW RT for Nanite ray tracing) | **Low–Med** for a full clone — multi-year Epic team effort; Luminex's scoped-down offline-LOD plan is far more tractable | https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine |
| Nanite Foliage | Present in current UE 5.8 docs; exact introducing version unconfirmed live (generally UE5.4-era) | Epic dev docs (2026) | Proprietary | Same as Nanite | **Low** — narrow, deep feature, low priority vs. Luminex's own scope | Same URL |
| Nanite Skeletal/Skinned Meshes | Present in current UE 5.8 docs (one draw call, animation LODs instead of geometry LODs); introducing version unconfirmed (generally UE5.4–5.5-era, experimental) | Epic dev docs (2026) | Proprietary | Same as Nanite | **Low** — one of the hardest Nanite extensions, well outside near-term solo scope | Same URL |
| Nanite Tessellation / Dynamic Displacement | Present in current UE 5.8 docs; introducing version unconfirmed (generally UE5.5–5.6-era) | Epic dev docs (2026) | Proprietary | Same as Nanite | **Low** — deep integration with Nanite's cluster pipeline | Same URL |
| Mesh shaders in shipped AAA titles | Alan Wake 2 (2023, Remedy/Northlight) — confirmed first title with native mesh-shader support, used for primary rasterization | Nvidia/AMD/Intel mesh-shader hardware announcements 2018–2022 (general) | N/A (proprietary engine) | RTX20+/RX6000+/PS5/XSX/Intel Arc | **Med** — Metal 4 has its own mesh/object-shading path; conceptually portable but needs a dedicated Slang/Metal build | https://en.wikipedia.org/wiki/Alan_Wake_2 |
| meshoptimizer clusterization/LOD suite (zeux) | Used across many indie/AAA-adjacent pipelines for offline mesh processing | Repo changelog is primary source | github.com/zeux/meshoptimizer | CPU-side offline tool; GPU-side consumption needs meshlet/index support | **High** — directly adoptable library; matches M9's "offline LOD + transitions" and "meshlets" items almost exactly | https://github.com/zeux/meshoptimizer |
| meshoptimizer `clusterlod.h` ("hierarchical clustering similar to Nanite") | Landed v1.0 (2023-12-08); v0.24 (2023-06-12) added `meshopt_buildMeshletsSpatial` + `meshopt_partitionClusters`; v1.1 (2024-04-02) meshlet codec compression; v1.2 (2024-06-30) `clodBuildHierarchy` DAG BVH | Repo changelog | github.com/zeux/meshoptimizer — confirmed functions: `meshopt_buildMeshlets*`, `meshopt_partitionClusters`, `clusterlod.h`, `clodBuildHierarchy` | CPU library, any GPU with meshlet/index-buffer support | **High** — the single most directly reusable "Nanite-like" open building block for M9's meshlet/visibility-buffer roadmap | https://github.com/zeux/meshoptimizer/releases (v0.19→v1.2 confirmed) |
| Visibility-buffer vs deferred vs Forward+ in 2024–2026 AAA practice | Could not confirm current Activision/Frostbite/id Tech talks live | Burns & Hunt, "The Visibility Buffer," JCGT 2013 — foundational, content unconfirmed live | Public reference implementations widely exist | Any GPU supporting programmable vertex shading + per-pixel material ID | **Med** — well-known pattern matching M9's "visibility-buffer experiments," needs care on Apple TBDR | http://jcgt.org/published/0002/02/04/ [empty fetch] |
| Apple TBDR implications for deferred/VB/Forward+ choice | Apple GPUs are tile-based deferred renderers; standard guidance favors Forward+/tile-based culling or a thin visibility buffer over a fat G-buffer, since large G-buffers cost bandwidth even with on-chip tile memory | Apple Metal tile-shading/programmable-blending WWDC sessions (existence not independently confirmed live) | N/A | Apple Silicon GPU (Metal 3+ tile shading) | **High relevance** but **[UNVERIFIED live]** — Apple's own TBDR doc pages returned only titles; recommend a follow-up direct check before committing roadmap language | [UNVERIFIED] — https://developer.apple.com/documentation/metal (root only) |

**Area take:** M9's offline LOD + transitions + meshlets is unusually well-matched to a real, actively maintained open building block: zeux/meshoptimizer's `clusterlod.h` (added exactly for "hierarchical clustering similar to Nanite," Dec 2023) plus `meshopt_partitionClusters`/`meshopt_buildMeshletsSpatial`. This is High feasibility and should anchor M9's implementation rather than a from-scratch clusterer. Mesh shaders remain Med (M3+ hardware floor); visibility-buffer vs deferred experiments are appropriately scoped as "experiments," matching that the field itself hasn't settled this for TBDR GPUs.

---

## 6b. Ray tracing

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Inline ray queries (`VK_KHR_ray_query`/DXR inline) | Adds RT effects inside existing compute/fragment shaders without a full RT pipeline; hybrid-rendering example: Wolfenstein: Youngblood (id Software) | Khronos Group, "Ray Tracing in Vulkan" developer blog | N/A (API feature) | Any RT-capable GPU (RTX20+/RX6000+/consoles/Apple M3+) | **High** relative to full pipeline — matches M10's own "BLAS/TLAS + inline ray queries" item and is the simpler surface to integrate into an existing render graph | https://www.khronos.org/blog/ray-tracing-in-vulkan |
| Full RT pipeline (dedicated shader-binding-table dispatch) | Used where complex material/shader diversity or programmable intersection control is needed (e.g. full path tracers) | Same Khronos source | N/A | Same hardware, higher driver/shader complexity | **Med** — more machinery (SBT management, multiple shader stages) than inline queries for comparable initial payoff | https://www.khronos.org/blog/ray-tracing-in-vulkan |
| Mandatory hardware RT for core rendering | Doom: The Dark Ages (id Software, 2025) — id Tech 8 confirmed as "the first generation of id Tech engine that requires dedicated raytracing-enabled hardware," due to fully ray-traced GI | Not independently confirmed via a dedicated talk; corroborated by Wikipedia's id Tech 8 article and RTX bundle promotions | N/A (proprietary) | Mandatory HW RT GPU (RTX20+/RX6000+/PS5/XSX-class), no non-RT fallback | Signal only — the clearest industry proof that HW RT for GI is now a viable *baseline*, validating M10's direction even though Luminex's scope (RT reflections) is far narrower | https://en.wikipedia.org/wiki/Id_Tech_8 |
| RT reflections/shadows/AO baseline on current-gen consoles | PS5 Pro — confirmed "twice as fast ray tracing performance" vs. base PS5; ~50 Pro-patched titles expected | Sony PS5 Pro technical presentation (not independently fetched) | N/A | PS5 Pro custom RDNA-derived HW RT | Context only — confirms RT is now a within-generation differentiator, reinforcing that RT-capable Apple Silicon (M3+) is on a comparable trajectory | https://en.wikipedia.org/wiki/PlayStation_5_Pro |
| Denoising: Nvidia NRD (ReBLUR/ReLAX/SIGMA) | 15+ AAA/ProVis titles (Autodesk Aurora, Enscape, Lumion); current v4.18.0 | Nvidia developer docs | github.com/NVIDIAGameWorks/RayTracingDenoiser (open source, C++/HLSL, "SH" mode competitive with DLSS-RR quality) | Any RT-capable GPU producing a per-pixel G-buffer guide | **Med** — API-agnostic and could in principle be ported to Metal 4/Slang; a substantial standalone integration but the most credible open denoiser to study/port for M10's "native denoiser" | https://github.com/NVIDIAGameWorks/RayTracingDenoiser |
| DLSS Ray Reconstruction (AI denoiser replacing NRD-style denoisers) | Debuted DLSS 3.5 (Sept 2023): Cyberpunk 2077 Phantom Liberty, Portal RTX, Alan Wake 2; improved in DLSS 4.5 (2026) | Nvidia DLSS 3.5/4.5 announcements | Proprietary, Nvidia-only | RTX-series GPU, path-traced/full-RT content | **Low** — proprietary AI model, not portable to Metal 4; confirms Luminex needs its own denoiser (NRD-style or custom) | https://en.wikipedia.org/wiki/Deep_Learning_Super_Sampling |
| Metal RT hardware acceleration on Apple Silicon | M3 (2023): first Apple Silicon gen with HW-accelerated RT. M4 (2024): Apple claims "twice as fast" vs. M3. M5 (base Oct 2025; Pro/Max Mar 2026; Ultra Aug 2026): "third-generation ray-tracing engine," up to 45%/35%/30% faster than M4 by tier | Apple product announcements 2023–2026 (dedicated WWDC session content not independently fetched) | N/A (proprietary hardware+API) | M3/A17 Pro and later | Context row — three generations of hardware RT with compounding perf gains is a reasonable basis for M10's RT roadmap, but confirms M10 is inherently an M3+-only milestone | https://en.wikipedia.org/wiki/Apple_M5 ; https://en.wikipedia.org/wiki/Apple_M4 ; https://en.wikipedia.org/wiki/Apple_M3 |
| Stochastic/any-hit RT transparency handling | Standard DXR/Metal RT practice (see Area 5) | Nvidia RTX Best Practices blog (2026) | N/A | RT-capable GPU with any-hit support | **Low** for Luminex today — depends on M10 RT landing first | https://developer.nvidia.com/blog/rtx-best-practices/ |

**Area take:** M10's "inline ray queries" choice over a full RT pipeline is directly validated by Khronos guidance and matches shipped hybrid-rendering practice (id Software's own Wolfenstein: Youngblood). NRD is the most credible open denoiser reference to study for the "native denoiser" item. The single hardest constraint across this whole review: M10 is unavoidably an M3+-only milestone, and three Apple Silicon generations of compounding RT perf gains (M3→M4→M5) make that a reasonable, not premature, bet by the time Luminex reaches M10.

---

## 7. Global illumination

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| Lumen Software RT (SDF cone-trace) | UE5.0–5.6, Fortnite et al. (2022–2026) | "Lumen: Real-Time Global Illumination," GDC 2022 | UE5 source (source-available) | SM6-class GPU, **no HW RT needed** | **Med** — portable algorithm, but mesh SDFs + global distance field + surface cache is a multi-month subsystem | dev.epicgames.com (verified) |
| Lumen Hardware RT | UE5.4–5.6 | Same | UE5 source | RTX2000+/RX6000+/PS5/Series X | **Low** — Luminex's RHI has no acceleration-structure/HW-RT pipeline at all yet (prerequisite: M10) | dev.epicgames.com (verified) |
| RTXGI v1 DDGI (probe-based) | Metro Exodus Enhanced Edition (4A Games), Fortnite RTXGI plugin | Majercik et al., "Dynamic Diffuse GI," HPG 2019 | github.com/NVIDIAGameWorks/RTXGI-DDGI (legacy, still hosted) | Compute-only viable (RT optional) | **High** — deterministic probe grid, decades of prior art, no HW RT dependency | github.com/NVIDIA-RTX/RTXGI README (verified) |
| RTXGI v2.0 — SHaRC (world-space hash radiance cache) + NRC | Nvidia SDK, 2025 | RTX Neural Rendering blog, 2025 | github.com/NVIDIA-RTX/RTXGI (v2.0), github.com/NVIDIA-RTX/NRC | SHaRC: any compute GPU. NRC: Tensor Cores (Turing+) | SHaRC **Med** (hardware-agnostic, portable); NRC **Low** (needs on-device NN training/inference, no Metal equivalent) | github.com (README confirms v2.0 "replaces probe-based irradiance caching with world-space radiance caches") |
| Surfel GI ("GIBS") | EA SEED (Frostbite-adjacent research) | SIGGRAPH 2021 Advances in Real-Time Rendering course | W298/SurfelGI (Falcor port), AntonioNoack/Unity-SurfelGI, WatchDogStudios/O3DESurfelGI | Compute-only, no HW RT | **Med-High** — several independent open ports exist; surfel placement/coverage/irradiance accumulation maps cleanly onto a compute-pass render graph | GitHub search (verified) |
| Radiance Cascades (2D/screen-space) | Path of Exile 2 (Grinding Gear Games, Alexander Sannikov, 2024) | Sannikov's blog + unpublished preprint (never formally peer-reviewed) | github.com/Raikiri/RadianceCascadesPaper; dozens of community ports (Unity URP, Godot, Bevy 2D) | Fragment/compute shaders only, no HW RT | **High** to prototype — cheapest, huge reference-code base, deterministic and directly comparable to a path-traced baseline | mini.gmshaders.com, github.com/Raikiri (verified) |
| Radiance Cascades — 3D extensions | Community research only, 2024–2026, **no shipped AAA/engine title found** | mxcop/src-dgi, "Surfel Radiance Cascades Diffuse GI" (active as of Sept 2026) | github.com/mxcop/src-dgi | Compute | **Med** — promising, small-scale, good validation target, but unproven at production scale | GitHub search (verified) |
| AMD Brixelizer / Brixelizer GI | FidelityFX SDK 1.1.0 (Jul 2024) → 1.1.4 (May 2025); **`brixelizer` directory absent from the current SDK tree (v2.3.0, Jun 2026)** | GDC 2024 AMD talk; gpuopen.com/fidelityfx-brixelizer | GPUOpen-LibrariesAndSDKs/FidelityFX-SDK | SDF-based, no HW RT required as shipped | **Med** — well-documented technique, but AMD itself appears to have quietly dropped/superseded it; deprioritize | Release notes and repo tree listing (verified live) |
| AMD FSR Radiance Caching (ML successor) | FidelityFX SDK v2.1.0 (Dec 2025), Technical Preview | Part of "AMD FSR Redstone" ML initiative, 2025 | Kits/FidelityFX/radiancecache (in-tree, HLSL) | Requires a full path tracer + online NN training/inference | **Low** — no Metal ML-inference-in-shader equivalent, plus a path-tracer prerequisite | FidelityFX-SDK v2.1.0 release body (verified) |
| ReSTIR GI / ReSTIR PT | Cyberpunk 2077 Overdrive [UNVERIFIED live], Alan Wake 2 path tracing (confirmed), Indiana Jones and the Great Circle (confirmed, mandatory HW RT + optional full path tracing), Star Wars Outlaws [RT details UNVERIFIED] | Ouyang et al., "ReSTIR GI," HPG 2021; Lin et al., "ReSTIR PT," SIGGRAPH Asia 2022 | github.com/NVIDIA-RTX/RTXDI | Mandatory HW RT (RTX20xx+/RX7000+) | **Low** — Luminex's RHI has no BVH/acceleration-structure abstraction; largest possible lift of any GI approach here | developer.nvidia.com/rtx/ray-tracing/rtxdi ; en.wikipedia.org/wiki/Indiana_Jones_and_the_Great_Circle (verified) |

**Recommendation (explicit):** DDGI-style world-space probe irradiance/distance volumes are the single most solo-feasible **and validatable** GI floor for Luminex on Metal 4. The technique needs no hardware-RT/BVH abstraction — probes can be relit via rasterized cubemap capture or compute-shader SDF/cone raymarch, both fitting the existing compute-pass render graph and transient-pool model. It has the deepest, best-documented prior art here (Majercik 2019, RTXGI-DDGI legacy SDK, Godot SDFGI as an architectural cousin), and is quantitatively validatable the same way Luminex already validates TAA/MetalFX: bake a reference path-traced/long-accumulated ground truth offline and FLIP-compare it via the existing `Tools/TemporalCompare` machinery. For the "one dynamic GI cache" tier above the floor, Surfel GI (GIBS-style) is the most credible next step for the same reasons — still HW-RT-free, still deterministic, with several independent open ports to study. Radiance Cascades is the most exciting research direction and cheapest to prototype, but its production track record is effectively one title (Path of Exile 2, largely 2D/isometric) and its 3D extension is unproven community work — correctly kept as independent research rather than the committed M11 floor. ReSTIR GI/PT and NRC/AMD-Radiance-Caching should stay in "independent research" until/unless Luminex's RHI grows a hardware-RT abstraction (i.e., post-M10).

---

## 8. Streaming / residency

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| UE5 Streaming Virtual Texturing (SVT) | Epic Games, UE5.0–5.6 | Epic GDC VT talks | UE5 engine source (source-available) | SM6-class GPU, tile-based, ≥2 texture fetches/sample | **Med** — concept is portable, but the full page-table + feedback + tile-cache stack is a large multi-month subsystem | dev.epicgames.com/.../streaming-virtual-texturing-in-unreal-engine (verified) |
| Godot virtual texturing | **Not implemented** — proposal (#10176) discussing VT/Brixelizer GI/HDDAGI/SDFGI tradeoffs was closed without core adoption | — | — | — | Negative data point: even a mature open engine skips full VT | github.com/godotengine/godot-proposals/issues/10176 (verified) |
| Bevy virtual texturing | **No tracked issue/PR found** as of Sept 2026 | — | — | — | Negative data point | Repository issue/PR search on bevyengine/bevy (verified, negative result) |
| DirectX 12 Sampler Feedback | Microsoft, GA since Win10 2004 (2020); used by UE5 SVT's feedback path | Microsoft DirectX dev blog | GameTechDev/SamplerFeedbackStreaming (Intel reference sample) | DX12 Feedback Tier 1.0 (Turing+/RDNA2+) | **Low** as a direct port — D3D12-specific, no Metal analogue; the *concept* (GPU reports which tiles it sampled) would need hand-rolling via a compute-written tile-usage buffer | github.com/GameTechDev/SamplerFeedbackStreaming (verified) |
| DirectStorage + GDeflate GPU decompression | Microsoft, DirectStorage 1.1+ GA since 2022; Xbox since 2020 | — | microsoft/DirectStorage (incl. GpuDecompressionBenchmark); community GDeflate ports (ProjectKML/gdeflate-rs, others) | NVMe SSD + GPU compute decompression | **Med** — GDeflate itself is an open, documented format with cross-platform community decoders; a Metal compute-shader GDeflate decoder is plausible even though the DirectStorage *API* is Windows-only | github.com/microsoft/DirectStorage (verified) |
| Nvidia RTX IO | Nvidia, 2020, effectively vendor branding of the DirectStorage GPU-decompression path | — | Closed/driver | RTX GPU + NVMe | N/A on Metal — vendor-specific, conceptually superseded by the open GDeflate format itself | No separate open repo found |
| Apple Metal fast resource loading (`MTLIOCommandQueue`) | Apple, shipped since macOS Ventura/iOS 16 (2022), continues under Metal 4/macOS 26 | WWDC sessions on Metal fast resource loading | Apple sample code | Apple Silicon + NVMe SSD (all modern Macs) | **High** — the native, already-present primitive Luminex should build residency/streaming on directly; no cross-platform gap to bridge | developer.apple.com/documentation/metal/mtliocommandqueue [UNVERIFIED — documentation page not reachable; high confidence from prior knowledge] |

**Area take:** M11's residency/streaming item should target Apple's native `MTLIOCommandQueue` fast-loading primitive plus a hand-rolled tile-usage-feedback buffer rather than attempting DX12-style Sampler Feedback/full virtual-texturing parity — full VT is Med feasibility and even Godot and Bevy don't have it (Godot's own proposal was closed; Bevy has no tracked work), which is useful negative evidence that this is correctly scoped as a later, smaller-footprint milestone rather than a bigger one.

---

## 9. Frame generation / temporal

| Approach | Who ships it | Key talk/paper | Open implementation | Hardware needs | Solo-feasibility (Metal 4) | Source |
|---|---|---|---|---|---|---|
| UE5 native Temporal Super Resolution (TSR) | Epic Games, UE5.0–5.6 | Epic TSR docs/talks | UE5 engine source | Cross-platform: D3D11/12, Vulkan, **Metal**, consoles; SM6-class | **High as validation** — explicitly confirms hand-tuned (non-ML) temporal upscaling remains first-class and shippable; TSR explicitly does **not** do frame generation/extrapolation, same category as Luminex's existing native TAA/TAAU | dev.epicgames.com/.../temporal-super-resolution-in-unreal-engine (verified) |
| UE5 "Frame Generation" | Delivered only via third-party plugins (Nvidia DLSS-G, AMD FSR3 FG) integrated into UE5.1–5.4's frame scheduling; **no proprietary Epic frame-gen found** | — | Closed (vendor plugins) | Vendor-specific | Confirms industry pattern: frame-gen is vendor-SDK territory, not engine-native, even for Epic | [UNVERIFIED — documentation page not reachable] |
| DLSS Frame Generation (DLSS 3/3.5) | Nvidia, 2022–2024, 100+ titles incl. Cyberpunk 2077, Alan Wake 2 | Nvidia GDC/CES keynotes | Closed (wrapped by Nvidia Streamline SDK) | RTX 40-series+ (Optical Flow Accelerator) | **Low** — fully proprietary, vendor-locked, no Metal path | [UNVERIFIED — prior knowledge] |
| AMD FSR 3/3.1 Frame Generation | AMD, 2023–2025, open-source | GPUOpen technical docs | GPUOpen-LibrariesAndSDKs/FidelityFX-SDK — `Kits/FidelityFX/framegeneration` (confirmed present) | Any DX12/Vulkan GPU (non-ML); FSR4 needs RDNA4 | **Med** — algorithm and optical-flow/extrapolation logic are openly documented and study-able even though the SDK targets D3D/Vulkan | Repo tree listing (verified) |
| Intel XeSS 3 (XeSS-FG + XeLL) | Intel, 2025–2026, explicitly cross-vendor (any GPU with SM6.4/DP4a) | Intel developer guides | github.com/intel/xess (confirmed live: XeSS-SR, XeSS-FG, XeLL, XeSS Inspector) | Broad — works on non-Intel GPUs | **Med** — genuinely cross-vendor design and openly published dev guides make this the most instructive frame-gen reference, even absent a Metal build | github.com/intel/xess README (verified) |
| Nvidia Reflex / Reflex 2 (Frame Warp) | Nvidia, Reflex GA 2020; Reflex 2 Frame Warp listed as "coming soon" on Nvidia's own live page | Nvidia developer content | Closed | Nvidia GPU | **Med** — the *concept* (re-sample input, warp the frame just before scanout) is engine-side and portable without any vendor SDK | nvidia.com/en-us/geforce/technologies/reflex/ (verified) |
| AMD Anti-Lag 2 | AMD | — | github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK (confirmed: driver-side Anti-Lag 1 vs. game-integrated Anti-Lag 2) | Any GPU (game-side SDK integration) | **High** — conceptually simple (delay input sampling to just before it's needed), genuinely implementable solo, matches Reflex 2's Frame Warp philosophy; a cheap independent-research win | github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK README (verified) |
| "TAA is dead"/TAA-replacement discourse | Community-driven (Digital Foundry retrospectives, Threat Interactive's 2023–2024 UE5/Lumen/TAA critique videos widely discussed), pushing toward ML upscalers replacing hand-tuned TAA resolve | — | — | — | Debate, not a technique — generic ghosting/blur/transparency criticism independently corroborated by Wikipedia's TAA article, but the specific 2024–2026 creator discourse is [UNVERIFIED live] | en.wikipedia.org/wiki/Temporal_anti-aliasing (verified) |

**Area take:** Frame generation is vendor-SDK-dominated (DLSS-FG/FSR-FG/XeSS-FG) or engine-native-but-plugin-delivered — correctly low priority as independent research, not roadmap-committed. Anti-Lag-2-style input-latency compensation is the more solo-feasible, higher-value item in this cluster and could be pulled forward as a cheap win independent of any GPU frame interpolation.

---

## Verdict on M7–M11

**M7 (GPU scene tables, GPU culling + indirect submission with CPU oracle, HZB temporal occlusion, clustered Forward+ lights) — Keep, unreshaped.** This is the industry-standard 2015-era GPU-driven pipeline, and it maps directly onto Metal's Indirect Command Buffers (Apple's own recommended path, WWDC25) and Metal 4's low-overhead barrier API — High feasibility with no hardware-RT dependency. Clustered Forward+ is proven across Frostbite, idTech, Godot and Unity; nothing in 2023–2026 practice supersedes it as a *baseline*. This is the plan's most conservative-in-a-good-way milestone.

**M8 (cascades/PCSS, local shadow atlas, GTAO, SSR + probe fallback, sorted transparency, froxel fog) — Keep, minor reshape.** Epic's own VSM docs certify **Apple M2** as a supported minimum, meaning M8's shadow plan could be reshaped toward a VSM-style page-cached atlas rather than a flatter cascade+atlas, since the harder VSM variant is already proven on Luminex's full hardware floor — worth considering as a stretch target within M8 rather than deferring it. GTAO should target VBGTAO (Bevy's bitmask AO, newer and better for thin geometry) over plain GTAO/XeGTAO, since XeGTAO has been unmaintained since April 2024. SSR-with-probe-fallback and froxel fog/Sky-Atmosphere are exactly what shipped engines do; "sorted premultiplied transparency" over full OIT is correct — no AAA title was found shipping MBOIT or "spatially-hashed OIT" in production.

**M9 (offline LOD + transitions, meshlets/mesh shaders, visibility-buffer experiments) — Keep, anchor on meshoptimizer.** zeux/meshoptimizer's `clusterlod.h` (added explicitly for "hierarchical clustering similar to Nanite," Dec 2023) plus `meshopt_partitionClusters`/`meshopt_buildMeshletsSpatial` is a real, maintained, directly reusable library matching this milestone almost exactly — High feasibility, should anchor the implementation rather than a bespoke clusterer. One reshape: mesh shaders require M3+/A17 Pro on Apple Silicon, so M9 should document that its mesh-shader path is an M3+ feature with a compute-emulated meshlet fallback for older Apple Silicon, since AGENTS.md's hardware line ("macOS 26+, Apple Silicon") is currently unqualified.

**M10 (BLAS/TLAS + inline ray queries, progressive path-trace oracle, RT reflections + denoiser) — Keep, is correctly scoped and appropriately conservative.** Inline ray queries over a full RT pipeline is directly validated by Khronos guidance and matches shipped hybrid-rendering practice. Doom: The Dark Ages (2025) proves mandatory hardware-RT GI is now shipping-viable at the AAA scale; Luminex's much narrower "RT reflections + denoiser" scope is sensibly conservative by comparison. NRD (open-source, ReBLUR/ReLAX/SIGMA) is the best available open reference to study for "native denoiser," since DLSS Ray Reconstruction is Nvidia-proprietary. Hard constraint: M10 is unavoidably M3+-only — three Apple Silicon RT generations (M3→M4→M5) with compounding gains make this a reasonable, not premature, bet by the time Luminex reaches it.

**M11 (probe-volume GI floor then one dynamic cache; residency/streaming) — Keep, strongly validated.** DDGI-style world-space probe GI is independently confirmed as the most solo-feasible *and validatable* GI floor: no HW-RT/BVH dependency, deep prior art, and FLIP-comparable against a path-traced reference using Luminex's existing `TemporalCompare` tooling. Surfel GI is the most credible "one dynamic GI cache" candidate for the same reasons. Drop or deprioritize AMD Brixelizer GI as a candidate — AMD itself appears to have quietly removed it from the current FidelityFX SDK tree. Keep Radiance Cascades, ReSTIR GI/PT, and NRC as independent research only, not the M11 floor — their production track records (one largely-2D title; HW-RT/BVH prerequisites; no Metal-equivalent NN-inference path) don't yet support committing them. For residency, reshape toward Apple's native `MTLIOCommandQueue` fast-loading plus a hand-rolled tile-feedback buffer rather than DX12-style Sampler Feedback/full virtual-texturing parity — even Godot and Bevy don't have full VT, a useful negative signal that this should stay a smaller-footprint milestone.

**Independent research items** (Work Graphs, DGC, ReSTIR, frame generation, VSM, virtualized geometry) are all correctly kept out of the committed roadmap: Work Graphs/DGC are D3D12-only and largely redundant with what Metal's ICB already does; MegaLights/ReSTIR DI/GI require HW RT and aren't even Mac-targeted by Epic; frame generation is vendor-SDK-dominated with no Metal path. The one item worth promoting out of "independent research" into a near-term cheap win is Anti-Lag-2-style input-latency compensation — conceptually simple, High feasibility, no GPU dependency at all.

**Overall:** the plan is not dated. It correctly identifies the buildable, validatable, Metal-portable subset of a 2023–2026 AAA pipeline rather than chasing ML/RT-mandatory techniques inappropriate for a solo developer, and its ordering (raster GPU-driven pipeline → shadows/AO/transparency → geometry/LOD → RT → GI/streaming) matches the field's own hardware-gating reality: everything before M10 is buildable on the full Apple Silicon target, and M10 onward is unavoidably, correctly, an M3+-only tier.

---

## Sources (all URLs fetched or seen, observed 2026-09-14)

### Area 1
- https://www.sctheblog.com/blog/nanite-materials-notes/
- https://cs418.cs.illinois.edu/website/text/nanite.html
- https://trickybitsblog.github.io/2024/04/20/nanite.html
- https://www.thecandidstartup.org/2023/04/03/nanite-graphics-pipeline.html
- https://jcgt.org/published/0012/02/01/paper.pdf [UNVERIFIED — snippet only]
- https://www.simplygon.com/features/clusteredmeshletsoptimization [UNVERIFIED — snippet only]
- https://interplayoflight.wordpress.com/2025/05/05/meshlets-and-mesh-shaders/
- https://medium.com/@williscool/task-and-mesh-shaders-a-practical-guide-vulkan-and-slang-25baebe6388e [UNVERIFIED — snippet only]
- https://gpuopen.com/learn/mesh_shaders/mesh_shaders-meshlet_compression/
- https://arxiv.org/pdf/2404.06359 [UNVERIFIED — not fetched]
- https://themaister.net/blog/2024/01/17/modernizing-granites-mesh-rendering/ [UNVERIFIED — snippet only]
- https://metalbyexample.com/mesh-shaders/
- https://schedule.gdconf.com/session/scale-up-ray-tracing-in-games-with-rtx-mega-geometry-presented-by-nvidia/911194
- https://github.com/NVIDIA-RTX/RTXMG
- https://dl.acm.org/doi/10.1145/3721243.3735983 [UNVERIFIED — not fetched]
- https://github.com/Thefefe/orbit, https://github.com/zeux/niagara, https://github.com/nvpro-samples/gl_vk_meshlet_cadscene, https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/extensions/mesh_shader_culling [snippet-level only]
- https://github.com/GPUOpen-LibrariesAndSDKs/WorkGraphsMeshNodeSample [UNVERIFIED — snippet only]
- https://developer.apple.com/videos/play/wwdc2025/205/
- https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/, https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/, https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/
- https://github.com/microsoft/DirectX-Graphics-Samples
- Unity GPU Resident Drawer docs — 404, [UNVERIFIED]
- Godot Forward+ docs — 404, [UNVERIFIED]

### Area 2
- https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine
- https://en.wikipedia.org/wiki/Frostbite_(game_engine) [no clustered-shading detail found, UNVERIFIED for that claim]
- https://en.wikipedia.org/wiki/Doom_Eternal [no engine detail found, UNVERIFIED]
- Bitterli et al. SIGGRAPH 2020 ReSTIR DI, Olsson/Billeter/Assarsson HPG 2012, Harada/McKee/Yang Eurographics 2012 — [UNVERIFIED live, established prior knowledge]

### Area 3
- https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine
- https://en.wikipedia.org/wiki/Apple_M3
- https://en.wikipedia.org/wiki/Alan_Wake_2
- https://en.wikipedia.org/wiki/Shadow_mapping [partial PCSS confirmation only]
- https://developer.apple.com/videos/play/wwdc2025/205/
- https://developer.apple.com/documentation/metal/mtlgpufamily — empty body, [UNVERIFIED]

### Area 4
- https://github.com/GameTechDev/XeGTAO (archived 2024-04-22)
- https://bevy.org/news/bevy-0-13/
- https://github.com/bevyengine/bevy/pull/13454
- https://arxiv.org/abs/2301.11376
- https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Ray-Traced-Reflections.html
- https://en.wikipedia.org/wiki/Screen_space_reflection [general background only, UNVERIFIED for modern adoption specifics]
- https://interplayoflight.wordpress.com/
- https://dev.epicgames.com/documentation/en-us/unreal-engine/reflections-in-lumen-in-unreal-engine — empty TOC, discarded

### Area 5
- https://en.wikipedia.org/wiki/Order-independent_transparency [no 2020s content found]
- https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-clouds-in-unreal-engine — empty TOC, [UNVERIFIED]
- https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/physically-based-sky-volume-override-reference.html
- https://developer.nvidia.com/blog/rtx-best-practices/
- https://research.nvidia.com/labs/rtr/ (used to rule out "spatially-hashed OIT" claim — no matching 2023–2026 paper found)

### Area 6
- https://github.com/zeux/meshoptimizer and https://github.com/zeux/meshoptimizer/releases
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.4-release-notes, .../unreal-engine-5.5-release-notes — empty TOC, [UNVERIFIED]
- https://en.wikipedia.org/wiki/Alan_Wake_2
- https://en.wikipedia.org/wiki/Halo_Infinite [no mesh-shader mention found]
- https://en.wikipedia.org/wiki/Mesh_shader [hardware/API timeline only, no 2022–2026 title list beyond Alan Wake 2]
- http://jcgt.org/published/0002/02/04/ — empty body, [UNVERIFIED]
- https://developer.apple.com/documentation/metal/render-passes/reducing-shading-bandwidth-with-tile-based-deferred-rendering — 404
- https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering — title only, [UNVERIFIED]
- https://en.wikipedia.org/wiki/Deferred_shading [no VB/TBDR content found]
- https://www.ea.com/frostbite/news [no relevant 2023–2026 posts found]

### Area 6b
- https://www.khronos.org/blog/ray-tracing-in-vulkan
- https://en.wikipedia.org/wiki/Id_Tech_8
- https://en.wikipedia.org/wiki/Doom:_The_Dark_Ages [no direct RT-requirement text in article; circumstantial corroboration only]
- https://github.com/NVIDIAGameWorks/RayTracingDenoiser
- https://en.wikipedia.org/wiki/Deep_Learning_Super_Sampling
- https://en.wikipedia.org/wiki/PlayStation_5_Pro
- https://en.wikipedia.org/wiki/Apple_M3, https://en.wikipedia.org/wiki/Apple_M4, https://en.wikipedia.org/wiki/Apple_M5
- https://developer.apple.com/documentation/metal/accelerating-ray-tracing-using-metal — title only, [UNVERIFIED]
- https://developer.nvidia.com/blog/rtx-best-practices/

### Area 7
- https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_lightmap_gi.html, .../introduction_to_global_illumination.html [confirms Godot 4.x stable ships VoxelGI + SDFGI + LightmapGI only, no HDDAGI/Brixelizer GI in core]
- https://github.com/NVIDIAGameWorks/RTXGI, https://github.com/NVIDIA-RTX/RTXGI, https://github.com/NVIDIA-RTX/NRC
- https://mini.gmshaders.com/p/radiance-cascades
- https://github.com/Raikiri/RadianceCascadesPaper [author confirms paper never formally peer-reviewed]
- https://github.com/godotengine/godot-proposals/issues/10176 [closed/not pursued]
- https://gpuopen.com/learn/brixelizer-gi/, /learn/fidelityfx-brixelizer-gi/ — both 404
- GitHub release notes for GPUOpen-LibrariesAndSDKs/FidelityFX-SDK v1.1.0, v1.1.4, v2.0.0, v2.1.0
- Repo-tree listing Kits/FidelityFX/ confirming no `brixelizer` directory in current SDK
- https://developer.nvidia.com/rtx/ray-tracing/rtxdi
- https://en.wikipedia.org/wiki/Alan_Wake_2, https://en.wikipedia.org/wiki/Indiana_Jones_and_the_Great_Circle
- https://en.wikipedia.org/wiki/Star_Wars_Outlaws [no RT/GI technical detail, UNVERIFIED]
- https://en.wikipedia.org/wiki/Metro_Exodus [confirms RT/GI generally, no DDGI specifics, UNVERIFIED for DDGI detail]
- GitHub search results: W298/SurfelGI, AntonioNoack/Unity-SurfelGI, WatchDogStudios/O3DESurfelGI, mxcop/src-dgi, alexmalyutindev/unity-urp-radiance-cascades, simondevyoutube/Shaders_RadianceCascades, Yaazarai/GMShaders-Radiance-Cascades, Sohojoe/radiance-cascades-godot, Lommix/solis_2d
- Cyberpunk 2077 Overdrive's specific ReSTIR variant — nvidia.com press URL 404'd, [UNVERIFIED, prior knowledge only]

### Area 8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/streaming-virtual-texturing-in-unreal-engine
- https://github.com/godotengine/godot-proposals/issues/10176
- Repository pull-request and issue search for "virtual texturing" on bevyengine/bevy [negative result]
- https://github.com/GameTechDev/SamplerFeedbackStreaming
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/sampler-feedback — 404, [UNVERIFIED]
- https://github.com/microsoft/DirectStorage
- GitHub search "GDeflate" (community ports: ProjectKML/gdeflate-rs, neptuwunium/GDeflateNet, scavanger2221/gdeflate-native-linux, others)
- GitHub search "RTX IO" — no dedicated open repo found
- https://developer.apple.com/documentation/metal/mtliocommandqueue — empty body, [UNVERIFIED live]
- https://developer.apple.com/documentation/metal/resource-loading — empty body, [UNVERIFIED]
- https://developer.apple.com/metal/ [confirms Metal 4 device support A14+/M1+/Vision Pro; no IO-specific content surfaced]

### Area 9
- https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine [explicitly confirms TSR does NOT do frame generation/extrapolation]
- https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-frame-interpolation-in-unreal-engine — empty content, [UNVERIFIED]
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.4-release-notes — empty content, [UNVERIFIED]
- https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK repo tree (`framegeneration` dir confirmed)
- https://github.com/intel/xess README
- https://www.nvidia.com/en-us/geforce/technologies/reflex/
- https://github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK README
- https://developer.nvidia.com/blog/nvidia-reflex-2-now-available/ — 404, [UNVERIFIED]
- https://en.wikipedia.org/wiki/Temporal_anti-aliasing
- https://bartwronski.com/2022/08/12/... — 404, [UNVERIFIED]
- Eurogamer/Digital Foundry TAA articles — fetch blocked (host restriction), [UNVERIFIED]
- Bing "Threat Interactive TAA Unreal Engine" news search — no results, [UNVERIFIED]

**Methodology caveat:** Several Apple Developer Documentation pages are JS-rendered SPAs that returned only page titles, not body content — flagged `[UNVERIFIED]` throughout rather than asserted. A handful of well-established citations (foundational SIGGRAPH/HPG papers predating 2023) could not be re-confirmed live and are carried from prior knowledge, marked accordingly.

