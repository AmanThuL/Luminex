// M5.1 Stage 4 hazard-matrix oracle (spec section 7), maintained-RHI side: relays a texture's
// RGBA8-packed content into a linear buffer -- the compute-side "reader" half of every hazard case
// whose consumer (or WAR/WAW producer) role is a compute read. The packed word is what the CPU
// readback oracle compares against hazardExpectedTexel's byte-for-byte expectation.

struct ReadImageParams
{
    uint extent;
};

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> gOut : register(u0); // -> MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gSrc : register(t0); // -> MSL [[texture(0)]]

[[vk::binding(0, 1)]]
ConstantBuffer<ReadImageParams> gParams : register(b1); // -> MSL [[buffer(1)]]

[shader("compute")]
[numthreads(8, 8, 1)]
void computeReadImageToBuffer(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.extent || tid.y >= gParams.extent)
    {
        return;
    }
    float4 texel = gSrc.Load(int3(tid.xy, 0));
    uint4 texelBytes = uint4(round(saturate(texel) * 255.0));
    uint packed = texelBytes.r | (texelBytes.g << 8) | (texelBytes.b << 16) | (texelBytes.a << 24);
    gOut[tid.y * gParams.extent + tid.x] = packed;
}
