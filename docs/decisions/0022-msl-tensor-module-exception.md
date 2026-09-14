# ADR 0022: Bounded MSL-authored tensor modules for learned-rendering studies

**Status**: Proposed (2026-09-14) · **Roadmap**: ../roadmap/neural-rendering.md

## Context

[ADR 0003](0003-slang-shaders.md) makes Slang the single shader source: every `.slang` file
compiles to readable MSL kept in the build tree, then to a Metal library. Metal 4 adds tensor
types and operations in the Metal Shading Language (`tensor_handle`, `tensor_inline`,
`cooperative_tensor`, `matmul2d`) and an ML command encoder for exported packages. Slang's Metal
target has no lowering for these types; its cooperative-vector support targets SPIR-V and DXIL,
and the SIGGRAPH 2026 neural-shading course marks those examples as non-Metal. The
[direction review](../research/2026-09-14-rendering-direction-review.md) records the evidence.

[Part V](../roadmap/neural-rendering.md) needs to evaluate the MSL path; without it the project
can measure only plain-ALU shaders and the ML encoder. Waiting for Slang is an unbounded delay.

## Decision

Metal Shading Language source may be authored directly, only under these bounds:

1. **Scope.** Hand-authored MSL exists only for tensor or machine-learning operations that Slang
   cannot express for the Metal target, and only inside Part V studies. Production passes outside
   an N-slice keep Slang as their sole source. Each module states in its header the Slang gap it
   works around, with a link to the upstream issue where one exists.
2. **Paired fallback.** Every MSL module has a plain FP16 Slang implementation of the same
   network with the same weights and inputs. Both paths are validated against the CPU numerical
   oracle within the same tolerance; the Slang path is the capability fallback and the reference.
3. **Location and build.** Hand-authored modules live under a dedicated `Shaders/Metal/`
   directory, compile through the same runtime and optional precompile paths as generated MSL,
   and follow the existing labeling, naming and header-comment conventions. The policy checkers
   verify that no MSL outside that directory is committed and that each module names its Slang
   pair.
4. **No leakage.** Public RHI headers, render-graph declarations and scene/material contracts do
   not depend on the MSL modules' existence. A pass declares its inputs and outputs the same way
   whichever implementation runs.
5. **Removal path.** When Slang gains Metal tensor lowering that reproduces the oracle within
   tolerance, the MSL module is replaced by Slang source in the same study and deleted. A study
   that closes with defer removes its MSL modules from the published baseline together with the
   rest of its experimental source.

## Consequences

Part V can measure Apple's shipped tensor path now, with an honest comparison against the Slang
path and a stated hardware floor: the code compiles on any Metal 4 device, and acceleration is
measured only on M5/A19 Pro-class GPUs. The two-source discipline costs one duplicate
implementation per network, which is acceptable for tiny networks and is the mechanism that keeps
the oracle meaningful.

This decision does not amend ADR 0003 for production shaders, does not adopt any learned pass,
and does not change the shader import, naming or header checks except to add the directory
rule. It becomes Accepted when N1's plan is approved and the checker rule exists.
