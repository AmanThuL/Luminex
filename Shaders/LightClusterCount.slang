// Safe-math froxel light counting; one thread per froxel, explicit resources keep reflection exact.
import LightCluster;
import SceneTables;
[[vk::binding(0, 0)]] ConstantBuffer<LightClusterParams> gParams : register(b0);
[[vk::binding(1, 0)]] StructuredBuffer<LightRow> gLights : register(t1);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> gCounts : register(u2);
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint froxel = thread.x;
    if (froxel >= gParams.froxelCount) return;
    uint3 coordinate = froxelCoordinate(froxel);
    FroxelBounds bounds = froxelBounds(gParams, coordinate.x, coordinate.y, coordinate.z);
    uint hits = 0u;
    if (!bounds.empty)
    {
        for (uint row = 0u; row < gParams.rowCount; ++row)
        {
            LightRow light = gLights[row];
            // A row with no positive bound radius is a free slot: never tested, never counted.
            if (light.boundRadius <= 0.0f) continue;
            float3 centre = transformViewPoint(gParams, light.boundCentre);
            if (froxelIntersectsSphere(bounds, centre, light.boundRadius)) hits += 1u;
        }
    }
    // The raw intersection count: the single-threaded scan applies the cap, so the totals it
    // reconciles are summed in one deterministic order rather than raced together here.
    gCounts[froxel] = hits;
}
