// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import Visibility;
import SceneTables;
import VisibilityPrefix;
[[vk::binding(0, 0)]] StructuredBuffer<CandidateRecord> gCandidates : register(t0);
[[vk::binding(2, 0)]] StructuredBuffer<ChunkRecord> gChunks : register(t2);
[[vk::binding(3, 0)]] StructuredBuffer<InstanceRow> gInstances : register(t3);
[[vk::binding(5, 0)]] StructuredBuffer<VisibilityViewParams> gViews : register(t5);
[[vk::binding(6, 0)]] ConstantBuffer<VisibilityParams> gParams : register(b6);
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> gStates : register(u7);
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> gCounters : register(u8);
[[vk::binding(9, 0)]] RWStructuredBuffer<uint> gCounts : register(u9);
uint viewForCandidate(uint candidate)
{
    return candidate >= gViews[1].firstCandidate ? 1u : 0u;
}
void count(uint view, uint word, uint value)
{
    uint ignored;
    InterlockedAdd(gCounters[view * 20u + word], value, ignored);
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint totalChunks = gViews[1].firstChunk + gViews[1].chunkCount;
    if (group.x >= totalChunks) return;
    ChunkRecord chunk = gChunks[group.x];
    uint candidate = chunk.firstCandidate + lane;
    uint retained = 0u;
    if (lane < chunk.candidateCount)
    {
        uint view = viewForCandidate(candidate);
        uint state = classifyBounds(gInstances[gCandidates[candidate].instanceRow], gViews[view]);
        count(view, 0u, 1u);
        uint type = state & 3u;
        count(view, type == 0u ? 1u : type == 1u ? 2u : 2u + (state >> 2u), 1u);
        if (candidate < gParams.stateCapacity)
        {
            gStates[candidate] = state;
            retained = type != 1u ? 1u : 0u;
        }
        else if (type != 1u) count(view, 9u, 1u);
    }
    uint inclusive = scanPrefix(lane, retained);
    if (lane == 255u) gCounts[group.x] = inclusive;
}
