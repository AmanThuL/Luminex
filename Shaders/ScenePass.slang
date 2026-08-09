// Forward linear-space Blinn-Phong pass with normal mapping, environment reflection, and light-0
// shadows. Output is scene-linear radiance scaled by exposure; nothing here encodes, because the
// target is a half-float scene-colour buffer and Shaders/DisplayTransform.slang owns the encode.
// Slots: buffers 0/1/2, textures 0/1/2/3, samplers 0/1. Resource classes use separate index spaces.
import Lighting;
import Shadow;

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent; // xyz tangent, w handedness (+1/-1)
    float2 uv;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0); // → MSL [[buffer(0)]]

// CPU mirrors must match this constant-buffer layout byte-for-byte.
struct ObjectUniforms
{
    float4x4 mvp;
    float4x4 model;
    float4x4 uvTransform;
    float4 albedo;    // linear; Engine decodes authored sRGB constants once at scene build
    float3 fresnelR0; // reflectance at normal incidence, per channel
    float roughness;
    uint flags;
};

// Bit 0 enables the texture-slot-1 normal map through a draw-uniform branch.
static const uint kFlagHasNormalMap = 1u;

[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUniforms> gObject : register(b1); // → MSL [[buffer(1)]]

struct PassUniforms
{
    float4x4 viewProj;
    float4x4 shadowTransform; // light view-proj with the NDC → texcoord map baked in
    float3 eyePos;
    float time;        // retained in the pass ABI; no current shader reads it
    float preExposure; // exp2(exposure in EV), applied to the fragment's linear output
    float4 ambient;
    DirLight lights[3];
    int shadowFilter; // Shadow.slang's kShadowFilterPcf / kShadowFilterPcss
};

[[vk::binding(2, 0)]]
ConstantBuffer<PassUniforms> gPass : register(b2); // → MSL [[buffer(2)]]

[[vk::binding(0, 2)]]
Texture2D gDiffuse : register(t0); // → MSL [[texture(0)]]
[[vk::binding(1, 2)]]
Texture2D gNormalMap : register(t1); // → MSL [[texture(1)]]
[[vk::binding(2, 2)]]
TextureCube gSkyCube : register(t2); // → MSL [[texture(2)]]
[[vk::binding(3, 2)]]
Texture2D gShadowMap : register(t3); // → MSL [[texture(3)]]

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0); // → MSL [[sampler(0)]]
[[vk::binding(1, 1)]]
SamplerComparisonState gShadowSampler : register(s1); // → MSL [[sampler(1)]]

// Build the tangent basis explicitly to preserve handedness and matrix convention.
float3 applyNormalMap(float3 mapSample, float3 unitNormalW, float4 tangentW)
{
    float3 normalT = 2.0 * mapSample - 1.0;
    float3 N = unitNormalW;
    // Re-orthogonalize the interpolated tangent against the normal.
    float3 T = normalize(tangentW.xyz - dot(tangentW.xyz, N) * N);
    float3 B = cross(N, T) * tangentW.w;
    return normalize(normalT.x * T + normalT.y * B + normalT.z * N);
}

struct VSOutput
{
    float4 position : SV_Position;
    float3 positionW : POSITION0;
    float3 normalW : NORMAL0;
    float4 tangentW : TANGENT0;
    float2 uv : TEXCOORD0;
    // Keep shadow coordinates homogeneous until fragment interpolation completes.
    float4 shadowPos : POSITION1;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex v = gVertices[vid];
    float4 positionW = mul(gObject.model, float4(v.position, 1.0));

    VSOutput o;
    o.position = mul(gObject.mvp, float4(v.position, 1.0));
    o.positionW = positionW.xyz;
    // Without an inverse-transpose, geometry must avoid normal-affecting non-uniform scale.
    o.normalW = mul((float3x3)gObject.model, v.normal);
    o.tangentW = float4(mul((float3x3)gObject.model, v.tangent.xyz), v.tangent.w);
    o.uv = mul(gObject.uvTransform, float4(v.uv, 0.0, 1.0)).xy;
    o.shadowPos = mul(gPass.shadowTransform, positionW);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    // sRGB sampling decodes albedo before lighting.
    float4 diffuseAlbedo = gDiffuse.Sample(gLinearSampler, input.uv) * gObject.albedo;

    float3 normalW = normalize(input.normalW);
    if ((gObject.flags & kFlagHasNormalMap) != 0u)
    {
        normalW = applyNormalMap(gNormalMap.Sample(gLinearSampler, input.uv).rgb, normalW,
                                 input.tangentW);
    }

    float3 toEye = normalize(gPass.eyePos - input.positionW);
    const float shininess = 1.0 - gObject.roughness;

    float3 litColor = gPass.ambient.rgb * diffuseAlbedo.rgb;

    // Only light 0 owns a shadow map.
    float shadowFactor =
        CalcShadowFactor(input.shadowPos, gShadowMap, gShadowSampler, gPass.shadowFilter);
    litColor += shadowFactor * ComputeDirectionalLight(gPass.lights[0], diffuseAlbedo.rgb,
                                                       gObject.fresnelR0, shininess, normalW,
                                                       toEye);
    litColor += ComputeDirectionalLight(gPass.lights[1], diffuseAlbedo.rgb, gObject.fresnelR0,
                                        shininess, normalW, toEye);
    litColor += ComputeDirectionalLight(gPass.lights[2], diffuseAlbedo.rgb, gObject.fresnelR0,
                                        shininess, normalW, toEye);

    // A black fallback cubemap makes the environment term neutral when no sky is bound.
    float3 reflectDir = reflect(-toEye, normalW);
    float3 reflectionColor = gSkyCube.Sample(gLinearSampler, reflectDir).rgb;
    float3 fresnelFactor = SchlickFresnel(gObject.fresnelR0, normalW, reflectDir);
    litColor += shininess * fresnelFactor * reflectionColor;

    // Pre-exposed, still linear: the display transform is the only thing that leaves this space.
    return float4(litColor * gPass.preExposure, diffuseAlbedo.a);
}
