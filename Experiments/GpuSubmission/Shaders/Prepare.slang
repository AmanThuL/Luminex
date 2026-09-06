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

struct DrawIndirectArgs
{
    uint vertexCount, instanceCount, firstVertex, firstInstance;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> gParams : register(b0);
[[vk::binding(1, 0)]] StructuredBuffer<Instance> gInstances : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<uint> gVisibility : register(t2);
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawIndirectArgs> gArguments : register(u3);

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint id = threadId.x;
    if (id >= gParams.count)
        return;
    bool visible = true;
    if (gParams.suite == 0)
        visible = gVisibility[id] != 0;
    else
    {
        Instance instance = gInstances[id];
        for (uint p = 0; p < 6; ++p)
        {
            float distance = dot(gParams.planes[p].xyz, instance.center) + gParams.planes[p].w;
            if (distance < -instance.radius - gParams.epsilon)
                visible = false;
        }
    }
    DrawIndirectArgs arguments;
    arguments.vertexCount = 3 * gParams.triangles;
    arguments.instanceCount = visible ? 1 : 0;
    arguments.firstVertex = id * arguments.vertexCount;
    arguments.firstInstance = 0;
    // Every candidate is overwritten, including invisible objects on a recycled slot.
    gArguments[id] = arguments;
}
