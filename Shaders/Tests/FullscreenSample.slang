// Test-only: fullscreen triangle that copies a texture via Load — no sampler state needed.
import RojoRHI;
import OracleRoots;

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vid = rojoRHI::vertexIndex(ids);
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    let params = rojoRHI::root<FullscreenSampleRoot>();
    return rojoRHI::texture2D<float4>(params.source).Load(int3(int2(in.position.xy), 0));
}
