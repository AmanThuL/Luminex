import RojoRHI;

struct HzbDepthRoot
{
    uint masked;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct HzbReadbackRoot
{
    rojoRHI::Addr paramsAddr;
    rojoRHI::Addr outputAddr;
    uint source;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct OcclusionProbeRoot
{
    rojoRHI::Addr boundsAddr;
    rojoRHI::Addr outputAddr;
    rojoRHI::Addr paramsAddr;
    uint pyramid;
    uint padding;
};

struct ReadbackParams
{
    uint width;
    uint height;
    uint level;
    uint offset;
};

struct ProbeBounds
{
    float4 minimum;
    float4 maximum;
};
