# M2 Backlog — carried from the M1 final review (2026-08-07)

Deferred items triaged DEFER-TO-M2 by the M1 whole-branch review. None block M1; each names its origin.

## Hard gate for M2's first dynamic binding
- **One argument table per frame in flight.** The single device-wide `MTL4ArgumentTable` is safe in M1 only because every frame rebinds the same static vertex-buffer address (idempotent write). The moment any binding varies per frame it is a data race. Documented at `Metal4Device.h` (T10 review; final review "loud carry-forward").

## Validation / hardening
- Reject `renderTarget && !isColorRenderableFormat(format)` in `validate(TextureDesc)` (3 lines + 1 test) when depth attachments land — closes the last abort-on-nil door (final review C-3).
- Pin the `Format::Unknown`-branch tests with a `contains("Unknown")` assert so the whitelist can't silently absorb the branch (T9 re-review nit).
- Rule-level guard against two targets sharing a shader output dir (defense-in-depth; the concrete race was fixed with Tests' own targetdir — T12 F3).
- Capture-guard unit test (empty/non-.gputrace path) — currently behaviorally verified only; needs `MTL_CAPTURE_ENABLED` plumbing to be testable (T14 review).

## Code shape / perf notes
- Remove per-draw `NS::AutoreleasePool` when draw counts grow (measured-safe now, wrong shape for a hot path — T11 review).
- Device-dtor autorelease pool drains before member release (body-local pool ordering); revisit when teardown grows (T8 M-1).
- `newFunction()` returns nil for entries with function constants — revisit when specialization constants arrive (T9 re-review).
- Skip a bare `--` in App arg parsing; bound-check `logPixel` (T11 nits).

## Toolchain / upstream
- **Slang emits `device` (not `const device`) pointers for read-only `StructuredBuffer`** → benign "writable resources in non-void vertex function" Metal warning on every shader compile; no slangc knob exists. Track upstream; also means the vertex buffer binds as writable (hazard-tracking cost later). (T6 C1, recurring.)
- Slang direct-metallib path: collapse the two-step compile when slang issues #12325/#12096 close (ADR 0003).
- clang-tidy non-blocking CI job (spec D8, deferred at M1 size — spec §6 "M1 deviations").
- Cache xmake/xrepo packages in CI (`~/.xmake/packages`) for wall-clock (T13 note).

## M2 feature work (from spec §8)
- `lmx::render` becomes real: mesh/camera/uniform plumbing, depth buffer, ImGui overlay (xrepo `imgui` has SDL3 + Metal 4 backends).
