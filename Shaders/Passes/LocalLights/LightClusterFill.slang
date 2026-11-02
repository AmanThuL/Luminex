// Safe-math froxel list fill; repeating the count kernel's test in ascending row order is what
// keeps a truncated froxel's kept set the lowest of its candidates without any cross-pass state.
import RojoRHI;
import LightRoots;
import LightCluster;
import LocalLightTypes;
import SceneTables;
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    let root = rojoRHI::root<LightClusterRoot>();
    let params = rojoRHI::load<LightClusterParams>(root.params, 0);
    uint froxel = thread.x;
    if (froxel >= params.froxelCount) return;
    ClusterRecord record = rojoRHI::load<ClusterRecord>(root.grid, froxel);
    uint granted = record.count & ~kClusterTruncatedBit;
    if (granted == 0u) return;
    uint3 coordinate = froxelCoordinate(froxel);
    FroxelBounds bounds = froxelBounds(params, coordinate.x, coordinate.y, coordinate.z);
    if (bounds.empty) return;
    uint written = 0u;
    for (uint row = 0u; row < params.rowCount && written < granted; ++row)
    {
        LightRow light = rojoRHI::load<LightRow>(root.lights, row);
        if (light.boundRadius <= 0.0f) continue;
        float3 centre = transformViewPoint(params, light.boundCentre);
        if (!froxelIntersectsSphere(bounds, centre, light.boundRadius)) continue;
        // The scan granted exactly `granted` entries at `record.offset`, so no write leaves it.
        rojoRHI::store<uint>(root.indices, record.offset + written, row);
        written += 1u;
    }
}
