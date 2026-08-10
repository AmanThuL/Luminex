// Camera-centered sky sphere pinned to the far plane. The pipeline uses GreaterEqual depth and no
// culling because the camera views the sphere from inside. Slots: buffers 0/2, texture 2, sampler 0.

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0); // → MSL [[buffer(0)]]

// The dedicated 96-byte CPU mirror pads eyePos to a 16-byte constant-buffer slot. preExposure is
// PassUniforms' field under another name: the sky draws in the same pass on the same slot, so it
// has to carry the same exposure or the background would ignore the exposure slider.
struct SkyUniforms
{
    float4x4 viewProj;
    float3 eyePos;
    float preExposure;
};

[[vk::binding(2, 0)]]
ConstantBuffer<SkyUniforms> gSky : register(b2); // → MSL [[buffer(2)]]

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

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    float3 local = gVertices[vid].position;
    VSOutput o;
    float4 clip = mul(gSky.viewProj, float4(local + gSky.eyePos, 1.0));
    // Pin every sky vertex to the far plane, which reversed depth puts at 0: z / w = 0 whatever
    // the sphere's own geometry says, so the sky rasterises at exactly the value the pass cleared
    // to and its GreaterEqual test lets it through only where no geometry has written. (The
    // conventional-depth form of this trick was clip.xyww, pinning z / w to 1.)
    o.position = float4(clip.xy, 0.0, clip.w);
    o.direction = local;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    // The sampled result is scene-linear: sRGB views decode automatically, while float views
    // already store linear radiance. It stays linear all the way to the display transform.
    float3 color = gSkyCube.Sample(gLinearSampler, normalize(input.direction)).rgb;
    return float4(color * gSky.preExposure, 1.0);
}
