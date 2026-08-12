// P03's address-first frontend (spec section 6): the depth-only shadow pass, expressed the way the
// prototype binds rather than the way `Shaders/ShadowPass.slang` does. The math is the one line
// ShadowPass owns -- `mul(mvp, position)` -- restated here because a two-statement vertex shader has
// no module to share; everything that *could* diverge (the vertex layout, the matrix, the winding)
// is pinned to the same values the incumbent uses.
//
// Binding surface: one per-draw root block at buffer 0, carrying the draw's `mvp` and the address of
// the shared vertex array. Nothing is bound: no vertex buffer, no uniform buffer, no descriptor. The
// fragment stage reads no root data at all, so `draw` passes `kNullAddress` for it and the prototype
// makes one `setAddress` call per draw rather than two.
import NoApiBindless;

struct ShadowObjectRoot
{
    float4x4 mvp;
    ConstBufferPointer<Vertex> vertices;
    uint2 padding; // Keeps the block a multiple of its own 16-byte alignment.
};

[[vk::binding(0, 0)]]
ConstantBuffer<ShadowObjectRoot> gObject : register(b0); // → MSL [[buffer(0)]], the vertex root

[shader("vertex")]
float4 vertexMain(uint vid: SV_VertexID): SV_Position
{
    return mul(gObject.mvp, float4(gObject.vertices[vid].position, 1.0));
}

[shader("fragment")]
void fragmentMain()
{
}
