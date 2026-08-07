# M3 Backlog — carried from the M2 final review (2026-08-07)

Items surfaced by the M2 final whole-branch review that do not block M2 close but should land in
M3. Each item names its origin. The first section is promoted to M3-plan-level treatment (not
just a backlog line); the rest are standard carry-forward items to triage when the M3 plan is
written.

## Promoted to M3 plan-level
- **Cross-frame uniform-ring overlap is unproven.** The rotation test drains per frame, so
  concurrent in-flight reads rest on validation-clean `LMX_MAX_FRAMES` runs only, not a targeted
  proof. Needs an RHI way to read back without draining, or an M3-plan-level measurement. (Final
  review.)
- **`beginRenderPass` has no color/depth extent-match assert.** `Renderer` keeps its color and
  depth targets in lockstep today by construction, so the gap has not bitten yet — M3 shadow maps
  will not keep them in lockstep. (Final review.)

## Hardening (cheap)
- `imguiForgetTexture`'s post-`g_device` early-return should be an assert instead — closes the
  same silent struct-reorder failure mode as `imguiResidencySet()`. (Final review.)
- `imguiResidencySet()` needs a `GetCurrentContext` guard. (Final review.)
- `Metal4ImGui.h`'s top-of-file example still models caller-side `waitIdle` — needs a one-word
  annotation noting that. (Final review.)

## Code shape / naming
- `bindVertexBuffer` → `bindBuffer` rename: bindless vertex-pulling means both methods already
  write the same address array. (Final review.)
- `kUniformOffsetAlignment`'s home — `Metal4Common` vs `Device.h` — needs deciding. (Final
  review.)
- `RHI.h` notes: `bindTexture`'s documented contract is narrower than its enforcement
  (`cpuReadback` is also accepted, only documented backend-side); the buffer-slot index space
  shared by `bindVertexBuffer`/`setUniforms` is unstated. (Final review.)
- Sweep `Source/` for milestone-qualified task citations — M1/M2 task numbers collide unqualified
  in places. (Final review.)

## Comments/docs polish
- `Mesh.cpp`'s label-lifetime mechanism relies on full-expression temporaries, not a base — worth
  a clarifying comment. (Final review.)
- App's `--` comment overclaims. (Final review.)
- `RenderTests`' self-referential move assertion and its pitch-clamp value need a clearer note.
  (Final review.)
- T8's capture-control comment is missing its "Verified" marker. (Final review.)
- README wants a real docked-editor-shell image (a windowed capture, not the offscreen
  `--screenshot` render) — M3 or Rudy's session. (Final review.)

## Watch items carried from the M2 ledger
- Device-dtor autorelease pool drains before member release (body-local pool ordering); revisit
  when teardown grows. (Carried unchanged from `m2-backlog.md`, originally T8 M-1.)
- `newFunction()` returns nil for entries with function constants — revisit when specialization
  constants arrive. (Carried unchanged from `m2-backlog.md`, originally T9 re-review.)
- Vendored imgui process-teardown leak — upstream, exit-only, unchanged.
- Tidy sweep excludes `.mm` files — moot today (none in `Source/`), watch as the codebase grows.
- `EditorShell` debounce state isn't reset when its panel is collapsed — inert today.
- Slang upstream items (`device`-pointer warning, direct-metallib collapse) — unchanged from
  `m2-backlog.md`, re-deferred again.
