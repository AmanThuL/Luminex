// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import RojoRHI;
import VisibilityRoots;
import Visibility;
import SceneTables;
import VisibilityPrefix;
groupshared uint carry;
uint viewForCandidate(uint candidate)
{
    let root = rojoRHI::root<VisibilityScanRoot>();
    return candidate >= (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstCandidate ? 1u : 0u;
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    let root = rojoRHI::root<VisibilityScanRoot>();
    uint totalRuns = (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstRun + (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).runCount;
    if (group.x >= totalRuns) return;
    RunRecord run = (rojoRHI::load<RunRecord>(root.runsAddr, group.x));
    uint view = viewForCandidate(run.firstCandidate);
    VisibilityViewParams vp = (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, view));
    // Binary search finds the first run chunk without a second CPU ABI table.
    uint lo = vp.firstChunk;
    uint hi = lo + vp.chunkCount;
    while (lo < hi)
    {
        uint mid = lo + (hi - lo) / 2u;
        if ((rojoRHI::load<ChunkRecord>(root.chunksAddr, mid)).run < group.x) lo = mid + 1u;
        else hi = mid;
    }
    uint chunkCount = (run.candidateCount + 255u) / 256u;
    if (lane == 0u) carry = 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint batch = 0u; batch < chunkCount; batch += 256u)
    {
        uint relative = batch + lane;
        uint value = relative < chunkCount ? (rojoRHI::load<uint>(root.countsAddr, lo + relative)) : 0u;
        uint before = carry;
        uint inclusive = scanPrefix(lane, value);
        if (relative < chunkCount) rojoRHI::store<uint>(root.offsetsAddr, lo + relative, before + inclusive - value);
        GroupMemoryBarrierWithGroupSync();
        if (lane == 255u) carry = before + inclusive;
        GroupMemoryBarrierWithGroupSync();
    }
}
