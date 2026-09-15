// Test-only bit-preserving readback of the camera's private D32Float attachment.
[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> gOutput : register(u0);

[[vk::binding(1, 0)]]
Texture2D<float> gDepth : register(t1);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint width, height;
    gDepth.GetDimensions(width, height);
    if (tid.x < width && tid.y < height)
        gOutput[tid.y * width + tid.x] = asuint(gDepth.Load(int3(tid.xy, 0)));
}
