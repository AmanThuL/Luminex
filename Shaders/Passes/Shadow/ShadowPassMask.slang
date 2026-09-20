import SceneTables;
// Alpha-tested shadow variant; uses the scene pass's transformed UV, sampler and coverage rule.
import AlphaMask;

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0);

[[vk::binding(1, 0)]]
ConstantBuffer<DrawUniforms> gDraw : register(b1);
[[vk::binding(4, 0)]]
StructuredBuffer<uint> gVisibleRows : register(t4);
[[vk::binding(5, 0)]]
StructuredBuffer<InstanceRow> gInstances : register(t5);
[[vk::binding(6, 0)]]
StructuredBuffer<MaterialRow> gMaterials : register(t6);

struct ShadowPassUniforms
{
    float4x4 lightViewProj;
};
[[vk::binding(2, 0)]]
ConstantBuffer<ShadowPassUniforms> gPass : register(b2);

[[vk::binding(0, 2)]]
Texture2D gDiffuse : register(t0);
[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0);

struct VSOutput
{
    nointerpolation uint instanceRow : TEXCOORD1;
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    Vertex vertex = gVertices[vid];
    VSOutput output;
    output.instanceRow = gVisibleRows[gDraw.firstEntry + instanceIndex];
    output.position = mul(gPass.lightViewProj, mul(gInstances[output.instanceRow].model, float4(vertex.position, 1.0)));
    output.uv = mul(gMaterials[gInstances[output.instanceRow].materialRow].uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput input)
{
    applyAlphaMask(gDiffuse.Sample(gLinearSampler, input.uv).a * gMaterials[gInstances[input.instanceRow].materialRow].albedo.a,
                   gMaterials[gInstances[input.instanceRow].materialRow].alphaCutoff);
}
