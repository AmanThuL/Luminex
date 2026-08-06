# ADR 0004: RHI philosophy — thin, explicit, honest

**Status**: Accepted (2026-08-07) · **Spec**: ../specs/2026-08-07-luminex-upgrade-design.md (§4)

## Context
`lmx::rhi` needs to model the shared conceptual core of Metal 4 / Vulkan / D3D12 without speculating
ahead of real feature demand — retrofitting bindless onto a slot-based RHI, or abstracting features no
milestone uses, are the classic mistakes this project wants to avoid. References: NVRHI's interface
shape, WickedEngine's production `wiGraphicsDevice_Metal.cpp`, Apple's game-porting-toolkit MTL4 skills.

## Decision
Expose only what the current milestone needs (M1: `Device`, `Swapchain`, `CommandList`, `Buffer`,
`Texture`, `ShaderLibrary`, `GraphicsPipeline`) and grow it feature-by-feature. No Metal (or other
backend) types leak through public headers; the backend is selected at build time behind a factory.
Creation returns `std::expected<T, Error>`; API misuse is a fatal `LMX_ASSERT`, never an error code.
Binding model is argument-table/bindless-first, since all three target APIs converge there.

## Consequences
V1 deliberately omits explicit barriers, compute, multi-queue, dynamic residency, queries, and RT —
each gets a known Metal 4 answer once a real milestone demands it. The RHI stays smaller and more
honest than a speculative "complete" abstraction, at the cost of revisiting its shape as backends grow.
