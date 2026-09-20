// The auto-exposure twin of Sky.slang (spec 9): byte-for-byte identical except the final multiply
// reads the persistent exposure buffer instead of SkyUniforms.preExposure. See
// ScenePassAuto.slang's header for why this is a separate compiled pipeline rather than a runtime
// branch in one shader -- keep every other edit mirrored between the two files.
//
// Camera-centered sky sphere pinned to the far plane. The pipeline uses GreaterEqual depth and no
// culling because the camera views the sphere from inside. Slots: buffers 0/2/3, texture 2, sampler 0.

import Motion;

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0); // → MSL [[buffer(0)]]

// The dedicated 176-byte CPU mirror pads eyePos to a 16-byte constant-buffer slot. preExposure is
// PassUniforms' field under another name: the sky draws in the same pass on the same slot, so it
// has to carry the same exposure or the background would ignore the exposure slider. Unused by
// this variant's fragment shader (see gExposureOverride below) but kept for CPU-mirror parity with
// Sky.slang's SkyUniforms.
struct SkyUniforms
{
    // On the temporal path this is the *unjittered* current view-projection and the jitter below
    // is applied to the rasterised position instead, so the motion pair the fragment derives stays
    // unjittered. With temporal off nothing jitters and the two readings coincide.
    float4x4 viewProj;
    float3 eyePos;
    float preExposure;
    // The frame's sub-pixel offset in NDC, read by the motion vertex entry point alone. Zero for
    // every frame that does not jitter, which is what leaves vertexMain's output untouched.
    float jitterNdcX;
    float jitterNdcY;
    float jitterPadding;
    // The previous declared frame's camera. Positioning the sphere at the previous eye is what
    // makes the sky's motion rotation-only: the camera's translation cancels with the sphere's.
    float4x4 previousViewProj;
    float3 previousEyePos;
};

[[vk::binding(2, 0)]]
ConstantBuffer<SkyUniforms> gSky : register(b2); // → MSL [[buffer(2)]]

// The persistent exposure buffer (spec 9's feedback buffer), the same one ScenePassAuto.slang
// binds at b3 -- one bindBuffer call in Renderer.cpp covers both, since bindings stay live across
// draws within one pass.
[[vk::binding(3, 0)]]
StructuredBuffer<float> gExposureOverride : register(t3); // → MSL [[buffer(3)]]

[[vk::binding(2, 2)]]
TextureCube gSkyCube : register(t2); // → MSL [[texture(2)]]

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0); // → MSL [[sampler(0)]]

struct VSOutput
{
    float4 position : SV_Position;
    // Origin-centered sphere positions are cubemap directions.
    float3 direction : TEXCOORD0;
};

VSOutput skyVertex(uint vid, float2 jitterNdc)
{
    float3 local = gVertices[vid].position;
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
VSOutput vertexMain(uint vid: SV_VertexID)
{
    return skyVertex(vid, float2(0.0, 0.0));
}

float3 skyRadiance(VSOutput input)
{
    // The sampled result is scene-linear: sRGB views decode automatically, while float views
    // already store linear radiance. It stays linear all the way to the display transform.
    float3 color = gSkyCube.Sample(gLinearSampler, normalize(input.direction)).rgb;
    // The one difference from Sky.slang: the multiplier comes from the persistent exposure buffer
    // (spec 9), not SkyUniforms.preExposure.
    return color * gExposureOverride[0];
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
SkyMotionVSOutput vertexMainMotion(uint vid: SV_VertexID)
{
    float3 local = gVertices[vid].position;
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
