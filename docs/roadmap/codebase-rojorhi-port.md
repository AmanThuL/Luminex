# Codebase Refactoring — RojoRHI Interface Port (R5)

**Status**: Proposed

Third file of Part III of the [rendering roadmap](../roadmap.md).
[Module Boundaries](codebase-module-boundaries.md) holds the module contract and R1;
[Repository, Subsystem and Shader Restructuring](codebase-restructuring.md) holds R2–R4. This file
holds R5, proposed on 2026-10-22 as execution-sequence step 18, after UX6 and before N1. It adds no
rendering scope. The [R5 record](../milestones/r/r5.md) holds the design and the parity gate in full.

## R5 — Luminex runs on the RojoRHI v2 interface

**Outcome:** Luminex `main` builds and runs only on RojoRHI's v2 interface (`RojoRHI` and
`RojoRHIImGui`). The editor with detached windows, headless `--screenshot`, `--capture-sequence`
and `--measure`, GPU capture and every rendered feature work as on v1, and RojoRHI is free to delete
v1. RojoRHI gains only what the port shows it needs, each addition merged in RojoRHI first.

**Sequence:** R5.1 → R5.2 and R5.3 (overlapping where their files are disjoint) → R5.4 → R5.5 on
the integration branch `feat/rojorhi-v2`, which reaches `main` as one squash commit; then R5.6 and
R5.7 as separate squash pull requests. R5.1–R5.3 stay on v1 and change no pixel. Luminex `main`
takes no work outside R5 until R5.5 integrates.

**Exit gate:** the [parity gate](../milestones/r/r5.md#parity-gate) against a frozen v1 build,
tag `r5-v1-reference`: tests at ledger-adjusted counts, images within Luminex's `strict` and
vendor profiles with byte identity reported per case, 600-frame smokes with no validation message,
and paired costs not slower beyond noise.

**Defer:** Luminex on D3D12; new rendering features; scene-pass deduplication, which
[ADR 0027](../decisions/0027-scene-pass-deduplication-defer.md) owns; mesh shaders; async compute
and a pipeline cache before parity; re-baselining historical evidence (M5.2 frame-data and M7
paired results stay history).

### R5.1 — Legacy-target bump

**Outcome:** Luminex builds v1 from RojoRHI `main`, where it lives under `Legacy/`.

**Deliver:** the pin moved to RojoRHI `main`; dependencies renamed to the legacy targets; hosted CI
configured on RojoRHI's Null backend with the legacy test paths; the module contract's names.

**Exit gate:** hosted CI green; local `Tests/gpu` and RojoRHI's legacy GPU cases green; parity
15/15 byte-identical.

### R5.2 — Foundation seams

**Outcome:** Luminex-owned seams absorb what v2 does differently, still on v1.

**Deliver:** in two or three pull requests, Luminex's error type, `GpuContext` (frame counter, idle
wait, shader loader), a readback API, GPU owners without getters, `CaptureSchema` in Luminex,
`asset::ImageData`, Luminex pass timings and upscaler support info. No abstraction over binding,
pipelines or draws.

**Exit gate:** per pull request, hosted green, `Tests/gpu` green, parity 15/15 byte-identical and
test counts unchanged.

### R5.3 — Graph and test seams

**Outcome:** the render graph lowers to a frame description that either interface can emit.

**Deliver:** Luminex use vocabulary, a lowered frame with a v1 emitter, a texture state table with
stateful imports, a transient backend interface, pass scopes in the lowering and pure root builders
per stage; graph tests on the lowered frame; test fakes reduced to Luminex seams.

**Exit gate:** as R5.2, and the graph dump goldens unchanged.

### R5.4 — Interface switch and stage ports

**Outcome:** on `feat/rojorhi-v2`, Luminex runs on v2, first as a minimal frame and then with every
stage family.

**Deliver:** the RojoRHI shader rule in place of Luminex's own; the GPU layer, graph emitter,
frame loops, editor adapter with detached windows and capture on v2; a hosted Null configure with a
Debug `[tracker]` job; then scene and shadow, visibility and occlusion, local lights, post and
editor passes, temporal and vendor stages, in parallel where their dependencies allow.

**Exit gate:** the branch builds on Null and Metal 4; the editor runs 600 frames validation-clean;
graph dumps byte-identical; each family's `[gpu]` cases green and its images within the gate.

### R5.5 — Parity and integration

**Outcome:** the integration branch passes the parity gate and reaches `main`.

**Deliver:** every stage back; the test ledger; goldens re-recorded only with frozen-v1
comparisons; `FrameDataBench` and `Tools/GpuDebug` on v2; architecture pages and `AGENTS.md`;
[ADR 0033](../decisions/0033-rojorhi-conformance-replaces-checkpoint-a.md) accepted; the gate's
evidence; a validated tag; the squash commit `rhi: run on RojoRHI v2 (R5)`.

**Exit gate:** the parity gate passes in full and R5 is accepted.

### R5.6 — Pin the release without v1

**Outcome:** Luminex pins the RojoRHI release that deletes v1.

**Exit gate:** hosted green; `Tests/gpu` green; parity 15/15 byte-identical against R5.5's images.

### R5.7 — Optimization

**Outcome:** the v2 frame is measured against the frozen v1 build and made cheaper where evidence
allows.

**Deliver:** bindless material rows, counted multi-draws per pipeline bucket, static pass roots and
narrowed raster stages, then profile-led items.

**Exit gate:** per pull request, gates 1–3 against the frozen build, costs reported with paired
evidence, and no cost regression.
