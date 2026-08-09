# Rendering Roadmap

**Status**: Accepted

The roadmap orders renderer capabilities by dependency and observable exit gates. Detailed evidence
is preserved in the frozen research synthesis and supporting notebooks under `docs/research/`.

## Current baseline

M3 renders Sponza and Damaged Helmet through a shadow-mapped, normal-mapped,
gamma-correct forward pipeline. M3.1 established the audited engineering baseline without adding a
rendering feature; its shipped evidence and remaining limits are recorded in `docs/milestones/m3.1.md`.

## M4 — Image formation and render-graph foundation

Outcome: the current frame runs through a small validating render graph and produces a scene-linear
HDR image with physically based glTF materials.

- Add GPU timestamps, compute/storage contracts, and resource views/usages while preserving the
  capture-readable pass labels established by the baseline.
- Introduce a minimal dependency-aware render graph with validation and transient lifetime tracking.
- Add correct filtered mip generation, tangent/normal transforms, metallic-roughness PBR, IBL,
  reversed-Z, FP16 HDR, exposure, and a replaceable display transform.
- Add `MaterialLab` as the deterministic material and lighting regression scene.

Exit: intermediates are inspectable; graph validation catches undeclared usage; Sponza and Helmet
match reference material behavior; HDR/exposure is deterministic; GPU timings are reported per pass.

## M5 — Temporal contract

Outcome: every frame owns explicit current/previous state and a portable reconstruction path.

- Add jitter, previous transforms, motion vectors, disocclusion/reactive masks, history invalidation,
  dynamic resolution, and a reference TAA/TAAU implementation.
- Integrate MetalFX through an adapter with the same inputs; retain the reference path for validation
  and future backends.

Exit: camera cuts, resize, scene changes, animation, and resolution changes invalidate history
correctly; motion-vector and rejection buffers are independently visualizable.

## M6 — GPU scene and visibility

Outcome: stable bindless scene data drives measured GPU visibility and indirect submission.

- Add stable instance/material/mesh IDs, GPU scene buffers, HZB, frustum/LOD/occlusion culling,
  indirect work, and clustered light lists.
- Keep a measured Forward+ reference path. Evaluate compact deferred or visibility-buffer paths only
  after representative scenes expose a real bandwidth or material-diversity constraint.

Exit: CPU submission no longer scales per visible draw; culling has correctness visualizations and
timings; fallback behavior exists for missing optional capabilities.

## Later investigations

Ray queries, denoising, ReSTIR, virtualized geometry, sparse resources, GI, and a reference path
tracer remain research candidates. None is a baseline dependency until its required scene, fallback,
performance budget, and debugging surfaces are named.
