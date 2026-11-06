import RojoRHI;

struct LightClusterRoot
{
    rojoRHI::Addr params;
    rojoRHI::Addr lights;
    rojoRHI::Addr counts;
    rojoRHI::Addr grid;
    rojoRHI::Addr indices;
    rojoRHI::Addr counters;
};

struct LightDebugRoot
{
    rojoRHI::Addr params;
    rojoRHI::Addr lights;
    rojoRHI::Addr grid;
    rojoRHI::Addr indices;
    uint depth;
    uint display;
    uint padding0;
    uint padding1;
};

struct DebugParams
{
    float4x4 inverseViewProjection;
    uint mode;
    uint rowCount;
    uint activeWidth;
    uint activeHeight;
    uint outputWidth;
    uint outputHeight;
    float sliceDepth[25];
};

// `DebugParams.sliceDepth`'s offset in 4-byte words (LightClusterFrameData.h asserts byte 88), for
// LocalLightCluster's in-place read of the table.
static const uint kDebugParamsSliceDepthWord = 22u;
