// Alpha-tested variant of ScenePassAuto.slang; keep opaque shading and uniform layouts identical.
// The auto-exposure twin of ScenePass.slang (spec 9): byte-for-byte identical except the final
// multiply reads the persistent exposure buffer instead of PassUniforms.preExposure. A separate
// file rather than a runtime branch in one shader, because a branch that is never taken still
// gives the compiler a different fragment to schedule around -- proven by an M5 parity check that
// failed by a single rounding bit in one pixel once ScenePass.slang carried the branch. Splitting
// the two into their own compiled pipelines (Renderer.cpp's m_scenePipeline vs
// m_scenePipelineAuto, selected by SceneView::autoExposureEnabled, the same pattern the wireframe
// pipeline already uses) means the manual pipeline's shader source -- and therefore its compiled
// output -- is untouched by auto-exposure's existence, which is what parity with pre-M5 output
// actually requires. Keep every other edit to this file mirrored in ScenePass.slang and vice
// versa; the two are meant to diverge in exactly one place.
//
// Forward linear-space metallic-roughness pass: analytic directional lights with a GGX BRDF,
// image-based lighting from the scene's generated IBL set, normal mapping, occlusion, emissive, and
// light-0 shadows. Output is scene-linear radiance scaled by exposure; nothing here encodes,
// because the target is a half-float scene-colour buffer and Shaders/DisplayTransform.slang owns
// the encode.
//
// Slots -- resource classes use separate index spaces:
//   buffers   b0 vertices, b1 per-draw ObjectUniforms, b2 per-pass PassUniforms, b3 the persistent
//             exposure buffer (spec 9) -- this variant's one difference from ScenePass.slang
//   textures  per-draw material set: t0 base colour, t1 normal, t4 metallic-roughness,
//             t5 occlusion, t6 emissive
//             per-pass shared set:   t3 shadow map, t7 irradiance, t8 prefiltered environment,
//             t9 DFG lookup table
//   samplers  s0 linear (wrap, anisotropic), s1 shadow comparison, s2 linear clamped with a linear
//             mip filter, for the IBL set only
//
// t2 belongs to the sky cubemap, which only Shaders/SkyAuto.slang reads; the sky draws inside this
// same render pass, so the slot is bound there rather than here. This fragment no longer samples
// the sky directly -- its ad-hoc mirror reflection was replaced by the prefiltered environment on
// t8.
import Lighting;
import AlphaMask;
import Motion;
import Shadow;

// Mip count of the prefiltered chain, mirroring Source/Engine/Ibl.h's kSpecularMipCount. Perceptual
// roughness maps linearly onto [0, kSpecularMipCount - 1], which is the mapping that file
// prefilters each level for. Tests/RenderTests.cpp pins the two constants against each other.
static const float kSpecularMipCount = 5.0f;

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
    // Inverse transpose of `model`, computed on the CPU once per draw. Declared 4x4 rather than
    // 3x3 because a constant-buffer float3x3 pads each column to its own 16-byte register, which
    // makes the CPU mirror's 48 bytes agree with the shader's only by convention; a float4x4 has
    // one unambiguous layout on every target, and the vertex shader simply uses its upper-left 3x3.
    float4x4 normalMatrix;
    float4x4 uvTransform;
    float4 albedo; // linear; Engine decodes authored sRGB constants once at scene build
    float roughness;
    uint flags;
    float metallic;
    float occlusionStrength;
    float3 emissive;
    // The object-to-world transform of the previous declared frame, read by the motion entry
    // points alone. Equal to `model` for an object that did not move, which is what makes a still
    // surface reproject onto itself.
    float4x4 previousModel;
};

// Bit 0 enables the texture-slot-1 normal map through a draw-uniform branch.
static const uint kFlagHasNormalMap = 1u;
// Bit 1 marks a draw whose motion cannot be derived from `previousModel`; its fragments write
// Motion.slang's sentinel instead of a reprojection.
static const uint kFlagMotionInvalid = 2u;

[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUniforms> gObject : register(b1); // → MSL [[buffer(1)]]

struct PassUniforms
{
    float4x4 viewProj;
    float4x4 shadowTransform; // light view-proj with the NDC → texcoord map baked in
    float3 eyePos;
    float time;        // retained in the pass ABI; no current shader reads it
    float preExposure; // exp2(exposure in EV), applied to the fragment's linear output
    DirLight lights[3];
    int shadowFilter; // Shadow.slang's kShadowFilterPcf / kShadowFilterPcss
    // The motion pair, unjittered on purpose: jitter moves where the frame is sampled, never where
    // the surface is, so a still scene rendered with a moving jitter sequence must still produce
    // zero motion. Rasterisation uses ObjectUniforms.mvp, which carries the jitter.
    float4x4 viewProjUnjittered;
    float4x4 previousViewProjUnjittered;
};

[[vk::binding(2, 0)]]
ConstantBuffer<PassUniforms> gPass : register(b2); // → MSL [[buffer(2)]]

// The persistent exposure buffer (spec 9's feedback buffer). This pipeline only ever binds while
// auto-exposure is on, so it is always read -- unlike ScenePass.slang, which never binds it at all.
[[vk::binding(3, 0)]]
StructuredBuffer<float> gExposureOverride : register(t3); // → MSL [[buffer(3)]]

[[vk::binding(0, 2)]]
Texture2D gDiffuse : register(t0); // → MSL [[texture(0)]]
[[vk::binding(1, 2)]]
Texture2D gNormalMap : register(t1); // → MSL [[texture(1)]]
[[vk::binding(3, 2)]]
Texture2D gShadowMap : register(t3); // → MSL [[texture(3)]]

// Per-draw material inputs, bound every draw with a shared white fallback when a material has no
// map -- white is the identity for all three, so a factor-only material shades exactly as if the
// texture were absent. Channel conventions match glTF 2.0: metallic-roughness packs roughness = G
// and metallic = B; occlusion packs R.
[[vk::binding(4, 2)]]
Texture2D gMetallicRoughness : register(t4); // → MSL [[texture(4)]]
[[vk::binding(5, 2)]]
Texture2D gOcclusion : register(t5); // → MSL [[texture(5)]]
[[vk::binding(6, 2)]]
Texture2D gEmissive : register(t6); // → MSL [[texture(6)]]

// The scene's IBL set (Source/Engine/Ibl.h). Black cubes and a zero DFG table stand in when a
// SceneView carries no IBL, which makes both image-based terms vanish rather than read an unbound
// slot.
[[vk::binding(7, 2)]]
TextureCube gIrradiance : register(t7); // → MSL [[texture(7)]]
[[vk::binding(8, 2)]]
TextureCube gPrefilteredEnv : register(t8); // → MSL [[texture(8)]]
[[vk::binding(9, 2)]]
Texture2D gDfgLut : register(t9); // → MSL [[texture(9)]]

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0); // → MSL [[sampler(0)]]
[[vk::binding(1, 1)]]
SamplerComparisonState gShadowSampler : register(s1); // → MSL [[sampler(1)]]

// The IBL set needs clamped addressing, not the wrapping s0 above. Both of its lookups run to the
// very edge of their domain -- the DFG table is read at N.V = 1 and at roughness 1, and a wrapping
// sampler would blend those edge texels with the opposite edge's, so a facing mirror would pick up
// the grazing column and a fully rough surface the mirror row. Clamping also keeps a cube's
// bilinear footprint inside the face it selected.
[[vk::binding(2, 1)]]
SamplerState gIblSampler : register(s2); // → MSL [[sampler(2)]]

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

VSOutput shadeVertex(uint vid)
{
    Vertex v = gVertices[vid];
    float4 positionW = mul(gObject.model, float4(v.position, 1.0));

    VSOutput o;
    o.position = mul(gObject.mvp, float4(v.position, 1.0));
    o.positionW = positionW.xyz;
    // The inverse transpose is what keeps a normal perpendicular to its surface under non-uniform
    // scale, where the model matrix alone would shear it off the surface.
    o.normalW = mul((float3x3)gObject.normalMatrix, v.normal);
    // The tangent is a direction *along* the surface, so it transforms by the model matrix itself;
    // the fragment re-orthogonalizes it against the normal.
    o.tangentW = float4(mul((float3x3)gObject.model, v.tangent.xyz), v.tangent.w);
    o.uv = mul(gObject.uvTransform, float4(v.uv, 0.0, 1.0)).xy;
    o.shadowPos = mul(gPass.shadowTransform, positionW);
    return o;
}

// The reactive weight the temporal resolve reads out of `lmx.render.reactive` (M6.2 spec 5): how
// much of this pixel refuses to accumulate. Emissive radiance is the one signal in this shader that
// can appear or vanish without any surface moving, so a history blended over it would fade the
// change in instead of showing it. kReactiveEmissiveScale mirrors Source/Render/TemporalResolve.h
// and is the pre-exposed luminance that maps to a fully reactive pixel.
static const float kReactiveEmissiveScale = 4.0;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

float reactiveFromEmissive(float3 preExposedEmissive)
{
    return saturate(dot(preExposedEmissive, kLuminanceWeights) / kReactiveEmissiveScale);
}

// What one shaded fragment produces: the pre-exposed colour every entry point writes, and the
// pre-exposed emissive term inside it, which only the motion entry point needs. Splitting the
// value out rather than shading twice is what keeps fragmentMain's compiled output identical to
// what it was before a reactive attachment existed.
struct SurfaceShading
{
    float4 color;
    float3 emissive;
};

SurfaceShading shadeSurfaceDetailed(VSOutput input, bool frontFace)
{
    // sRGB sampling decodes base colour before lighting.
    float4 baseColorSample = gDiffuse.Sample(gLinearSampler, input.uv) * gObject.albedo;
    applyAlphaMask(baseColorSample.a);
    float3 baseColor = baseColorSample.rgb;

    // glTF 2.0 packing: roughness in G, metallic in B, each scaling its own factor.
    float3 metallicRoughness = gMetallicRoughness.Sample(gLinearSampler, input.uv).rgb;
    // Clamped once, here, so the direct lobe, the prefiltered mip, and the DFG lookup all speak of
    // the same roughness -- the split-sum contract only holds if they agree.
    float perceptualRoughness =
        clamp(gObject.roughness * metallicRoughness.g, kMinRoughness, 1.0f);
    float metallic = saturate(gObject.metallic * metallicRoughness.b);
    float alpha = AlphaFromRoughness(perceptualRoughness);
    // glTF strength interpolates between no occlusion and the authored map; multiplying by the
    // strength directly would incorrectly black out the environment when strength is zero.
    float occlusion = lerp(1.0f, gOcclusion.Sample(gLinearSampler, input.uv).r,
                           saturate(gObject.occlusionStrength));

    float3 normalW = normalize(input.normalW);
    if ((gObject.flags & kFlagHasNormalMap) != 0u)
    {
        normalW = applyNormalMap(gNormalMap.Sample(gLinearSampler, input.uv).rgb, normalW,
                                 input.tangentW);
    }

    normalW = frontFace ? normalW : -normalW;

    float3 toEye = normalize(gPass.eyePos - input.positionW);
    float3 f0 = F0FromBaseColor(baseColor, metallic);

    // Only light 0 owns a shadow map.
    float shadowFactor =
        CalcShadowFactor(input.shadowPos, gShadowMap, gShadowSampler, gPass.shadowFilter);
    float3 litColor = shadowFactor * ComputeDirectionalLight(gPass.lights[0], normalW, toEye,
                                                             baseColor, f0, metallic, alpha);
    litColor += ComputeDirectionalLight(gPass.lights[1], normalW, toEye, baseColor, f0, metallic,
                                        alpha);
    litColor += ComputeDirectionalLight(gPass.lights[2], normalW, toEye, baseColor, f0, metallic,
                                        alpha);

    // Image-based lighting. N.V is clamped off zero because the DFG table's first column stands for
    // a half-texel inside the domain, not for the grazing limit itself.
    float nov = clamp(dot(normalW, toEye), 1e-4f, 1.0f);
    float3 reflectDir = reflect(-toEye, normalW);
    float3 irradiance = gIrradiance.Sample(gIblSampler, normalW).rgb;
    float3 prefiltered =
        gPrefilteredEnv.SampleLevel(gIblSampler, reflectDir,
                                    perceptualRoughness * (kSpecularMipCount - 1.0f))
            .rgb;
    float2 dfg = gDfgLut.Sample(gIblSampler, float2(nov, perceptualRoughness)).rg;
    // Occlusion attenuates the environment only. It describes how much of the surrounding hemisphere
    // reaches this point, which says nothing about a light whose visibility the shadow map already
    // answered -- applying it to both would darken a lit surface twice.
    litColor +=
        occlusion * ImageBasedLight(irradiance, prefiltered, dfg, baseColor, f0, metallic);

    // Emissive is radiance the surface produces rather than reflects, so it takes no lighting term
    // and is added last; pre-exposure then applies to the whole sum exactly once.
    float3 emissiveTerm = gObject.emissive * gEmissive.Sample(gLinearSampler, input.uv).rgb;
    litColor += emissiveTerm;

    // Pre-exposed, still linear: the display transform is the only thing that leaves this space.
    // The one difference from ScenePass.slang: the multiplier comes from the persistent exposure
    // buffer (spec 9), not PassUniforms.preExposure.
    SurfaceShading shaded;
    shaded.color = float4(litColor * gExposureOverride[0], 1.0);
    shaded.emissive = emissiveTerm * gExposureOverride[0];
    return shaded;
}

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    return shadeVertex(vid);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool frontFace: SV_IsFrontFace): SV_Target
{
    return shadeSurfaceDetailed(input, frontFace).color;
}

// The motion variant of the pair above: the same shading, plus the unjittered clip pair the
// fragment turns into this pixel's motion. It is a second entry point rather than a branch in the
// one above so that a temporal-off frame compiles and rasterises exactly what it did before the
// motion attachment existed.
struct MotionVSOutput
{
    VSOutput base;
    float4 clipCurrent : TEXCOORD1;
    float4 clipPrevious : TEXCOORD2;
};

struct MotionFSOutput
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float reactive : SV_Target2;
};

[shader("vertex")]
MotionVSOutput vertexMainMotion(uint vid: SV_VertexID)
{
    float3 position = gVertices[vid].position;
    MotionVSOutput o;
    o.base = shadeVertex(vid);
    o.clipCurrent = mul(gPass.viewProjUnjittered, mul(gObject.model, float4(position, 1.0)));
    o.clipPrevious =
        mul(gPass.previousViewProjUnjittered, mul(gObject.previousModel, float4(position, 1.0)));
    return o;
}

[shader("fragment")]
MotionFSOutput fragmentMainMotion(MotionVSOutput input, bool frontFace: SV_IsFrontFace)
{
    MotionFSOutput o;
    SurfaceShading shaded = shadeSurfaceDetailed(input.base, frontFace);
    o.color = shaded.color;
    o.reactive = reactiveFromEmissive(shaded.emissive);
    o.motion = (gObject.flags & kFlagMotionInvalid) != 0u
                   ? kMotionInvalid
                   : motionFromClip(input.clipCurrent, input.clipPrevious);
    return o;
}
