# Shader Style (Slang)

- One `.slang` file per pipeline/feature in `Shaders/`; entry points `vertexMain`/`fragmentMain`/`computeMain`
  marked with `[shader("...")]` attributes (no `-entry` flags at compile time).
- Globals: `gPascalCase` resources, `kPascalCase` constants. Explicit flat binding indices — they map 1:1 to
  `MTL4ArgumentTable` slots today and Vulkan descriptor sets later (spec §5).
- Build: Slang → MSL source → `xcrun metal -std=metal4.0` → `.metallib` (two-step; see ADR 0003).
  Generated `.metal` stays in the build tree for debugging — read it when a shader misbehaves.
