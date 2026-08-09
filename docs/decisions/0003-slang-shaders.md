# ADR 0003: Shader pipeline — Slang, two-step MSL compile

**Status**: Accepted (2026-08-07) · **Spec**: ../specs/2026-08-07-luminex-upgrade-design.md (D5, D6)

## Context
One `.slang` source can target MSL now and SPIR-V/DXIL for future backends. Slang's direct-to-metallib
path has open Metal 4 bugs (slang #12325, #12096), so the build emits readable MSL. The offline Metal
toolchain is optional; the OS runtime shader compiler provides the supported fallback.

## Decision
Pipeline: `slangc` → readable MSL (kept in the build tree, per shader-style.md) → runtime
`newLibrary(source:)` compile at load. The xmake shader rule also precompiles `.metallib` via
`xcrun metal -std=metal4.0` whenever `xcrun metal` exists — auto-enabling offline precompile with no code
change. `Device::loadShaderLibrary` prefers a precompiled `.metallib`, else runtime-compiles the `.metal`.

## Consequences
Two shader-loading paths (precompiled vs. runtime) must stay behavior-identical. Runtime compile adds a
small first-load cost when `.metallib` is absent but keeps the build unblocked by the offline toolchain
component. Collapse to Slang→metallib directly once the upstream Slang bugs close.
