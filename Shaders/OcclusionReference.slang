// Independent candidate selector and byte ID output, sharing only scene transforms and alpha coverage.
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
struct ReferenceParams
{
    float4x4 viewProjection;
    uint instanceRow;
    uint padding0;
    uint padding1;
    uint padding2;
};
[[vk::binding(1, 0)]]
ConstantBuffer<ReferenceParams> gReference : register(b1);
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
VSOutput vertexMain(uint vid : SV_VertexID)
{
    Vertex vertex = gVertices[vid];
    InstanceRow instance = gInstances[gReference.instanceRow];
    MaterialRow material = gMaterials[instance.materialRow];
    float4 positionW = mul(instance.model, float4(vertex.position, 1.0));
    VSOutput output;
    output.position = mul(gReference.viewProjection, positionW);
    output.uv = mul(material.uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}
float4 byteId()
{
    uint id = gReference.instanceRow + 1;
    return float4(id & 255, (id >> 8) & 255, (id >> 16) & 255, (id >> 24) & 255) / 255.0;
}
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return byteId();
}
[shader("fragment")]
float4 fragmentMask(VSOutput input) : SV_Target
{
    MaterialRow material = gMaterials[gInstances[gReference.instanceRow].materialRow];
    applyAlphaMask(gDiffuse.Sample(gSampler, input.uv).a * material.albedo.a,
                   material.alphaCutoff);
    return byteId();
}
