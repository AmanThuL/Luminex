# M2 Backlog — carried from the M1 final review (2026-08-07)

Deferred items triaged DEFER-TO-M2 by the M1 whole-branch review. Closed out at the M2 milestone
boundary (2026-08-07) — every item below is either closed with the commit that closed it, or
explicitly re-deferred with a reason. See `docs/plans/2026-08-07-m2-renderer-skeleton.md` for the
full task-by-task record and `docs/specs/2026-08-07-m2-renderer-skeleton-design.md` §8 for the
spec-level disposition.

## Hard gate for M2's first dynamic binding
- **One argument table per frame in flight.** **Closed in M2 (Task 2, commit `099e623`
  "Close the argument-table hard gate: one table per frame in flight").** The single device-wide
  `MTL4ArgumentTable` is safe in M1 only because every frame rebinds the same static vertex-buffer
  address (idempotent write). The moment any binding varies per frame it is a data race. Now
  documented on the `m_argumentTables` member comment in `Metal4Device.h` (not "at `Metal4Device.h`"
  generically — the member itself carries the rotation rationale).

## Validation / hardening
- Reject `renderTarget && !isColorRenderableFormat(format)` in `validate(TextureDesc)` — **Closed in
  M2 (Task 4, commit `b8f57c1` "Grow the RHI: depth attachments, sampled targets, format
  validation", tightened in `d42d843` "Close the texture-usage validation gap and the depth
  load-action door").** Depth formats are accepted alongside color-renderable ones now that depth
  attachments exist.
- Pin the `Format::Unknown`-branch tests with a `contains("Unknown")` assert — **Closed in M2 (Task
  1, commit `88dbdf0` "Close M1-review hardening items: arg nits, Unknown pins, outdir guard").**
- Rule-level guard against two targets sharing a shader output dir — **Closed in M2 (Task 1, commit
  `88dbdf0`).** `slang2metallib`'s `on_buildcmd_file` now raises loudly when two opted-in targets
  share a targetdir.
- Capture-guard unit test (empty/non-.gputrace path) — **Closed in M2 (Task 11, commit `f7730b3`
  "Add capture-guard tests with MTL_CAPTURE_ENABLED plumbing").** `Tests/CaptureTests.cpp` covers the
  bad-path guard unconditionally and the happy path best-effort (`SKIP` when capture is unavailable
  in the environment). Note: both cases are `[gpu]`-tagged (need a real `Device&`), not
  `Tests/unit` — CI, which runs `Tests/unit` only, does not exercise this coverage; it is a local
  gate, same as the rest of the GPU smoke suite.

## Code shape / perf notes
- Remove per-draw `NS::AutoreleasePool` when draw counts grow — **Closed in M2 (Task 2, commit
  `099e623`).** Pools moved to render-pass scope; per-call pools removed from `bindPipeline`,
  `bindVertexBuffer`, `draw`.
- Device-dtor autorelease pool drains before member release (body-local pool ordering) — **Re-
  deferred.** Nothing in M2 touched device teardown; still a watch-item for whenever teardown grows
  more complex than it is today.
- `newFunction()` returns nil for entries with function constants — **Re-deferred.** No M2 shader
  uses specialization constants; revisit when one does.
- Skip a bare `--` in App arg parsing; bound-check `logPixel` — **Closed in M2 (Task 1, commit
  `88dbdf0`).**

## Toolchain / upstream
- **Slang emits `device` (not `const device`) pointers for read-only `StructuredBuffer`** → benign
  "writable resources in non-void vertex function" Metal warning on every shader compile — **Re-
  deferred, upstream.** Still no slangc knob; confirmed still present on `Shaders/Mesh.slang` (Task
  8 record) exactly as on M1's `Shaders/Triangle.slang`. Track upstream.
- Slang direct-metallib path: collapse the two-step compile when slang issues #12325/#12096 close
  (ADR 0003) — **Re-deferred, upstream.** Blocked on the same upstream issues; no change possible
  from this repo.
- clang-tidy non-blocking CI job (spec D8, deferred at M1 size) — **Closed in M2 (Task 12, commit
  `26b1951` "Add non-blocking clang-tidy CI job and xmake package cache").** `.clang-tidy` +
  `continue-on-error` job added, Metal4 backend TUs excluded (third-party header noise).
- Cache xmake/xrepo packages in CI (`~/.xmake/packages`) — **Closed in M2 (Task 12, commit
  `26b1951`, same commit as the tidy job).**

## M2 feature work (from spec §8)
- `lmx::render` becomes real: mesh/camera/uniform plumbing, depth buffer, ImGui overlay — **Closed
  in M2**, across:
  - Camera + procedural meshes (Task 7, commit `47d590b` "Add lmx::render Camera and procedural
    meshes with tests").
  - `Renderer` with depth-tested draws, per-draw uniforms, and the render→barrier→sample path
    proven on the GPU (Task 8, commit `5e3a2d9` "Add lmx::render Renderer with GPU-proven depth,
    uniforms, and barrier").
  - Metal 4 ImGui backend glue (Task 9, commits `7087c43` "Add ImGui renderer glue for the Metal 4
    backend" and `072971e` "Tighten the ImGui glue's contracts after review").
  - The docked editor shell, fly camera, and offscreen viewport replacing the M1 triangle app (Task
    10, commits `7f6c6f8` "Move the App's offscreen screenshot onto the M2 scene", `bb953e0`
    "Replace the triangle app with the M2 editor shell", and `9ca4dcc` "Release displayed textures
    from the ImGui residency set").
