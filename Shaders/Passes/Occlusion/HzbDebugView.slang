// Raw reversed depth, nearest-expanded over the active rectangle; white is near and black far.
struct HzbDebugViewParams
{
    uint level;
    uint activeWidth;
    uint activeHeight;
    uint padding;
    uint outputWidth;
    uint outputHeight;
    uint padding2;
    uint padding3;
};
[[vk::binding(0, 0)]]
Texture2D<float> gPyramid : register(t0);
[[vk::binding(0, 0)]]
ConstantBuffer<HzbDebugViewParams> gParams : register(b0);
struct VertexOutput
{
    float4 position : SV_Position;
};
[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID)
{
    VertexOutput output;
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    uint2 active = uint2(gParams.activeWidth, gParams.activeHeight);
    uint2 source = min(uint2(input.position.xy * float2(active) /
                            float2(gParams.outputWidth, gParams.outputHeight)), active - 1);
    uint2 coordinate = source >> (gParams.level + 1);
    float depth = gPyramid.Load(int3(coordinate, gParams.level));
    return float4(depth, depth, depth, 1.0);
}
