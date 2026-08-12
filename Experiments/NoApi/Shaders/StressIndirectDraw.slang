// M5.1 Stage 4 I1/I2 oracle (spec section 7), maintained-RHI side: the smallest possible draw a
// drawIndirect/drawIndexedIndirect case can verify -- a full-screen triangle in a fixed colour, with
// no vertex buffer, texture, or sampler needed. Whether the triangle appears at all, and whether it
// covers the whole target, is exactly what proves the GPU read `vertexCount`/`indexCount` (and not
// zero) out of the indirect argument block this case wrote.

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexIndirect(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle from a bare vertex (or fetched index) id.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentSolid(VSOutput in): SV_Target
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
