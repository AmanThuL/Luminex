# ADR 0003: Shader pipeline — Slang, two-step MSL compile

**Status**: Accepted (2026-08-07) · Amended 2026-08-07 (A1) · **Spec**: ../specs/2026-08-07-luminex-upgrade-design.md (D5, D6)

## Context
Slang is Khronos-hosted, 34% adoption and rising: one `.slang` source targets MSL now, SPIR-V/DXIL later.
Slang's direct-to-metallib path has open Metal 4 bugs (slang #12325, #12096), so MSL is generated as a
readable intermediate step. **Amendment A1**: at bootstrap, Apple's offline Metal toolchain download was
unavailable (asset-catalog outage); the OS runtime shader compiler (`newLibrary(source:)`) was verified
working instead. The toolchain was later installed manually; offline `xcrun metal -std=metal4.0` works now.

## Decision
Pipeline: `slangc` → readable MSL (kept in the build tree, per shader-style.md) → runtime
`newLibrary(source:)` compile at load. The xmake shader rule also precompiles `.metallib` via
`xcrun metal -std=metal4.0` whenever `xcrun metal` exists — auto-enabling offline precompile with no code
change. `Device::loadShaderLibrary` prefers a precompiled `.metallib`, else runtime-compiles the `.metal`.

## Consequences
Two shader-loading paths (precompiled vs. runtime) must stay behavior-identical. Runtime compile adds a
small first-load cost when `.metallib` is absent but keeps the build unblocked by the offline toolchain
component. Collapse to Slang→metallib directly once the upstream Slang bugs close.
