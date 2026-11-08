import RojoRHI;
import SceneTables;

struct PunctualLightRoot
{
    rojoRHI::Addr samples;
    rojoRHI::Addr output;
};

struct PunctualSample
{
    LightRow light;
    float3 position;
    float pad0;
    float3 normal;
    float pad1;
    float3 toEye;
    float pad2;
    float3 baseColour;
    float metallic;
    float3 f0;
    float alpha;
};
