import SceneTables;
// Depth-only shadow-map pass. Buffers 0 and 1 hold vertices and per-draw uniforms.
// The vertex layout matches ScenePass because both pull from the same buffer.

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
ConstantBuffer<DrawUniforms> gDraw : register(b1);
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

[shader("vertex")]
float4 vertexMain(uint vid: SV_VertexID): SV_Position
{
    return mul(gPass.lightViewProj, mul(gInstances[gDraw.instanceRow].model, float4(gVertices[vid].position, 1.0)));
}

[shader("fragment")]
void fragmentMain()
{
}
