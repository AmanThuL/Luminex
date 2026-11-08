import RojoRHI;

struct SceneTableAbiRoot
{
    rojoRHI::Addr output;
    rojoRHI::Addr instances;
    rojoRHI::Addr materials;
    rojoRHI::Addr meshes;
    rojoRHI::Addr lights;
    uint padding0;
    uint padding1;
};

struct ShadowSmokeRoot
{
    rojoRHI::Addr vertices;
    rojoRHI::Addr uniforms;
    uint shadow;
    uint sampler;
    uint padding0;
    uint padding1;
};

struct ShadowSmokeUniforms
{
    float4x4 lightViewProj;
    float4x4 shadowTransform;
    int filter;
    int padding0;
    int padding1;
    int padding2;
};

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
