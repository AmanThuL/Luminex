import RojoRHI;

struct BufferHazardRoot
{
    rojoRHI::Addr output;
    rojoRHI::Addr source;
    uint bias;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct ComputeImageRoot
{
    uint image;
    uint source;
    uint extent;
    uint padding;
};

struct FullscreenSampleRoot
{
    uint source;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct SamplerRoot
{
    uint source;
    uint sampler;
    uint padding0;
    uint padding1;
};

struct VisibilityDepthRoot
{
    rojoRHI::Addr output;
    uint depth;
    uint padding;
};
