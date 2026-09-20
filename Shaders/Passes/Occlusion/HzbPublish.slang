// Retains a real shader read of every active mip for the cross-frame terminal-use contract.
[[vk::binding(0, 0)]]
Texture2D<float> gPyramid : register(t0);
[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gPublication : register(u1);
[[vk::binding(0, 0)]]
ConstantBuffer<uint> gLevelCount : register(b0);

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    float sum = 0.0;
    for (uint level = 0; level < gLevelCount; ++level)
        sum += gPyramid.Load(int3(0, 0, level));
    gPublication[0] = sum;
}
