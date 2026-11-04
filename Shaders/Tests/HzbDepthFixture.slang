import RojoRHI;
import VisibilityOracleRoots;
import AlphaMask;
struct VertexOutput
{
    float4 position : SV_Position;
};
[shader("vertex")]
VertexOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vertex = rojoRHI::vertexIndex(ids);
    VertexOutput output;
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
    return output;
}
[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Depth
{
    uint gMasked = rojoRHI::root<HzbDepthRoot>().masked;
    uint2 p = uint2(input.position.xy);
    if (gMasked != 0)
        applyAlphaMask(((p.x / 7 + p.y / 5) % 3) == 0 ? 0.0 : 1.0, 0.5);
    return float(1 + (p.x * 17 + p.y * 31) % 1000) / 1024.0;
}
