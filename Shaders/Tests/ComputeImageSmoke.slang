// Test-only compute oracle for storage textures: one kernel writes a gradient, the other reads a
// storage texture and writes its complement, so a stale read shows up as the original gradient
// rather than as noise.
import RojoRHI;
import OracleRoots;

// Every channel takes a separate arithmetic path, so a swizzled or half-written texel is visible
// in the readback.
[shader("compute")]
[numthreads(8, 8, 1)]
void computeWriteImage(uint3 tid: SV_DispatchThreadID)
{
    let params = rojoRHI::root<ComputeImageRoot>();
    let image = rojoRHI::rwTexture2D<float4>(params.image);
    float scale = 1.0 / float(params.extent - 1u);
    image[tid.xy] = float4(float(tid.x) * scale, float(tid.y) * scale, 0.25, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeInvertImage(uint3 tid: SV_DispatchThreadID)
{
    let params = rojoRHI::root<ComputeImageRoot>();
    let image = rojoRHI::rwTexture2D<float4>(params.image);
    let storageSource = rojoRHI::rwTexture2D<float4>(params.source);
    float4 source = storageSource[tid.xy];
    image[tid.xy] = float4(1.0 - source.r, 1.0 - source.g, 1.0 - source.b, 1.0);
}
