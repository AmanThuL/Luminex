// Retains a real shader read of every active mip for the cross-frame terminal-use contract.
import RojoRHI;
import OcclusionRoots;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    HzbPublishRoot root = rojoRHI::root<HzbPublishRoot>();
    Texture2D<float> gPyramid = rojoRHI::texture2D<float>(root.pyramid);
    uint gLevelCount = root.levelCount;
    float sum = 0.0;
    for (uint level = 0; level < gLevelCount; ++level)
        sum += gPyramid.Load(int3(0, 0, level));
    rojoRHI::store<float>(root.publicationAddr, 0, sum);
}
