struct ReadbackParams
{
    uint width;
    uint height;
    uint level;
    uint offset;
};
[[vk::binding(0, 0)]]
Texture2D<float> gSource : register(t0);
[[vk::binding(0, 0)]]
ConstantBuffer<ReadbackParams> gParams : register(b0);
[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gValues : register(u1);
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gParams.width || tid.y >= gParams.height)
        return;
    gValues[gParams.offset + tid.y * gParams.width + tid.x] =
        gSource.Load(int3(tid.xy, gParams.level));
}
