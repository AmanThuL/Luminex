// Safe-math froxel list fill; repeating the count kernel's test in ascending row order is what
// keeps a truncated froxel's kept set the lowest of its candidates without any cross-pass state.
import LightCluster;
import SceneTables;
[[vk::binding(0, 0)]] ConstantBuffer<LightClusterParams> gParams : register(b0);
[[vk::binding(1, 0)]] StructuredBuffer<LightRow> gLights : register(t1);
[[vk::binding(3, 0)]] RWStructuredBuffer<ClusterRecord> gGrid : register(u3);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> gIndices : register(u4);
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint froxel = thread.x;
    if (froxel >= gParams.froxelCount) return;
    ClusterRecord record = gGrid[froxel];
    uint granted = record.count & ~kClusterTruncatedBit;
    if (granted == 0u) return;
    uint3 coordinate = froxelCoordinate(froxel);
    FroxelBounds bounds = froxelBounds(gParams, coordinate.x, coordinate.y, coordinate.z);
    if (bounds.empty) return;
    uint written = 0u;
    for (uint row = 0u; row < gParams.rowCount && written < granted; ++row)
    {
        LightRow light = gLights[row];
        if (light.boundRadius <= 0.0f) continue;
        float3 centre = transformViewPoint(gParams, light.boundCentre);
        if (!froxelIntersectsSphere(bounds, centre, light.boundRadius)) continue;
        // The scan granted exactly `granted` entries at `record.offset`, so no write leaves it.
        gIndices[record.offset + written] = row;
        written += 1u;
    }
}
