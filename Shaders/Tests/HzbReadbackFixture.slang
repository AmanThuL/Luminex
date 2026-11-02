import RojoRHI;
import VisibilityOracleRoots;
[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    HzbReadbackRoot root = rojoRHI::root<HzbReadbackRoot>();
    ReadbackParams gParams = rojoRHI::load<ReadbackParams>(root.paramsAddr, 0);
    Texture2D<float> gSource = rojoRHI::texture2D<float>(root.source);
    if (tid.x >= gParams.width || tid.y >= gParams.height)
        return;
    rojoRHI::store<float>(root.outputAddr, gParams.offset + tid.y * gParams.width + tid.x,
                           gSource.Load(int3(tid.xy, gParams.level)));
}
