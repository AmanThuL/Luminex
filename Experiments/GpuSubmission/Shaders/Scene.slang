struct Params
{
    float4 viewProjection[4];
    float4 planes[6];
    uint count, triangles, bins, suite;
    uint batched, binOffset, debug;
    float epsilon;
};

struct Instance
{
    float3 center;
    float halfExtent;
    float radius;
    uint bin, padding0, padding1;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> gParams : register(b0);
[[vk::binding(1, 0)]] StructuredBuffer<Instance> gInstances : register(t1);
[[vk::binding(4, 0)]] StructuredBuffer<uint> gVisibleIds : register(t4);
[[vk::binding(5, 0)]] StructuredBuffer<float4> gColors : register(t5);

struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    uint vertexCount = 3 * gParams.triangles;
    uint objectId = gParams.batched != 0
        ? gVisibleIds[gParams.binOffset + instanceId] : vertexId / vertexCount;
    uint localVertex = gParams.batched != 0 ? vertexId : vertexId % vertexCount;
    uint side = gParams.triangles == 32 ? 4 : 1;
    uint cell = localVertex / 6;
    uint corner = localVertex % 6;
    // Two counterclockwise triangles share the diagonal of each grid cell.
    uint2 offset = corner == 0 ? uint2(0, 0) :
                   corner == 1 ? uint2(1, 0) :
                   corner == 2 ? uint2(1, 1) :
                   corner == 3 ? uint2(0, 0) :
                   corner == 4 ? uint2(1, 1) : uint2(0, 1);
    float2 local = (float2(uint2(cell % side, cell / side) + offset) / float(side)) * 2 - 1;
    Instance instance = gInstances[objectId];
    float4 world = float4(instance.center.xy + local * instance.halfExtent, instance.center.z, 1);
    VertexOutput output;
    // Explicit columns preserve the host's column-major ABI independently of Slang defaults.
    output.position = gParams.viewProjection[0] * world.x +
                      gParams.viewProjection[1] * world.y +
                      gParams.viewProjection[2] * world.z + gParams.viewProjection[3];
    output.color = gColors[instance.bin];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color;
}
