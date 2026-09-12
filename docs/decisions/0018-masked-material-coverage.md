# Masked material coverage

**Status**: Accepted (2026-09-12)

## Context

San Miguel's foliage needs binary texture coverage to exercise temporal reconstruction on real
content. The existing opaque path ignores authored alpha and remains the native-parity reference.
Blended transparency is a separate roadmap boundary.

## Decision

`render::AlphaMode` supports `Opaque` and `Mask`. A masked fragment survives when sampled
base-color alpha multiplied by the material's base-color-factor alpha is at least `alphaCutoff`.
glTF MASK defaults to 0.5, carries a finite nonnegative authored cutoff, and optionally renders both
faces; back-facing masked surfaces reverse their shading normal. Referenced BLEND materials fail
loading explicitly rather than pretending to be opaque.

Dedicated masked scene, auto-exposure scene and shadow pipelines preserve the original opaque
shaders and object uniform ABI. Scene color, depth, motion and reactive coverage come from the same
fragment invocation; masked shadows use the same texture, UV transform, factor and cutoff. The
cutoff uses a separate frame-data block; the capture schema records it. Masked pipelines support
one- and two-sided culling. This does not add an RHI feature.

The San Miguel converter opts into diffuse PNG alpha and its verified `N_` tangent-normal naming
convention. Existing Sponza conversion remains unchanged. Phong-to-PBR mapping is approximate;
source glass/water transmission, specular maps and height bumps are not represented as new renderer
features. Preserve upstream license and conversion provenance alongside fetched assets.

## Consequences and validation

GPU tests pin holes in scene/depth/motion and real cast shadows, material UV transforms, rigid and
invalid motion, cutoff equality and back-face shading. Native opaque parity remains independently
checked. Color/shadow sample footprints can select different mip levels; this contract shares the
coverage function, not a screen-space pixel mask across different projections.

Ordinary filtered alpha mipmaps can shrink thin leaves under minification. Coverage-preserving
mips, alpha-to-coverage, translucent blending, transmission and deforming foliage are not introduced
by this decision. Temporal reconstruction can expose these content limitations; difference maps
must not attribute every such artifact to the reconstruction algorithm.

The [design extension](../specs/2026-09-12-m6.4-metalfx-temporal-adapter-design.md) owns the bounded
real-scene comparison work; the [milestone record](../milestones/m6.4.md) owns measured evidence.
