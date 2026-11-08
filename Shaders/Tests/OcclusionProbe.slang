// Emits every projection field for exact comparison with the ordered CPU oracle.
import RojoRHI;
import VisibilityOracleRoots;
import VisibilityRoots;
import Occlusion;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    OcclusionProbeRoot root = rojoRHI::root<OcclusionProbeRoot>();
    ProbeBounds bounds = rojoRHI::load<ProbeBounds>(root.boundsAddr, id.x);
    OcclusionParams gParams = rojoRHI::load<OcclusionParams>(root.paramsAddr, 0);
    Texture2D<float> gPyramid = rojoRHI::texture2D<float>(root.pyramid);
    OcclusionProjection result = testOcclusionBounds(bounds.minimum.xyz, bounds.maximum.xyz,
                                                       gParams, gPyramid);
    uint base = id.x * 8;
    rojoRHI::store<uint>(root.outputAddr, base, asuint(result.rectangle.x));
    rojoRHI::store<uint>(root.outputAddr, base + 1, asuint(result.rectangle.y));
    rojoRHI::store<uint>(root.outputAddr, base + 2, asuint(result.rectangle.z));
    rojoRHI::store<uint>(root.outputAddr, base + 3, asuint(result.rectangle.w));
    rojoRHI::store<uint>(root.outputAddr, base + 4, result.level);
    rojoRHI::store<uint>(root.outputAddr, base + 5, asuint(result.zBox));
    rojoRHI::store<uint>(root.outputAddr, base + 6, result.outcome);
    rojoRHI::store<uint>(root.outputAddr, base + 7, result.occluded);
}
