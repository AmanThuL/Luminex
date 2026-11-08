import RojoRHI;
import Lighting;

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

struct PassUniforms
{
    float4x4 viewProj;
    float4x4 shadowTransform;
    float3 eyePos;
    float eyePadding;
    float time;
    float preExposure;
    float alignmentPadding[2];
    DirLight lights[3];
    int shadowFilter;
    int shadowCaster;
    int tailPadding[2];
    float4x4 viewProjUnjittered;
    float4x4 previousViewProjUnjittered;
};

struct SkyUniforms
{
    float4x4 viewProj;
    float3 eyePos;
    float eyePadding;
    float preExposure;
    float jitterNdcX;
    float jitterNdcY;
    float jitterPadding;
    float4x4 previousViewProj;
    float3 previousEyePos;
    float previousEyePosPadding;
};
