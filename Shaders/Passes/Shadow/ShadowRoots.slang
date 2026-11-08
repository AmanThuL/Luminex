import RojoRHI;

struct ShadowPassUniforms
{
    float4x4 lightViewProj;
};

struct ShadowPassRoot
{
    rojoRHI::Addr vertices;
    rojoRHI::Addr visibleRows;
    rojoRHI::Addr instances;
    rojoRHI::Addr materials;
    rojoRHI::Addr uniforms;
    uint linearSampler;
    uint padding;
};

struct ShadowDrawRoot
{
    rojoRHI::Addr pass;
    uint firstEntry;
    uint baseColor;
    uint indirect;
    uint padding0;
    uint padding1;
    uint padding2;
};
