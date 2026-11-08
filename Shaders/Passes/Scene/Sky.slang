// Camera-centered far-plane sky; exposure source is selected by the compiled twin.
import RojoRHI;
import SceneRoots;
import VertexStorage;
import Motion;

struct VSOutput
{
    float4 position : SV_Position;
    // Origin-centered sphere positions are cubemap directions.
    float3 direction : TEXCOORD0;
};

VSOutput skyVertex(uint vid, float2 jitterNdc)
{
    SkyRoot root = rojoRHI::root<SkyRoot>();
    SkyUniforms gSky = rojoRHI::load<SkyUniforms>(root.uniforms, 0);
    float3 local = loadVertex(root.vertices, vid).position;
    VSOutput o;
    float4 clip = mul(gSky.viewProj, float4(local + gSky.eyePos, 1.0));
    // Pin every sky vertex to the far plane, which reversed depth puts at 0: z / w = 0 whatever
    // the sphere's own geometry says, so the sky rasterises at exactly the value the pass cleared
    // to and its GreaterEqual test lets it through only where no geometry has written. (The
    // conventional-depth form of this trick was clip.xyww, pinning z / w to 1.)
    o.position = float4(clip.xy + jitterNdc * clip.w, 0.0, clip.w);
    o.direction = local;
    return o;
}

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    return skyVertex(rojoRHI::vertexIndex(ids), float2(0.0, 0.0));
}

float3 skyRadiance(VSOutput input)
{
    SkyRoot root = rojoRHI::root<SkyRoot>();
    SkyUniforms gSky = rojoRHI::load<SkyUniforms>(root.uniforms, 0);
    // The sampled result is scene-linear: sRGB views decode automatically, while float views
    // already store linear radiance. It stays linear all the way to the display transform.
    float3 color = rojoRHI::textureCube(root.sky).Sample(rojoRHI::sampler(root.sampler), normalize(input.direction)).rgb;
    return color * gSky.preExposure;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    return float4(skyRadiance(input), 1.0);
}

// The sky's motion variant. The sphere is drawn at the previous eye for the previous frame's
// matrices, so what survives is the camera's rotation and nothing else -- the only motion a
// surface at infinity has.
struct SkyMotionVSOutput
{
    VSOutput base;
    float4 clipCurrent : TEXCOORD1;
    float4 clipPrevious : TEXCOORD2;
};

struct SkyMotionFSOutput
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    // The sky never accumulates less readily than the geometry in front of it: it emits no
    // radiance of its own that can switch on, so its reactive weight is the constant 0.
    float reactive : SV_Target2;
};

[shader("vertex")]
SkyMotionVSOutput vertexMainMotion(rojoRHI::VertexIds ids)
{
    SkyRoot root = rojoRHI::root<SkyRoot>();
    SkyUniforms gSky = rojoRHI::load<SkyUniforms>(root.uniforms, 0);
    uint vid = rojoRHI::vertexIndex(ids);
    float3 local = loadVertex(root.vertices, vid).position;
    SkyMotionVSOutput o;
    o.base = skyVertex(vid, float2(gSky.jitterNdcX, gSky.jitterNdcY));
    o.clipCurrent = mul(gSky.viewProj, float4(local + gSky.eyePos, 1.0));
    o.clipPrevious = mul(gSky.previousViewProj, float4(local + gSky.previousEyePos, 1.0));
    return o;
}

[shader("fragment")]
SkyMotionFSOutput fragmentMainMotion(SkyMotionVSOutput input)
{
    SkyMotionFSOutput o;
    o.color = float4(skyRadiance(input.base), 1.0);
    o.motion = motionFromClip(input.clipCurrent, input.clipPrevious);
    o.reactive = 0.0;
    return o;
}
