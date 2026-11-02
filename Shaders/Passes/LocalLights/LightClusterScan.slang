// Safe-math froxel prefix sum; one thread walks the 3,456 counts so ranges, truncation and every
// counter are a function of froxel order alone and no atomic decides the outcome.
import RojoRHI;
import LightRoots;
import LightCluster;
import LocalLightTypes;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    let root = rojoRHI::root<LightClusterRoot>();
    let params = rojoRHI::load<LightClusterParams>(root.params, 0);
    uint running = 0u;
    uint candidates = 0u;
    uint droppedPerCluster = 0u;
    uint droppedGlobal = 0u;
    uint truncatedFroxels = 0u;
    uint maxCount = 0u;
    for (uint froxel = 0u; froxel < params.froxelCount; ++froxel)
    {
        uint hits = rojoRHI::load<uint>(root.counts, froxel);
        uint kept = hits < params.perClusterCap ? hits : params.perClusterCap;
        uint truncated = hits > kept ? 1u : 0u;
        candidates += hits;
        droppedPerCluster += hits - kept;
        maxCount = kept > maxCount ? kept : maxCount;
        // Ranges are granted in flat froxel order: the first froxel that does not fit takes the
        // remainder and every later froxel with intersections is truncated to nothing.
        uint remaining = params.globalCapacity - running;
        uint granted = kept < remaining ? kept : remaining;
        if (granted < kept)
        {
            truncated = 1u;
            droppedGlobal += kept - granted;
        }
        ClusterRecord record;
        record.offset = running;
        record.count = granted | (truncated != 0u ? kClusterTruncatedBit : 0u);
        rojoRHI::store<ClusterRecord>(root.grid, froxel, record);
        truncatedFroxels += truncated;
        running += granted;
    }
    rojoRHI::store<uint>(root.counters, 0, candidates);
    rojoRHI::store<uint>(root.counters, 1, running);
    rojoRHI::store<uint>(root.counters, 2, droppedPerCluster);
    rojoRHI::store<uint>(root.counters, 3, droppedGlobal);
    rojoRHI::store<uint>(root.counters, 4, truncatedFroxels);
    rojoRHI::store<uint>(root.counters, 5, maxCount);
}
