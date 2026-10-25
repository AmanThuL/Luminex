// Scene-linear shading with address-first scene and per-run material roots.
import RojoRHI;
import SceneRoots;
import VertexStorage;
import Lighting;
import SceneTables;
import Motion;
import Shadow;

// Mip count of the prefiltered chain, mirroring Source/Engine/Ibl.h's kSpecularMipCount. Perceptual
// roughness maps linearly onto [0, kSpecularMipCount - 1], which is the mapping that file
// prefilters each level for. Tests/Engine/Asset/EngineMeshGeneratorTests.cpp pins the two constants
// against each other.
static const float kSpecularMipCount = 5.0f;

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
    nointerpolation uint instanceRow : TEXCOORD3;
    float4 position : SV_Position;
    float3 positionW : POSITION0;
    float3 normalW : NORMAL0;
    float4 tangentW : TANGENT0;
    float2 uv : TEXCOORD0;
    // Keep shadow coordinates homogeneous until fragment interpolation completes.
    float4 shadowPos : POSITION1;
};

VSOutput shadeVertex(uint vid, uint instanceRow)
{
    SceneDrawRoot draw = rojoRHI::root<SceneDrawRoot>();
    ScenePassRoot pass = rojoRHI::load<ScenePassRoot>(draw.pass, 0);
    PassUniforms gPass = rojoRHI::load<PassUniforms>(pass.passUniforms, 0);
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(pass.materials, instance.materialRow);
    Vertex v = loadVertex(pass.vertices, vid);
    float4 positionW = mul(instance.model, float4(v.position, 1.0));

    VSOutput o;
    o.instanceRow = instanceRow;
    o.position = mul(gPass.viewProj, positionW);
    o.positionW = positionW.xyz;
    // The inverse transpose is what keeps a normal perpendicular to its surface under non-uniform
    // scale, where the model matrix alone would shear it off the surface.
    o.normalW = mul((float3x3)instance.normalMatrix, v.normal);
    // The tangent is a direction *along* the surface, so it transforms by the model matrix itself;
    // the fragment re-orthogonalizes it against the normal.
    o.tangentW = float4(mul((float3x3)instance.model, v.tangent.xyz), v.tangent.w);
    o.uv = mul(material.uvTransform, float4(v.uv, 0.0, 1.0)).xy;
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

SurfaceShading shadeSurfaceDetailed(VSOutput input)
{
    SceneDrawRoot draw = rojoRHI::root<SceneDrawRoot>();
    ScenePassRoot pass = rojoRHI::load<ScenePassRoot>(draw.pass, 0);
    PassUniforms gPass = rojoRHI::load<PassUniforms>(pass.passUniforms, 0);
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, input.instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(pass.materials, instance.materialRow);
    // sRGB sampling decodes base colour before lighting.
    float4 baseColorSample = rojoRHI::texture2D(draw.baseColor).Sample(rojoRHI::sampler(pass.linearSampler), input.uv) * material.albedo;
    float3 baseColor = baseColorSample.rgb;

    // glTF 2.0 packing: roughness in G, metallic in B, each scaling its own factor.
    float3 metallicRoughness = rojoRHI::texture2D(draw.metalRough).Sample(rojoRHI::sampler(pass.linearSampler), input.uv).rgb;
    // Clamped once, here, so the direct lobe, the prefiltered mip, and the DFG lookup all speak of
    // the same roughness -- the split-sum contract only holds if they agree.
    float perceptualRoughness =
        clamp(material.roughness * metallicRoughness.g, kMinRoughness, 1.0f);
    float metallic = saturate(material.metallic * metallicRoughness.b);
    float alpha = AlphaFromRoughness(perceptualRoughness);
    // glTF strength interpolates between no occlusion and the authored map; multiplying by the
    // strength directly would incorrectly black out the environment when strength is zero.
    float occlusion = lerp(1.0f, rojoRHI::texture2D(draw.occlusion).Sample(rojoRHI::sampler(pass.linearSampler), input.uv).r,
                           saturate(material.occlusionStrength));

    float3 normalW = normalize(input.normalW);
    if ((material.flags & kMaterialHasNormalMap) != 0u)
    {
        normalW = applyNormalMap(rojoRHI::texture2D(draw.normal).Sample(rojoRHI::sampler(pass.linearSampler), input.uv).rgb, normalW,
                                 input.tangentW);
    }

    float3 toEye = normalize(gPass.eyePos - input.positionW);
    float3 f0 = F0FromBaseColor(baseColor, metallic);

    // Only light 0 owns a shadow map.
    float shadowFactor =
        CalcShadowFactor(input.shadowPos, rojoRHI::texture2D(pass.shadow), rojoRHI::comparisonSampler(pass.shadowSampler), gPass.shadowFilter);
    float3 litColor = (gPass.shadowCaster == 0 ? shadowFactor : 1.0f) *
                      ComputeDirectionalLight(gPass.lights[0], normalW, toEye,
                                                             baseColor, f0, metallic, alpha);
    litColor += (gPass.shadowCaster == 1 ? shadowFactor : 1.0f) *
                ComputeDirectionalLight(gPass.lights[1], normalW, toEye, baseColor, f0, metallic,
                                        alpha);
    litColor += (gPass.shadowCaster == 2 ? shadowFactor : 1.0f) *
                ComputeDirectionalLight(gPass.lights[2], normalW, toEye, baseColor, f0, metallic,
                                        alpha);

    // Local-light accumulation is unavailable until address-based light lists are supplied.

    // Image-based lighting. N.V is clamped off zero because the DFG table's first column stands for
    // a half-texel inside the domain, not for the grazing limit itself.
    float nov = clamp(dot(normalW, toEye), 1e-4f, 1.0f);
    float3 reflectDir = reflect(-toEye, normalW);
    float3 irradiance = rojoRHI::textureCube(pass.irradiance).Sample(rojoRHI::sampler(pass.iblSampler), normalW).rgb;
    float3 prefiltered =
        rojoRHI::textureCube(pass.prefilteredEnv).SampleLevel(rojoRHI::sampler(pass.iblSampler), reflectDir,
                                    perceptualRoughness * (kSpecularMipCount - 1.0f))
            .rgb;
    float2 dfg = rojoRHI::texture2D(pass.dfg).Sample(rojoRHI::sampler(pass.iblSampler), float2(nov, perceptualRoughness)).rg;
    // Occlusion attenuates the environment only. It describes how much of the surrounding hemisphere
    // reaches this point, which says nothing about a light whose visibility the shadow map already
    // answered -- applying it to both would darken a lit surface twice.
    litColor +=
        occlusion * ImageBasedLight(irradiance, prefiltered, dfg, baseColor, f0, metallic);

    // Emissive is radiance the surface produces rather than reflects, so it takes no lighting term
    // and is added last; pre-exposure then applies to the whole sum exactly once.
    float3 emissiveTerm =
        (material.emissive * instance.emissiveScale) * rojoRHI::texture2D(draw.emissive).Sample(rojoRHI::sampler(pass.linearSampler), input.uv).rgb;
    litColor += emissiveTerm;

    // Pre-exposed, still linear: the display transform is the only thing that leaves this space.
    SurfaceShading shaded;
    shaded.color = float4(litColor * gPass.preExposure, baseColorSample.a);
    shaded.emissive = emissiveTerm * gPass.preExposure;
    return shaded;
}

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    SceneDrawRoot draw = rojoRHI::root<SceneDrawRoot>();
    ScenePassRoot pass = rojoRHI::load<ScenePassRoot>(draw.pass, 0);
    uint firstEntry = draw.indirect != 0 ? rojoRHI::drawId(ids) : draw.firstEntry;
    return shadeVertex(rojoRHI::vertexIndex(ids), rojoRHI::load<uint>(pass.visibleRows, firstEntry + rojoRHI::instanceIndex(ids)));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    return shadeSurfaceDetailed(input).color;
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
MotionVSOutput vertexMainMotion(rojoRHI::VertexIds ids)
{
    SceneDrawRoot draw = rojoRHI::root<SceneDrawRoot>();
    ScenePassRoot pass = rojoRHI::load<ScenePassRoot>(draw.pass, 0);
    PassUniforms gPass = rojoRHI::load<PassUniforms>(pass.passUniforms, 0);
    uint vid = rojoRHI::vertexIndex(ids);
    uint firstEntry = draw.indirect != 0 ? rojoRHI::drawId(ids) : draw.firstEntry;
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, rojoRHI::load<uint>(pass.visibleRows, firstEntry + rojoRHI::instanceIndex(ids)));
    float3 position = loadVertex(pass.vertices, vid).position;
    MotionVSOutput o;
    o.base = shadeVertex(vid, rojoRHI::load<uint>(pass.visibleRows, firstEntry + rojoRHI::instanceIndex(ids)));
    o.clipCurrent = mul(gPass.viewProjUnjittered, mul(instance.model, float4(position, 1.0)));
    o.clipPrevious =
        mul(gPass.previousViewProjUnjittered, mul(instance.previousModel, float4(position, 1.0)));
    return o;
}

[shader("fragment")]
MotionFSOutput fragmentMainMotion(MotionVSOutput input)
{
    SceneDrawRoot draw = rojoRHI::root<SceneDrawRoot>();
    ScenePassRoot pass = rojoRHI::load<ScenePassRoot>(draw.pass, 0);
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, input.base.instanceRow);
    MotionFSOutput o;
    SurfaceShading shaded = shadeSurfaceDetailed(input.base);
    o.color = shaded.color;
    o.reactive = reactiveFromEmissive(shaded.emissive);
    o.motion = (instance.flags & kInstanceMotionInvalid) != 0u
                   ? kMotionInvalid
                   : motionFromClip(input.clipCurrent, input.clipPrevious);
    return o;
}
