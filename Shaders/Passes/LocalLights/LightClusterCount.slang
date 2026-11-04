// Safe-math froxel light counting; one address-root thread per froxel.
import RojoRHI;
import LightRoots;
import LightCluster;
import SceneTables;
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    let root = rojoRHI::root<LightClusterRoot>();
    let params = rojoRHI::load<LightClusterParams>(root.params, 0);
    uint froxel = thread.x;
    if (froxel >= params.froxelCount) return;
    uint3 coordinate = froxelCoordinate(froxel);
    FroxelBounds bounds = froxelBounds(params, coordinate.x, coordinate.y, coordinate.z);
    uint hits = 0u;
    if (!bounds.empty)
    {
        for (uint row = 0u; row < params.rowCount; ++row)
        {
            LightRow light = rojoRHI::load<LightRow>(root.lights, row);
            // A row with no positive bound radius is a free slot: never tested, never counted.
            if (light.boundRadius <= 0.0f) continue;
            float3 centre = transformViewPoint(params, light.boundCentre);
            if (froxelIntersectsSphere(bounds, centre, light.boundRadius)) hits += 1u;
        }
    }
    // The raw intersection count: the single-threaded scan applies the cap, so the totals it
    // reconciles are summed in one deterministic order rather than raced together here.
    rojoRHI::store<uint>(root.counts, froxel, hits);
}
