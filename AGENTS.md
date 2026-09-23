# Luminex
Metal 4-first rendering playground / portfolio (macOS 26+, Apple Silicon), with a thin RHI and
one backend. Direction: graph-scheduled GPU-driven hybrid rendering with shared scene/material/
light/temporal semantics, visible quality and reproducible evidence. Future scope and prerequisites
live only in `docs/roadmap.md` and its linked parts under `docs/roadmap/`.
## Golden sources
- Founding design: `docs/decisions/0000-founding-design.md` (D1–D10 binding). Architecture/frame: `docs/architecture/overview.md` · `docs/frame-pipeline.md`; guides: `docs/guides/gpu-debugging.md` · `docs/guides/temporal-comparison.md` · `docs/guides/screenshot-comparison.md`
- ADRs: `docs/decisions/` · Conventions: `docs/conventions/` · Roadmap: `docs/roadmap.md` · Evidence storage/recovery: `docs/guides/evidence-archive.md`
- Design and planning write to two places: a brainstormed design is the `Proposed` milestone record
  in its series folder under `docs/milestones/`; an executor plan goes to `docs/plans/`.
- Roadmap parts: `docs/roadmap/rendering-foundations.md` (M4–M6.5 and gate B), `docs/roadmap/gpu-driven-hybrid-rendering.md` (M7–M11 and independent research),
  `docs/roadmap/codebase-module-boundaries.md` (contract and R1) with `codebase-restructuring.md` (R2 RHI→RojoRHI submodule, R3 Donut-style Core/Engine/Render/App subsystems and tree restructure, seven slices: R3.4 Core math/containers with Engine decomposition and a `Source/Scenes` catalog unit under accepted ADR 0026 (implemented record `docs/milestones/r/r3.4.md`, validation `docs/milestones/r/r3.4-validation.md`), then R3.5 Render, R3.6 App, R3.7 Tests, R4 shader source deduplication), `docs/roadmap/editor-experience.md` (UX1 before M7.1; UX2 scene documents/hierarchy before N1), and
  `docs/roadmap/neural-rendering.md` (N1–N4 learned-rendering studies; accepted post-M7 order N1 → M9 → M8 → M10 → M11; hardware floor; ADR 0022 proposed MSL tensor-module exception).
- Gate B passes after R1 (`docs/milestones/m6/interface-gate-b.md`); ADR 0021 owns the approved
  scene-identity/update handoff contract. UX1 is implemented and owner-accepted for integration
  after manual review; `docs/milestones/ux/ux1.md` retains evidence limits. Its executor plan is closed.
  M7.1 is implemented and owner-accepted after manual verification; `docs/milestones/m7/m7.1.md` retains passing Xcode replay and 11/15 original versus 15/15 accepted scoped vendor-profile comparisons. Its plan is closed; the owner approved main integration on 2026-09-15 and M7.2 is owner-accepted for integration on 2026-09-15 after manual review; its plan is closed, original/revised image gates remain failed (13/15 and 9/15), and no new tolerance or performance adoption follows (`docs/milestones/m7/m7.2-validation.md#owner-acceptance-and-integration`). M7.3 is implemented and owner-accepted for integration on 2026-09-16; `docs/milestones/m7/m7.3-validation.md` retains both 14/15 failed GPU/CPU exact-image gates and the incomplete ICB capture gate. Its plan is closed; defaults are unchanged. M7.4 is implemented and owner-accepted for integration on 2026-09-18; `docs/milestones/m7/m7.4-validation.md` retains its 13/15 failed exact-image gate and mixed costs. Occlusion stays off; its executor plan is closed. M7.5 is implemented and owner-accepted for integration on 2026-09-19 after visual review; its executor plan is closed. Clustered remains default by the passed list/scoped-image gates. [Acceptance](docs/milestones/m7/m7.5-validation.md#owner-acceptance-and-integration) retains original 11/15 failed zero-light mode invariance and frozen costs; the [follow-up](docs/milestones/m7/m7.5-followup.md) records authored-light controls, the camera tour, flicker corrections, passing exact causal/current-camera 15/15 gates and unresolved historical-camera replay.
- Roadmap entry: M6 has five temporal/display slices; M7 ends after five scene/visibility/lighting slices; M8 has five shadow/indirect/transparency/atmosphere slices; N1 has four inference-lab slices; `docs/roadmap.md#execution-sequence` owns the cross-part order: accepted post-M7 order R2 → R3 → R4 → UX2 → N1; R2.1 (RojoRHI conventions/ADRs in `rojo-rhi`, accepted ADR 0024) is owner-accepted 2026-09-19 with its plan closed, R2.2 (in-place decoupling, `RHITests`, standalone `RHI/` root) is owner-accepted 2026-09-19 with its plan closed, R2.3 (mechanical rename to `rojoRHI`, `RojoRHI/`, `RojoRHITests`) is owner-accepted 2026-09-20 with its plan closed, R2.4 (extract, mount and wire; `RojoRHI/` is now a git submodule of the public, Apache-2.0 `rojo-rhi` repository) is implemented 2026-09-20 with its pull request pending owner review, R3.1 (documentation records: milestone series folders, each retained design beside its record, closed plans deleted and reachable from tag `r3.1-closed-plans`) is owner-accepted 2026-09-20 with its plan closed, R3.2 (shader folders: ten `Shaders/Passes/<family>/` folders and `Shaders/Common/` replacing `Modules/`, enforced by the build rule and the import checker) is implemented 2026-09-20 with its plan closed and awaiting owner review, R3.3 (the Engine subsystem: `Source/Asset` and `Source/Scene` merge into `Source/Engine/`, Render's scene vocabulary moves to `Engine/Types/` (`Bounds.h` to `Core/`), `render` depends on `engine` and not the reverse; ADR 0025) is implemented 2026-09-21 with its plan closed and its pull request set to merge on green CI (`docs/milestones/r/r3.3-validation.md`), R3.4 (Core's math and data structures move down with their own tests, `Engine/Types/` splits into `View/`, `Lights/`, `Geometry/`, `Material/` and `Scene/`, and the catalog leaves Engine for its own `scenes` unit at `Source/Scenes`; ADR 0026) is implemented 2026-09-21 with its plan closed and its pull request set to merge on green CI (`docs/milestones/r/r3.4-validation.md`), R3.5 (Render folders, Core adoptions, decompositions, stage shape and shared helpers) is implemented and owner-accepted for integration on 2026-09-22, with its plan closed; the scoped part A parity exception, passing part B gates and measured Scene-only adoption remain in `docs/milestones/r/r3.5-validation.md`; R3.6 A is implemented and owner-accepted 2026-09-23 with one scoped parity exception; the 14/15 failure remains in `docs/milestones/r/r3.6-validation.md`; B is implemented with its own passing gates and a retained double selection fit; its executor plan is closed; R3.7 is in progress under `docs/plans/2026-09-23-r3.7-tests.md`; `Proposed` records `docs/milestones/r/r2.md`, `docs/milestones/r/r3.md`, `docs/milestones/r/r4.md`, `docs/milestones/ux/ux2.md` (M8/M9 keep their identifiers though M9 delivers first); transparency belongs to M8, cluster LOD
  to M9, area lights to a separate extension, learned passes to Part V. Planned boundaries, not capabilities; no neural, clustered-geometry or ray-traced path exists; clustered local lighting is implemented.
- Current baseline: `docs/milestones/m6/m6.5.md` (explicit SDR/UI/capture domains, tagged PNG,
  EDR DEFER and an accepted historical-hash exception; ADR 0019) over
  `docs/milestones/m6/m6.4.md` (opt-in vendor reconstruction, masked San Miguel and
  offline comparison; ADRs 0017–0018, evidence limits and follow-up QA recorded there) over
  `docs/milestones/m6/m6.3.md` (temporal upscaling and dynamic resolution, ADR 0016) over
  `docs/milestones/m6/m6.2.md` (native TAA and exposure stability, ADRs 0014–0015) over
  `docs/milestones/m6/m6.1.md` (temporal state/motion, ADR 0013) over `docs/milestones/m5/m5.5.md`
  (graph legibility/detached window) over `docs/milestones/m5/m5.4.md` (graph nodes, ADR 0011) over
  `docs/milestones/m5/m5.3.md` (editor workspace and selection) over `docs/milestones/m5/m5.2.md`
  (frame-data path, ADR 0010) over `docs/milestones/m5/m5.1.md` over `docs/milestones/m5/m5.md`
- M5.6 closed as reliability failure / DEFER, with no accepted performance conclusion (ADR 0012).
  Evidence/source are frozen at `m5.6-gpu-submission-evidence`; see `docs/milestones/m5/m5.6.md`.
  Verified raw bundles are GitHub Release attachments; local originals were deleted. Restore:
  `docs/guides/gpu-submission-archive.md`. Baseline remained M5.5; M6 was unblocked.
## Commands
- Clone with `git clone --recursive` so `RojoRHI/` (a git submodule of the public `rojo-rhi` repository) populates; a plain clone or a new worktree instead needs `git submodule update --init` before configuring.
- Setup (once): `brew install xmake`, `xmake setup`, which fetches pinned ThirdParty deps (metal-cpp,
  slang, Dear ImGui docking-branch commit, imgui-node-editor, Inter 4.1 font/license), Damaged Helmet, the CC0 Studio
  Small 09 HDRI, and the official ~78 MB Crytek Sponza OBJ+PNG archive into gitignored
  `Assets/Fetched/`, with upstream provenance/license metadata.
  Setup deterministically converts Sponza to uncompressed core glTF, then bakes every base-color
  and normal image referenced by Sponza and Damaged Helmet into a deterministic offline mip chain
  (`Tools/TextureBake`, DDS + manifest) that scene loading prefers over its in-process fallback;
  pins and hashes are in `xmake/setup.lua`. Setup re-applies the maintained ThirdParty patches
  idempotently: a tree already carrying the current patch is left alone, and one carrying an older
  revision of it is restored to the pin before the patch is applied. Optional:
  `xcodebuild -downloadComponent MetalToolchain` enables offline shader precompile (runtime-MSL fallback works without it).
- Optional courtyard: `xmake setup --san-miguel` downloads the official ~511 MiB archive, converts
  its realtime OBJ at authored metre scale with diffuse alpha cutouts and `N_` tangent normals,
  and bakes referenced textures. `Assets/Fetched/SanMiguel/` preserves source metadata, license and
  conversion provenance; archive and converted-tree hashes are pinned in `xmake/setup.lua`.
- Editor setup (once, for clangd): `xmake project -k compile_commands` writes
  `compile_commands.json` (gitignored); without it clangd reports spurious diagnostics.
- Build: `xmake` · Run: `xmake run App` · Tests: `xmake test` runs four groups (`Tests/unit`, `Tests/gpu`, `RojoRHITests/unit`, `RojoRHITests/gpu`; CPU-only: `xmake test Tests/unit RojoRHITests/unit`). Standalone RojoRHI: `xmake setup -P RojoRHI` (or copy/symlink `ThirdParty` into `RojoRHI/`), then `xmake f -P RojoRHI` and `xmake build -P RojoRHI RojoRHITests`; a bare copy of `RojoRHI/` builds and tests the same way from its own root.
- **Gotcha**: Tests has `set_default(false)`: plain `xmake` does not relink tests after source edits. GPU regression uses `[gpu]~[.]`; hidden replay diagnostics require their explicit filter.
  `xmake test` rebuilds; before running Tests directly, run `xmake build Tests` to avoid stale passes.
- Frozen portability-checkpoint-A subset (ADR 0009), split over both binaries (16 RHI, 3 graph):
  `xmake build Tests && xmake build RojoRHITests && (cd build/macosx/arm64/release/rojorhi-test && MTL_DEBUG_LAYER=1
  ./RojoRHITests "[checkpoint-a]") && (cd build/macosx/arm64/release/test && MTL_DEBUG_LAYER=1 ./Tests
  "[checkpoint-a]")` — a future backend must pass this filter unchanged; each working directory must be that binary's build directory (shaders resolve relative to CWD). `python3 Tools/check_checkpoint_a.py` checks the split against the frozen inventory.
- Format: `xmake format` (check: `xmake format --check`) · Policy: `xmake policy` (also runs
  `check_module_deps.py`/`check_source_headers.py`; `check_module_deps.py --link` needs a build, so
  CI runs it after Build). `RojoRHI/` is a git submodule; the root `policy` task runs `Tools/check_submodule_pin.py` (fails unless the pinned commit is reachable from `rojo-rhi`'s `origin/main`) but no longer walks the component's own checkers, which run in `rojo-rhi`'s own CI. Two-repository rule: a Luminex commit never edits a file under `RojoRHI/`; an RHI change is a `rojo-rhi` commit that reaches Luminex as a pin bump.
- **Gotcha**: `xmake policy` run from inside a nested git worktree silently validates the *outer* checkout, not the worktree: xmake resolves its project root to the outermost ancestor directory holding an `xmake.lua`. A worktree also needs `git submodule update --init` once after creation, before configuring, or `xmake f` fails at the missing `RojoRHI/` checkout.
  In a worktree, run the checkers directly from its root instead: `python3 Tools/check_project_policy.py`; `python3 Tools/check_module_deps.py`; `python3 Tools/check_source_headers.py`; `python3 Tools/check_cpp_comments.py --public-api-docs error` (first regenerate that worktree's `compile_commands.json` with `xmake project -k compile_commands -P .`, a prerequisite the comment checker reads, not a checker itself); `python3 Tools/check_cpp_layout.py`; `python3 Tools/check_submodule_pin.py`.
  `RojoRHI/`'s own checkers (`check_project_policy.py`, `check_shader_imports.py`, `check_cpp_comments.py`, `check_rhi_headers.py`, `check_cpp_layout.py`), format/policy tasks and Python suite (`python3 -m unittest discover -s RojoRHI/Tools/tests -t RojoRHI`) run from inside the mount with `-P .`, e.g. `python3 RojoRHI/Tools/check_project_policy.py`; they are no longer part of Luminex's root `policy` task, and run in `rojo-rhi`'s own CI.
- Frame-data benchmark: `xmake build FrameDataBench` then `python3
  Tools/Bench/frame_data_paired.py` for paired CPU-encoding measurements against a frozen baseline
  build; both the bench binary and the driver support `--selftest`.
- Scenes: `xmake run App` opens the editor maximized to the display's usable bounds, with Sponza
  selected by default (File > Open Scene catalog); `--windowed` keeps the fixed 1280×720
  default size instead. Offscreen: `xmake run App --screenshot <out.bmp>` or `--scene
  <sponza|damaged-helmet|milk-truck|material-lab|temporal-lab|san-miguel|visibility-lab|light-lab> --screenshot <out.bmp>`. `--frames N`
  (default 1) renders N frames before writing the last (the temporal warmup control), advancing
  the scene's animation by 1/60 s and following its camera track (if any) between them.
  `.png` uses tagged PNG with display/frame metadata; `.bmp` preserves the historical bytes.
  In both windowed and screenshot runs, `--temporal <off|raw|taa|metalfx>` (default `taa`; a bare
  `--temporal` also means `taa`) selects the reconstruction, and
  `--temporal-view off|motion|reprojection|reprojected|rejection|weight|age`
  selects a diagnostic overlay (naming a view still implies temporal on; `--temporal off` with a
  non-`off` view is an error). Example: `xmake run App --scene temporal-lab --frames 32 --temporal-view
  rejection --screenshot out.bmp`. `--render-scale <0.5..1.0>` (default 1.0) sets the render scale
  the temporal path reconstructs from (conflicts with `--temporal off` below 1.0, since the temporal
  path is what reconstructs a sub-output render). `metalfx` requests the device temporal scaler,
  with Native TAA fallback when unsupported or creation fails; `rejection`, `weight`, and `age`
  conflict with `--temporal metalfx`. Vendor scale is clamped to the supported range, and Native TAA
  stays the default and reference. Running the binary directly requires CWD = its
  build dir (shaders resolve relative to CWD). Sponza's first load decodes its referenced textures;
  expect several seconds in a debug build.
- Visibility: `--classify cpu|gpu` (CPU default), GPU-only `--classify-check`, `--visibility cull|off`, `--submission direct|indirect|batched` (cull/indirect defaults; GPU forbids direct). Lab-only `--lab-instances N` defaults 4096. `--occlusion on` adds previous-frame HZB under GPU/cull; `--occlusion-check` uses an independent ID oracle;
  `--hzb-level K` visualizes a mip; `--lab-occluders N` is lab-only. `--measure out.json --warmup W --frames N` records schema 4 costs; `--unscored` permits instrumentation/check mode. Paired controls and retired diagnostics: `docs/guides/gpu-visibility.md`.
- Local lights: `--local-lights off|direct|clustered` (Clustered default); unshadowed point/spot lights affect opaque/masked surfaces. LightLab `--lab-lights N` defaults 256, `--lab-light-pile P` defaults 0, N≥1 and N+P≤4096. Sponza authors 16 static lights; its `--local-light-rig on|off` defaults on, with explicit off disabling all rig lights while retaining IDs/rows. Its camera rail tours both corridor levels in 120 seconds through the atrium. `--light-check` and non-off `--light-view off|count|overflow|missed` require Clustered (non-off views conflict with temporal/HZB views); diagnostic measurement requires `--unscored`. `LMX_LIGHT_CHECK_DUMP` writes raw frame-keyed CPU/GPU list evidence for checked screenshot/sequence runs to a new path. Schema 4 joins retired lighting to every frame, including Off/Direct/zero-live; `lightingGpuMs` sums `lmx.pass.light.*` separately from scene cost. `Tools/Bench/lighting_paired.py --control local` compares Direct/Clustered; `--control zero --parent /frozen/parent/App` compares parent schema 3 with candidate schema 4. `Tools/Lighting/missed_oracle.py` audits PNG pixels/manifests. Both tools support `--selftest`. Procedures: `docs/guides/gpu-debugging.md#inspect-local-lighting`. [Default decision](docs/milestones/m7/m7.5-validation.md#default-decision): family 2 lossless lists and scoped family 3 exact images passed; Direct remains the reference.
- Sequences: `--capture-sequence <directory> --frames N --warmup W` saves N numbered PNGs (or `--capture-format bmp`) after W
  unsaved frames at 60 Hz, plus a v2 camera/settings/status/display/container/UI manifest, into a new or empty directory.
  It conflicts with `--screenshot`; vendor fallback fails the sequence. `Tools/TemporalCompare/`
  builds synchronized Raw/Native/MetalFX reports and optional CPU LDR-FLIP maps of final sRGB output;
  these measure differences against Native TAA, not ground-truth accuracy or realtime performance.
- Debug: Metal validation `MTL_DEBUG_LAYER=1 xmake run App`; GPU capture: press `c` in-app, or use
  Debug > Capture Next GPU Frame in the main menu (shown with its `C` shortcut); both need
  `MTL_CAPTURE_ENABLED=1`. Then open the .gputrace in Xcode. Automated runs: `LMX_MAX_FRAMES=N`
  exits after N frames; `LMX_CAPTURE_AT_FRAME=N` captures without a keypress;
  `LMX_DYNAMIC_RESOLUTION_BUDGET_MS=<ms>` starts the editor with dynamic resolution on and that GPU
  budget before the frame loop begins, and logs every controller scale change at INFO, the
  automation hook for controller-settling evidence. Performance publishes a coherent 60-retired-
  frame rolling snapshot at 4 Hz, with sortable pass costs, frame interval/FPS, extents and memory.
  `Freeze metrics` freezes the whole snapshot; frozen `Clear history` empties it; `Resume metrics`
  waits for new samples. Timed-pass sum excludes presentation, driver and untimed GPU work.
  Render Graph owns a detached native window and publishes one owned frame at 4 Hz, including
  exact matched timings (never averages). First data/Resume publish immediately; later topology
  changes wait for publication. Freeze latches the displayed frame through scene switches.
  Physical temporal-resource alternation preserves unchanged canvas identity/navigation while
  details retain exact physical resources. Dump exports the displayed frame, including frozen.
  Stage groups collapse by default; `columns` 0 means no wrap. Fit graph/selection, 100%, Reset
  layout and output Copy path/Reveal are explicit actions. See `docs/guides/gpu-debugging.md`.
- GitHub-hosted macOS exposes a paravirtual GPU without Metal 4. Hosted CI compiles and inventories
  GPU cases; docs-only changes (`docs/`, README, AGENTS/CLAUDE.md, LICENSE) run the policy job alone; renderer/RHI/shader PRs still require `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on
  Metal 4 Apple Silicon before merge.
- Editor uses bundled Inter Regular at 16 pt with stable-width digits; App stages Fonts from setup. UI zoom: top-bar minus/percentage/plus or Layout > UI Scale, 75–150%, persisted; Cmd+-/Cmd++/Cmd+0 outside editing. Controls: RMB look, WASD move, Q/E down/up; release to edit.
  `Camera help` explains controls; text entry suppresses camera/capture keys. Top Scene/Measure toolbar owns a state-switching Play/Pause button, separate Stop/Step and camera-rail follow options; scenes load Stopped. First Scene Play captures camera/time and animation-owned object poses/emissive strength and tracked light positions; Step advances 1/60 s and pauses.
  Stop/scene switch restores the captured preview and resets motion/temporal/exposure; rendering settings and unrelated edits are outside restoration. Measure Play starts fixed W/N; Pause is disabled, Stop cancels, and completion/Stop restores preview state. Performance retains plan/results/export in a detached native window; Measure Play opens/focuses it once, while mode selection and completion/cancellation do not. Closing it leaves the run active; toolbar options > Show measurement opens/focuses Measure anytime. CLI is unchanged.
  Playback, metric freeze and graph freeze are independent. Reset camera restores its authored pose/lens and stops follow; static scenes allow camera preview; unavailable camera-rail options explain their disabled state.
  Hierarchy has compact search, collapsible subjects and keyboard navigation; frustum-rejected names are dimmed but remain selectable, with reasons on hover/in Inspector. File > Open Scene
  owns catalog loading/retry. Source names disambiguate per scene; filters retain selection. Frame
  selected fits reliable bounds. A toggleable editor-only outline follows visible selected geometry.
  Rendering expands in Hierarchy into Reconstruction, Resolution, Visibility, Occlusion, Submission, Lighting, Exposure, Bloom, Shadows, Display and Scene tables. Each Inspector topic keeps controls and compact live readings together without nested detail toggles. Visibility/Occlusion/Submission/Lighting readings publish coherent frames every 250 ms; fallback and failure warnings remain immediate.
  Local lights use clipped Hierarchy rows, full LightId selection and per-light enable checkboxes that preserve IDs/edits/orbits; Inspector edits position/colour/intensity/range and spot direction/cones, with authored/current-orbit reset. Lighting owns mode/check/view and bounded LightLab pile Apply/Clear; scoped Reset leaves individual lights and other Rendering topics unchanged. Measure disables editor edits and freezes the initial enabled population. Fields reflow, vectors label XYZ/RGB, scoped Reset shows changes, and delayed tips explain
  nonobvious controls; defaults/recovery are documented in `docs/guides/gpu-debugging.md`.
  File/Window/Layout/Debug expose quit, visibility, Reset Default Layout and capture. Workspace schema 3, docking and viewport state persist in build-local `imgui.ini`.
  Console alone occupies the bottom dock; Performance and Render Graph are detached native windows, closed by default. Window > Performance toggles it normally.
  Schema 2 migrates to default topology while preserving valid UiScalePercent; schema 3 restores visibility and window bounds. Missing scale defaults to 100%.
  Reset Default Layout preserves UI scale, closes Performance/Graph and resets Performance's next-open bounds; both detached windows otherwise remember geometry.
  Menu, C and viewport capture share capability/pending/result state; disabled startup explains
  `MTL_CAPTURE_ENABLED=1` and relaunch. Failures retain reasons; successes expose Copy path/Reveal.
  Pending capture waits for a drawable. Scene-only captures contain no transport/selection cues.
  Console retains 2,000 entries/2 MiB, truncates messages at 16 KiB, and shows UTC/severity/loss
  counts. Search/minimum severity filter its display; Freeze keeps ingestion live, Clear empties
  history/counters, and Copy visible exports matching displayed messages. It never executes commands.
- GPU debug: `MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=N LMX_MAX_FRAMES=N+10
  LMX_CAPTURE_PATH=/tmp/out.gputrace xmake run App` then `python3
  Tools/GpuDebug/gputrace_dump.py /tmp/out.gputrace`; timings: `python3 Tools/GpuDebug/profile.py`.
  `LMX_GRAPH_DUMP=/tmp/out.txt xmake run App` writes the first compiled frame once; paths must be
  absolute. Interactive Dump exports the displayed frame. Procedures, recovery and exposure/bloom
  parity checks: `docs/guides/gpu-debugging.md`.
## Architecture
`Source/Core` (lmx:: `Diagnostics/` log/assert; `IO/` file/JSON; `Math/` alignment, dispatch division, colour transfer, finite AABBs with their corner transform (`Aabb.h`), spheres, frusta, reversed-infinite-Z/orthographic-fit projections, low-discrepancy sequences, IBL sampling measures and TRS transforms; `Containers/` a generational handle with its slot allocator, a dirty set, an interval and a ring buffer; `Util/` numeric parsing, SHA-256, a stopwatch and ASCII lowercasing; public spdlog/glm) and, independently, the root `RojoRHI/` component, with no Core dependency, its own private `RojoRHI/Source/Base` (assert/log/align/JSON) and one public `RojoRHI/Include/rojoRHI/Message.h` callback (severity, text; unset writes stderr) that `Render/Common/RhiLog` forwards into spdlog/Console for App and the Luminex `Tests` binary. Standalone `RojoRHI/xmake.lua` plus `RojoRHI/xmake/targets.lua`, `setup.lua` and `shaders.lua` configure/build/test it alone (`xmake -P RojoRHI`); the repository root includes `RojoRHI/xmake/targets.lua` and nothing else from the component. `RojoRHI/Tests`/`RojoRHI/Shaders/Tests` hold its own contract/GPU suite (`RojoRHITests`, linking only `RojoRHI`); `RojoRHI/Tools` holds its header check, ImGui patch and buffer probe (`RojoRHI/Include/rojoRHI`: public `rojoRHI`
interfaces with **no Metal or ImGui types**; `RojoRHI/Source`: shared implementation;
`RojoRHI/Backends/Metal4/Source`: the only backend, with metal-cpp, 3 frames in flight, argument tables (16 buffer / 16 texture / 8 sampler slots; texture slots cleared at each render/compute pass)
+ a per-frame-slot growable frame-data page arena with a checked recycle invariant, residency set,
shared-event pacing, per-pass GPU timing for every pass kind, samplers, sRGB/BC1/cubemap/RGBA16Float
formats, depth-only passes, compute passes with storage bindings, subresource views, explicit
texture and buffer barriers, copy passes with general copies and fills (the path to any subresource
but level zero), indirect draws and dispatches over RHI-owned argument layouts, untracked
placement heaps whose resources are created at explicit offsets, and up to `kMaxExtraColorTargets`
(3) additional colour attachments per render pass/pipeline beyond the primary, with `RG16Float` and
`R8Unorm` colour-renderable and CPU-readable, and an origin-anchored render area confining a pass to
a sub-rectangle of its attachments; `Device::capabilities()` reports the neutral temporal-scaler
capability, `TemporalScaler` owns vendor history, and the timed `CommandList::temporalScale` encodes
between passes with `ExternalRead`/`ExternalWrite` barriers. MetalFX uses a fence handoff and a private
output copied to CPU-readable outputs; `R16Float` supports sampled/storage exposure texels; `R32Float` supports sampled/storage/CPU-readable HZB texels; `BufferDesc::cpuWrite` enables checked nonempty `Buffer::write(offset, data, size)` host uploads only after all GPU use of the range retires (paced slot or waitIdle); placed private buffers reject it;
`RojoRHIMetal4ImGui`: optional ImGui glue target; maintained backend patch quarantines each slot's used vertex/index buffers until its next paced visit, preventing native-window uploads from overwriting main-window GPU reads) → `Source/Engine/Asset` (lmx::asset: CPU DDS/glTF/Radiance HDR/PNG/BMP loaders and writers,
GeometryGenerator, deterministic environment/IBL generation, texture baking,
repository asset discovery, clip data and sampling; depends on Core and
RHI format/descriptor headers only) + `Source/Engine` outside `Asset` (lmx::engine: `View/Camera`,
`Geometry/` CPU `MeshData`/`Vertex`, `Lights/LocalLightMath`, `Material/AlphaMode.h`,
`Lights/LocalLight.h`, `Scene/DrawItem.h`, `Lights/DirectionalLight.h`, `Scene/MotionClass.h`,
`Scene/SceneTables.h` shared row ABI, `Scene`, GPU
DDS/cubemap/IBL uploads, environment rig, source names, shared mesh bounds/world-row updates, initial camera,
playback and previous transforms; generational `InstanceId`/`MeshId`/`MaterialId`/`TextureId`/`LightId` reject stale/foreign handles; add/finalize builds one immutable rebased vertex/index pool including sky. Stable row slots survive removal/reorder; three paced table buffers per kind update only dirty rows. Growth doubles capacity and retires old buffers at lastFrame+3; new instances seed their previous pose;
localLights() includes disabled identities, enabledLightCount()/render liveLightCount count enabled lights, and only pre-finalize authored lights receive orbit indices; depends on Core, Asset and the public RojoRHI headers) → `Source/Render` (lmx::render: `Graph/`, `Renderer/`, `Common/` and ten shader-matched `Passes/<family>/` folders; depending on Engine's `Camera`/`SceneTables.h` vocabulary: the
validating `RenderGraph` — raster/compute/copy/external passes with per-subresource uses (including
extra colour attachments) over imported resources and over one-frame transients the graph creates,
dead-pass culling from declared sinks only, conservative aliasing of lifetime-disjoint transients
into `TransientPool`'s per-frame-slot placement heaps, and a `CompiledFrameRecord` per frame
(schedule, barriers, transient lifetimes and assignments, memory totals) that `GraphDump.h` renders
as deterministic text; `CompiledFrameRecord.h` owns the observer contract; graph compile/transitions/validation/ranges are separate units; `FrameDeclaration` shares graph execution;
`SceneView.h` describes mesh ranges/textures and borrows CPU rows plus five geometry/material GPU buffers and live-only lights, produced from a scene by `Render/Renderer/SceneViewBuilder.h`'s `buildSceneView`; 240 B instances/48 B meshes carry world/local AABBs. CPU-default five-plane visibility or opt-in `GpuVisibility` reset/classify/scan/emit feeds paced b4 row lists and indirect args; 16 B firstEntry selects b5/b6 rows. Direct/indirect/batched modes retain per-run textures. Opt-in `HzbStage`/`Occlusion` consume previous source-space depth with global coverage invalidation; `OcclusionReference` checks direct IDs and recovery at retirement. `Renderer` imports five read-only geometry/material `lmx.scene.*` buffers, live-only `lmx.scene.lights`, plus `lmx.draw.rows`/`lmx.draw.args` and composes `ShadowStage`/`SceneStage` and private `ExposureStage`/`BloomStage`/`DisplayStage`; these own pipelines/resources and declare histogram exposure
(clear/accumulate/resolve with bounded adaptation, GPU-resident `{applied, previous}` feedback into
the next frame), bloom (threshold/downsample/bilinear upsample), display-transform, and, opt-in via
`SceneView::temporal.enabled` (on by default since M6.2), motion/reactive/reconstruction passes
(reproject diagnostic, the `TemporalResolve` stage's native TAA/TAAU, raw history commit, or vendor
packing plus external reconstruction, debug view) into a graph consuming a plain `SceneView`;
`AlphaMode::Mask` uses dedicated scene/auto-exposure/shadow pipelines sharing texture-alpha × factor
alpha cutoff coverage, with optional two-sided shading; color/depth/motion/reactive are discarded
together. Cutoff/flags come from shared material rows; AlphaMask is a pure function and its old per-draw block is retired. Pipeline variants stay separate; glTF BLEND is unsupported (ADR 0018).
`fitShadowOrtho` and friends are free functions;
`Temporal.h`/`TemporalHistory.h` hold the motion convention, jitter sequence, active render extent
and reset-reason derivation (ADR 0013, narrowed by ADR 0016); `TemporalResolve` has native/upscale/vendor/diagnostic declaration units sharing the
reconstruction contract, ping-pong slot ownership, the native/upscale kernel-selection rule and
frozen constants (ADRs 0014–0016); its composed `VendorTemporalScaler` translates invalid motion to
zero motion/reactive one, writes reciprocal applied exposure into one `R16Float` texel, and maps
motion/jitter/content extents. The scaler resets on engine reset, vendor re-entry or recreation;
engine history stays valid across native/vendor switches. `lmx.pass.temporal.vendor.pack` feeds
`lmx.pass.temporal.vendor`, with native fallback/status and extent-scoped creation retry (ADR 0017).
`ResolutionController` is a pure, App-driven policy that proposes a render scale from a retired frame's summed GPU pass time (ADR 0016)) →
`Source/Scenes` (lmx::scenes: the catalog moved out of Engine — `SceneLibrary`, VisibilityLab, LightLab and optional San Miguel with deterministic 12-second rails, plus Sponza's 16 static lights and 120-second two-level tour; depends on Core, Asset, Engine and the public RojoRHI headers; linked by AppModel, App and Tests) →
`Source/App/Model` (AppModel static library linked by App and Tests; `Options/`, `Scene/`, `Graph/`, `Performance/`, `Console/`, `Capture/`, `Workspace/` and `Rendering/{Settings,Temporal,Lighting,Visibility}/`;
pure editor/capture models, shared SceneSession and record observers; Tests compiles its own C++ only; no SDL/ImGui/Metal/
RenderGraph dependency. `SceneSession` retains per-scene authored transform/light defaults on
first activation and performs targeted current-time edits/resets; editor/capture call `prepareFrame` after `beginFrame` before declaration. `SceneTableDisplay` formats the Scene tables topic counts/capacities, writes, slot, growth and retirement. `EditorRenderDefaults` defines
independent rendering reset scopes; `SelectionBounds` uses Core's shared AABB transform (`Math/Aabb.h`) on mesh bounds
for framing. `TemporalEditorState` owns scene generation, camera cuts, persistent reset events
paired with declared-frame counts, and compatible live retired timing. Renderer's
per-frame reset field retains its original meaning. `DynamicResolutionState::lastObservedFrame`
is the consumed/skipped publication cursor; `lastMeasurementFrame` pairs with the last controller
measurement. `MeasurementRun` shares exact frame/GPU joins with serialized-retirement headless and unscored interactive runs. `FrameRecordRing` retains declaration-time counts/extents/context with compiled
records; `GraphSnapshot` owns live/frozen 4 Hz copies. `ConsoleLog`/`ConsoleModel` own bounded
thread-safe logging and filtered/frozen display) →
`Source/App` (`Shell/` owns main and EditorShell partials; `Headless/` owns Screenshot/Measurement/OcclusionValidation; `Panels/` groups Scene/Inspector/Viewport/Graph/Performance/Console/Shared;
`EditorWorkspace` owns settings callbacks, default docking and UI-scale controls; `EditorInput` owns camera input. Inspector subject units share private `InspectorInternal.h`; dispatch/row helpers stay in `InspectorPanel`.
SDL3 six-panel editor: Hierarchy/Viewport/Inspector dock with Console alone below; Performance and Render Graph own detached native windows. `EditorStyle.h` shares responsive fields/tips; Inspector separates
requested/effective/available reconstruction, live timing and controller observations. Temporal
off shows Off/N/A and full resolution while retaining requests. `DiagnosticLegend` supplies
shader-derived legends and Raw placeholder notes; Viewport owns framing, the top toolbar playback. App alone declares
`Render/Passes/SelectionOutline/SelectionOutline` silhouette/scene-depth/composite passes into a separate SDR target; graph costs remain
visible, while ordinary Renderer/offscreen paths and temporal histories are untouched.
`EditorActions`/`ActionFeedback` share capability-aware results while the frame loop executes
capture. `DynamicResolution.h` drives the shell-owned controller from retired timed-pass sums.
`GraphNodeModel`/`GraphLayout` preserve logical canvas identity across physical temporal instances.
Editor/capture loops share session playback/views/motion and declaration/retention, but own their
waits, UI sink and presentation scheduling; full structure: `docs/architecture/overview.md`).
`LightClusters` mirrors fixed 16×9×24 pixel-aligned froxels; `LightClusterStage` owns three paced reset/count/scan/fill slots with 128 lights/froxel and 65,536 indices. Shared `LocalLights.slang` selects Off/Direct/Clustered using b8 lights/b9 grid/b10 indices/b11 frame params; PassUniforms stays 400 B. Normal-footprint filtering broadens only punctual specular alpha; authored roughness/directional/IBL remain unchanged. Zero-enabled frames add no light import/pass. `LightingStatus` joins declaration-time mirror and retired GPU lists/counters; post-display Count/Overflow/Missed diagnostics preserve scene/history. `LightingDisplay` owns 250 ms coherent readings with immediate warnings; accepted ADR 0023 owns the contract; validation records retain failed historical gates.
`Render/Renderer/DisplayDomain.h` names the opaque 8-bit SDR BT.709/sRGB/PBR Neutral output; Renderer exposes it to capture metadata and
the read-only Inspector Display details (domain, encoded SDR UI, backing scale and 1:1 status).
Asset `PngImage` owns tagged PNG read/write; RHI is SDR-only. Build: unit-local `xmake.lua`, shared `xmake/` tasks/rules/setup. Shaders: `Shaders/Common/` owns
Encode, Lighting, LocalLights, Shadow, Motion, Tonemap, SceneTables and AlphaMask. Each pass family's entries and local modules live in `Shaders/Passes/<family>/`:
ScenePass/ScenePassAuto, ScenePassMask/ScenePassAutoMask, ShadowPass/ShadowPassMask, Sky/SkyAuto,
HistogramAccumulate, ExposureSeed, ExposureResolve, BloomThreshold/BloomDownsample/BloomUpsample,
DisplayTransform, TemporalReproject, TemporalResolve, TemporalUpscale, SpatialUpscale, TemporalDebugView, VendorTemporalPack (with module TemporalCommon),
SelectionMask and SelectionOutline (editor-only).
`Shaders/Tests/` owns FrameDataQuad and the sampler/shadow/fullscreen/MRT/compute-image/buffer-hazard/full-field scene-table ABI oracles. `RojoRHI/Shaders/Tests/` is the RHI component's own tree over `Modules/Shadow.slang`: Triangle, the cube/render-area/compute/indirect/binding-limit smoke shaders, and byte-identical copies of the six oracles both test targets need. Runtime LightClusterCount/Scan/Fill and LightDebugView entries build and inspect local-light assignment. Runtime basenames stay unchanged; frame walkthrough: `docs/frame-pipeline.md`.
## Hard rules
- C++23. No Metal 3 fallback (`MTLGPUFamilyMetal4` required). 3 frames in flight.
- Creation returns `Result<T>`; misuse is `LMX_ASSERT`. GPU objects always get labels.
- xrepo deps only as needed (current: libsdl3, glm, spdlog, catch2, cgltf, stb). ThirdParty/ and
  Assets/Fetched/ are fetched via `xmake setup`, pinned in xmake/setup.lua, never committed.
- Integration uses squash merge, normally one commit per milestone; keep staged work in one final PR
  where practical. Preserve validation revisions before branch deletion; see `docs/conventions/commits.md`.
- Engineering and documentation follow `docs/conventions/`. Every commit compiles, passes the
  relevant tests and `xmake policy` (including private-header visibility and shader import checks).
- Experimental source does not live on `main`. Preserve accepted evidence with an immutable tag
  and develop a new experiment on a short-lived `exp/<topic>` branch; only conclusions, ADRs, and
  adopted production code return to `main`.
- Lighting math runs in scene-linear space and is pre-exposed before the scene target sees it;
  authored color constants, including the editor's clear color, decode via `lmx::srgbToLinear` in `Core/Math/Color.h` once at scene build or
  pass declaration. Nothing upstream of `Shaders/Passes/Display/DisplayTransform.slang` encodes sRGB.
- Public-facing copy (README, GitHub About, release text, gallery captions) leads with shipped
  rendering behavior and uses plain feature themes for future work. It never exposes milestone
  numbers, task/plan status, or an unimplemented backend as a current capability. `CLAUDE.md`
  imports this file, so the same rule applies to Codex and Claude Code.
## Update policy
Refresh this file whenever a command, architecture contract, or hard rule changes.
