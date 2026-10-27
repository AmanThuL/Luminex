import RojoRHI;

struct VisibilityClassifyRoot
{
    rojoRHI::Addr candidatesAddr;
    rojoRHI::Addr chunksAddr;
    rojoRHI::Addr instancesAddr;
    rojoRHI::Addr viewsAddr;
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr statesAddr;
    rojoRHI::Addr countersAddr;
    rojoRHI::Addr countsAddr;
};

struct VisibilityOcclusionRoot
{
    rojoRHI::Addr candidatesAddr;
    rojoRHI::Addr chunksAddr;
    rojoRHI::Addr instancesAddr;
    rojoRHI::Addr viewsAddr;
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr statesAddr;
    rojoRHI::Addr countersAddr;
    rojoRHI::Addr countsAddr;
    rojoRHI::Addr occlusionAddr;
    uint pyramid;
    uint padding;
};

struct VisibilityScanRoot
{
    rojoRHI::Addr runsAddr;
    rojoRHI::Addr chunksAddr;
    rojoRHI::Addr viewsAddr;
    rojoRHI::Addr countsAddr;
    rojoRHI::Addr offsetsAddr;
    uint padding0;
    uint padding1;
};

struct VisibilityEmitRoot
{
    rojoRHI::Addr candidatesAddr;
    rojoRHI::Addr runsAddr;
    rojoRHI::Addr chunksAddr;
    rojoRHI::Addr instancesAddr;
    rojoRHI::Addr meshesAddr;
    rojoRHI::Addr viewsAddr;
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr statesAddr;
    rojoRHI::Addr countersAddr;
    rojoRHI::Addr offsetsAddr;
    rojoRHI::Addr rowsAddr;
    rojoRHI::Addr argumentsAddr;
};

struct CandidateRecord { uint instanceRow; uint run; };
struct RunRecord { uint firstCandidate; uint candidateCount; uint firstSlot; uint argumentIndex; };
struct ChunkRecord { uint run; uint firstCandidate; uint candidateCount; uint padding; };
struct VisibilityViewParams
{
    float4 planes[5];
    uint flags; uint firstCandidate; uint candidateCount; uint firstRun;
    uint runCount; uint firstChunk; uint chunkCount; uint padding;
};
struct VisibilityParams
{
    uint layout; uint rowCapacity; uint argumentCapacity; uint stateCapacity;
    uint candidateCount; uint viewCount; uint2 padding;
};

struct OcclusionParams {
    float4 sourceRows[4];
    uint sourceWidth; uint sourceHeight; uint levelCount; uint flags;
    float nearGuard; float depthGuard; uint2 padding;
};
