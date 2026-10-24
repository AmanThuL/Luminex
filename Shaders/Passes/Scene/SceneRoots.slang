import RojoRHI;

struct ScenePassRoot
{
    rojoRHI::Addr vertices;
    rojoRHI::Addr visibleRows;
    rojoRHI::Addr instances;
    rojoRHI::Addr materials;
    rojoRHI::Addr passUniforms;
    rojoRHI::Addr exposure;
    rojoRHI::Addr lights;
    rojoRHI::Addr lightGrid;
    rojoRHI::Addr lightIndices;
    rojoRHI::Addr localLightParams;
    uint shadow;
    uint irradiance;
    uint prefilteredEnv;
    uint dfg;
    uint linearSampler;
    uint shadowSampler;
    uint iblSampler;
    uint padding;
};

struct SceneDrawRoot
{
    rojoRHI::Addr pass;
    uint firstEntry;
    uint baseColor;
    uint normal;
    uint metalRough;
    uint occlusion;
    uint emissive;
    uint indirect;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct SkyRoot
{
    rojoRHI::Addr vertices;
    rojoRHI::Addr uniforms;
    rojoRHI::Addr exposure;
    uint sky;
    uint sampler;
};
