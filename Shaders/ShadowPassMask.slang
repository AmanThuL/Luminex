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

struct ShadowMaskObjectUniforms
{
    float4x4 mvp;
    float4x4 uvTransform;
    float albedoAlpha;
};

[[vk::binding(1, 0)]]
ConstantBuffer<ShadowMaskObjectUniforms> gObject : register(b1);
[[vk::binding(0, 2)]]
Texture2D gDiffuse : register(t0);
[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0);

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
    output.position = mul(gObject.mvp, float4(vertex.position, 1.0));
    output.uv = mul(gObject.uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput input)
{
    applyAlphaMask(gDiffuse.Sample(gLinearSampler, input.uv).a * gObject.albedoAlpha);
}
