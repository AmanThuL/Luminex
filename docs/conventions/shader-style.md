# Shader Style (Slang)

**Status**: Accepted

- One `.slang` file per pipeline/feature in `Shaders/`; entry points `vertexMain`/`fragmentMain`/`computeMain`
  marked with `[shader("...")]` attributes (no `-entry` flags at compile time).
- Globals: `gPascalCase` resources, `kPascalCase` constants. Explicit flat binding indices map 1:1
  to `MTL4ArgumentTable` slots in the current API; any replacement binding model or D3D12 lowering
  must update this convention with its shader ABI.
- Build: Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib` (two-step; see ADR 0003).
  Generated `.metal` stays in the build tree for debugging — read it when a shader misbehaves.
