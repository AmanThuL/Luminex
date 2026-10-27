import RojoRHI;

struct HzbReduceRoot
{
    rojoRHI::Addr paramsAddr;
    uint source;
    uint destination;
};

struct HzbPublishRoot
{
    rojoRHI::Addr publicationAddr;
    uint pyramid;
    uint levelCount;
};

struct HzbDebugRoot
{
    rojoRHI::Addr paramsAddr;
    uint pyramid;
    uint padding;
};

struct OcclusionReferenceRoot
{
    rojoRHI::Addr verticesAddr;
    rojoRHI::Addr instancesAddr;
    rojoRHI::Addr materialsAddr;
    rojoRHI::Addr paramsAddr;
    uint diffuse;
    uint sampler;
    uint padding0;
    uint padding1;
};

struct HzbReduceParams
{
    uint sourceWidth;
    uint sourceHeight;
    uint destinationWidth;
    uint destinationHeight;
    uint sourceLevel;
};

struct HzbDebugViewParams
{
    uint level;
    uint activeWidth;
    uint activeHeight;
    uint padding;
    uint outputWidth;
    uint outputHeight;
    uint padding2;
    uint padding3;
};

struct ReferenceParams
{
    float4x4 viewProjection;
    uint instanceRow;
    uint padding0;
    uint padding1;
    uint padding2;
};
