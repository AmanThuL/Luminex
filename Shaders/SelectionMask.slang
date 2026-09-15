// Editor selection coverage uses the same alpha rule as scene and shadow rendering.
import AlphaMask;
import SceneTables;

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};
[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0);
struct MaskParams
{
    float4x4 viewProj;
    uint instanceRow;
    float selected;
    uint padding0;
    uint padding1;
};
[[vk::binding(1, 0)]]
ConstantBuffer<MaskParams> gMask : register(b1);
[[vk::binding(5, 0)]]
StructuredBuffer<InstanceRow> gInstances : register(t5);
[[vk::binding(6, 0)]]
StructuredBuffer<MaterialRow> gMaterials : register(t6);
[[vk::binding(0, 2)]]
Texture2D gDiffuse : register(t0);
[[vk::binding(0, 1)]]
SamplerState gSampler : register(s0);
struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex vertex = gVertices[vid];
    VSOutput output;
    output.position = mul(gMask.viewProj, mul(gInstances[gMask.instanceRow].model, float4(vertex.position, 1.0)));
    output.uv = mul(gMaterials[gInstances[gMask.instanceRow].materialRow].uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}
[shader("fragment")]
float fragmentMain(VSOutput input): SV_Target
{
    if ((gMaterials[gInstances[gMask.instanceRow].materialRow].flags & kMaterialMasked) != 0)
        applyAlphaMask(gDiffuse.Sample(gSampler, input.uv).a * gMaterials[gInstances[gMask.instanceRow].materialRow].albedo.a,
                       gMaterials[gInstances[gMask.instanceRow].materialRow].alphaCutoff);
    return gMask.selected;
}

// Scene visibility uses exactly the same rasterization and alpha coverage as selected geometry.
[shader("fragment")]
void depthMain(VSOutput input)
{
    if ((gMaterials[gInstances[gMask.instanceRow].materialRow].flags & kMaterialMasked) != 0)
        applyAlphaMask(gDiffuse.Sample(gSampler, input.uv).a * gMaterials[gInstances[gMask.instanceRow].materialRow].albedo.a,
                       gMaterials[gInstances[gMask.instanceRow].materialRow].alphaCutoff);
}
