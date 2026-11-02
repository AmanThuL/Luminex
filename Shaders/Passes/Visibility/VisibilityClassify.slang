// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import RojoRHI;
import VisibilityRoots;
import Visibility;
import SceneTables;
import VisibilityPrefix;
uint viewForCandidate(uint candidate)
{
    let root = rojoRHI::root<VisibilityClassifyRoot>();
    return candidate >= (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstCandidate ? 1u : 0u;
}
void count(uint view, uint word, uint value)
{
    let root = rojoRHI::root<VisibilityClassifyRoot>();
    rojoRHI::atomicAdd(root.countersAddr, view * kVisibilityCounterWords + word, value);
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    let root = rojoRHI::root<VisibilityClassifyRoot>();
    let params = rojoRHI::load<VisibilityParams>(root.paramsAddr, 0);
    uint totalChunks = (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstChunk + (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).chunkCount;
    if (group.x >= totalChunks) return;
    ChunkRecord chunk = (rojoRHI::load<ChunkRecord>(root.chunksAddr, group.x));
    uint candidate = chunk.firstCandidate + lane;
    uint retained = 0u;
    if (lane < chunk.candidateCount)
    {
        uint view = viewForCandidate(candidate);
        uint state = classifyBounds((rojoRHI::load<InstanceRow>(root.instancesAddr, (rojoRHI::load<CandidateRecord>(root.candidatesAddr, candidate)).instanceRow)), (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, view)));
        uint type = state & 3u;
        uint reason = (state >> 2u) & 63u;
        if (reason == 6u) count(view, 17u, 1u);
        else
        {
            count(view, 0u, 1u);
            count(view, type == 0u ? 1u : type == 1u ? 2u : 2u + reason, 1u);
        }
        if (candidate < params.stateCapacity)
        {
            rojoRHI::store<uint>(root.statesAddr, candidate, state);
            retained = type != 1u ? 1u : 0u;
        }
        else if (type != 1u) count(view, 9u, 1u);
    }
    uint inclusive = scanPrefix(lane, retained);
    if (lane == 255u) rojoRHI::store<uint>(root.countsAddr, group.x, inclusive);
}
