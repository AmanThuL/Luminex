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

// The depth pass uploads only lightViewProj * model, not the scene pass's full object block.
struct ObjectUniforms
{
    float4x4 mvp;
};

[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUniforms> gObject : register(b1); // → MSL [[buffer(1)]]

[shader("vertex")]
float4 vertexMain(uint vid: SV_VertexID): SV_Position
{
    return mul(gObject.mvp, float4(gVertices[vid].position, 1.0));
}

[shader("fragment")]
void fragmentMain()
{
}
