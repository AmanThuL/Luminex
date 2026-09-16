// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import Visibility;
import SceneTables;
import VisibilityPrefix;
[[vk::binding(1, 0)]] StructuredBuffer<RunRecord> gRuns : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<ChunkRecord> gChunks : register(t2);
[[vk::binding(5, 0)]] StructuredBuffer<VisibilityViewParams> gViews : register(t5);
[[vk::binding(9, 0)]] RWStructuredBuffer<uint> gCounts : register(u9);
[[vk::binding(10, 0)]] RWStructuredBuffer<uint> gOffsets : register(u10);
groupshared uint carry;
uint viewForCandidate(uint candidate)
{
    return candidate >= gViews[1].firstCandidate ? 1u : 0u;
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint totalRuns = gViews[1].firstRun + gViews[1].runCount;
    if (group.x >= totalRuns) return;
    RunRecord run = gRuns[group.x];
    uint view = viewForCandidate(run.firstCandidate);
    VisibilityViewParams vp = gViews[view];
    // Binary search finds the first run chunk without a second CPU ABI table.
    uint lo = vp.firstChunk;
    uint hi = lo + vp.chunkCount;
    while (lo < hi)
    {
        uint mid = lo + (hi - lo) / 2u;
        if (gChunks[mid].run < group.x) lo = mid + 1u;
        else hi = mid;
    }
    uint chunkCount = (run.candidateCount + 255u) / 256u;
    if (lane == 0u) carry = 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint batch = 0u; batch < chunkCount; batch += 256u)
    {
        uint relative = batch + lane;
        uint value = relative < chunkCount ? gCounts[lo + relative] : 0u;
        uint before = carry;
        uint inclusive = scanPrefix(lane, value);
        if (relative < chunkCount) gOffsets[lo + relative] = before + inclusive - value;
        GroupMemoryBarrierWithGroupSync();
        if (lane == 255u) carry = before + inclusive;
        GroupMemoryBarrierWithGroupSync();
    }
}
