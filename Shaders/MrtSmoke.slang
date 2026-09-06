// Test-only: fullscreen triangle writing two colour attachments in one pass.
struct VSOutput
{
    float4 position : SV_Position;
};

struct FSOutput
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

// Both values are exactly representable as halves, so a readback of the second attachment can be
// compared bit for bit rather than within a tolerance.
[shader("fragment")]
FSOutput fragmentMain(VSOutput in)
{
    FSOutput o;
    o.color = float4(1.0, 0.0, 0.0, 1.0);
    o.motion = float2(0.25, -0.5);
    return o;
}
