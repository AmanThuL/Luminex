// Safe-math froxel prefix sum; one thread walks the 3,456 counts so ranges, truncation and every
// counter are a function of froxel order alone and no atomic decides the outcome.
import LightCluster;
[[vk::binding(0, 0)]] ConstantBuffer<LightClusterParams> gParams : register(b0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> gCounts : register(u2);
[[vk::binding(3, 0)]] RWStructuredBuffer<ClusterRecord> gGrid : register(u3);
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> gCounters : register(u5);
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint running = 0u;
    uint candidates = 0u;
    uint droppedPerCluster = 0u;
    uint droppedGlobal = 0u;
    uint truncatedFroxels = 0u;
    uint maxCount = 0u;
    for (uint froxel = 0u; froxel < gParams.froxelCount; ++froxel)
    {
        uint hits = gCounts[froxel];
        uint kept = hits < gParams.perClusterCap ? hits : gParams.perClusterCap;
        uint truncated = hits > kept ? 1u : 0u;
        candidates += hits;
        droppedPerCluster += hits - kept;
        maxCount = kept > maxCount ? kept : maxCount;
        // Ranges are granted in flat froxel order: the first froxel that does not fit takes the
        // remainder and every later froxel with intersections is truncated to nothing.
        uint remaining = gParams.globalCapacity - running;
        uint granted = kept < remaining ? kept : remaining;
        if (granted < kept)
        {
            truncated = 1u;
            droppedGlobal += kept - granted;
        }
        ClusterRecord record;
        record.offset = running;
        record.count = granted | (truncated != 0u ? kClusterTruncatedBit : 0u);
        gGrid[froxel] = record;
        truncatedFroxels += truncated;
        running += granted;
    }
    gCounters[0] = candidates;
    gCounters[1] = running;
    gCounters[2] = droppedPerCluster;
    gCounters[3] = droppedGlobal;
    gCounters[4] = truncatedFroxels;
    gCounters[5] = maxCount;
}
