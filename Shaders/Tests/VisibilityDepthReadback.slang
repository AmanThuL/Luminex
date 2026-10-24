// Test-only bit-preserving readback of the camera's private D32Float attachment.
import RojoRHI;
import OracleRoots;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    let params = rojoRHI::root<VisibilityDepthRoot>();
    let depth = rojoRHI::texture2D<float>(params.depth);
    uint width, height;
    depth.GetDimensions(width, height);
    if (tid.x < width && tid.y < height)
        rojoRHI::store<uint>(params.output, tid.y * width + tid.x,
                            asuint(depth.Load(int3(tid.xy, 0))));
}
