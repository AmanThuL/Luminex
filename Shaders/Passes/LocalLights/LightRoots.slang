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
