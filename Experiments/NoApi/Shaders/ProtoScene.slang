// P04's address-first frontend (spec section 6): the complete production ScenePass binding surface
// re-expressed over root addresses and one bindless table, with the shading math imported from the
// *same* production modules `Shaders/ScenePass.slang` imports -- `Lighting` and `Shadow`, reached by
// relative path because slangc resolves an import against the compiled file's own directory and this
// file compiles from `Experiments/NoApi/Shaders`. Nothing below recomputes a lighting term; the only
// thing that differs from ScenePass.slang is where a resource comes from (spec section 2:
// "only resource and root-data plumbing may differ"), so the byte-identical parity oracle is bounding
// a divergence this file deliberately has none of.
//
// Binding surface, all of it address-first:
//   buffer 0  per-draw root  (SceneObjectRoot): the b1 fields, the five material slot indices, and
//                            the vertex array's address
//   buffer 2  pass root      (ScenePassRoot): the b2 fields, the pass-shared slot indices, and the
//                            bindless table's address under each view type it is read through
//   (none)    everything else: the shadow map, the IBL set, the material set, and all three samplers
//                            are slot indices into the single bindless table. There is no texture
//                            binding, no sampler binding, and no per-draw descriptor object anywhere
//                            in this pass.
//
// FINDING -- the interface's `draw(vertexRoot, pixelRoot, ...)` names its two root addresses per
// stage, but Metal 4's argument table is shared by both render stages, so index 0 and index 2 are
// simply two root blocks that either stage may read. This pass uses them that way: the vertex stage
// reads the per-draw block at 0 *and* the pass block at 2, and so does the fragment stage. The
// per-stage naming is a model-side fiction on this target (evidence section 6.10); the traffic is
// unchanged either way, two `setAddress` calls per draw.
//
// The table is reached from root data rather than from a bound table index because a Metal buffer
// binding can carry only one shader-side element type, and this pass needs five views of the one
// table (2D, cube, sampler, comparison sampler, and -- in the bloom kernels -- storage image). See
// docs/research/2026-08-12-execution-model-evidence.md section 5.10.
import "../../../Shaders/Lighting.slang";
import "../../../Shaders/Shadow.slang";
import NoApiBindless;

// Mirrors Shaders/ScenePass.slang's own kSpecularMipCount, itself mirroring
// Source/Engine/Ibl.h's kSpecularMipCount.
static const float kSpecularMipCount = 5.0f;

// Bit 0 enables the normal map, exactly as ScenePass.slang's kFlagHasNormalMap does.
static const uint kFlagHasNormalMap = 1u;

// The pass-shared block: `Shaders/ScenePass.slang`'s b2 fields plus the slot indices and table
// addresses that replace its t3/t7/t8/t9 and s0/s1/s2 bindings. Pushed once per frame; its address
// is re-bound at index 2 by every draw, because nothing in this interface deduplicates a root
// address against the one already bound.
//
// Layout rule for every root block in this experiment: 16-byte-sized members first, eight-byte
// pointers last and in pairs, so the constant-buffer layout Slang gives a `ConstantBuffer<T>` and the
// natural layout it gives a `ConstBufferPointer<T>` pointee agree member for member with the CPU
// mirror in Bench/NoApiAdapter.cpp.
struct ScenePassRoot
{
    float4x4 viewProj;
    float4x4 shadowTransform;  // light view-proj with the NDC → texcoord map baked in
    float4 eyePositionAndTime; // xyz eye position, w time (in the ABI, read by nothing)
    float4 preExposure;        // x exp2(exposure in EV); yzw unused
    float4 lightStrength0;
    float4 lightDirection0;
    float4 lightStrength1;
    float4 lightDirection1;
    float4 lightStrength2;
    float4 lightDirection2;
    uint4 sharedSlots;  // shadow map, irradiance, prefiltered environment, DFG lookup table
    uint4 samplerSlots; // linear, shadow comparison, IBL; w = Shadow.slang's filter selector
    ConstBufferPointer<TextureSlot> textures;
    ConstBufferPointer<CubeSlot> cubes;
    ConstBufferPointer<SamplerSlot> samplers;
    ConstBufferPointer<ComparisonSamplerSlot> comparisonSamplers;
};

// The per-draw block: `Shaders/ScenePass.slang`'s b1 fields plus the five per-material slot indices
// that replace its t0/t1/t4/t5/t6 binds, and the shared quad's address.
struct SceneObjectRoot
{
    float4x4 mvp;
    float4x4 model;
    float4x4 normalMatrix; // Inverse transpose of `model`; the vertex stage uses its upper-left 3x3.
    float4x4 uvTransform;
    float4 albedo;         // linear
    float4 materialParams; // x roughness, y metallic, z occlusion strength, w unused
    float4 emissive;       // xyz emissive factor, w unused
    uint4 flagsAndSlots;   // x flags, y base colour slot, z normal slot, w metallic-roughness slot
    uint4 materialSlots;   // x occlusion slot, y emissive slot; zw unused
    ConstBufferPointer<Vertex> vertices;
    uint2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneObjectRoot> gObject : register(b0); // → MSL [[buffer(0)]], the per-draw root
[[vk::binding(2, 0)]]
ConstantBuffer<ScenePassRoot> gPass : register(b2); // → MSL [[buffer(2)]], the pass root

// Build the tangent basis explicitly to preserve handedness and matrix convention. Restated from
// ScenePass.slang line for line: it is a private helper there, not a module export.
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
    Vertex v = gObject.vertices[vid];
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

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    SamplerState linearSampler = gPass.samplers[gPass.samplerSlots.x].handle;
    SamplerComparisonState shadowSampler =
        gPass.comparisonSamplers[gPass.samplerSlots.y].handle;
    SamplerState iblSampler = gPass.samplers[gPass.samplerSlots.z].handle;

    Texture2D diffuse = gPass.textures[gObject.flagsAndSlots.y].handle;
    Texture2D normalMap = gPass.textures[gObject.flagsAndSlots.z].handle;
    Texture2D metallicRoughnessMap = gPass.textures[gObject.flagsAndSlots.w].handle;
    Texture2D occlusionMap = gPass.textures[gObject.materialSlots.x].handle;
    Texture2D emissiveMap = gPass.textures[gObject.materialSlots.y].handle;
    Texture2D shadowMap = gPass.textures[gPass.sharedSlots.x].handle;
    TextureCube irradianceMap = gPass.cubes[gPass.sharedSlots.y].handle;
    TextureCube prefilteredEnvMap = gPass.cubes[gPass.sharedSlots.z].handle;
    Texture2D dfgLut = gPass.textures[gPass.sharedSlots.w].handle;

    // sRGB sampling decodes base colour before lighting.
    float4 baseColorSample = diffuse.Sample(linearSampler, input.uv) * gObject.albedo;
    float3 baseColor = baseColorSample.rgb;

    // glTF 2.0 packing: roughness in G, metallic in B, each scaling its own factor.
    float3 metallicRoughness = metallicRoughnessMap.Sample(linearSampler, input.uv).rgb;
    // Clamped once, here, so the direct lobe, the prefiltered mip, and the DFG lookup all speak of
    // the same roughness -- the split-sum contract only holds if they agree.
    float perceptualRoughness =
        clamp(gObject.materialParams.x * metallicRoughness.g, kMinRoughness, 1.0f);
    float metallic = saturate(gObject.materialParams.y * metallicRoughness.b);
    float alpha = AlphaFromRoughness(perceptualRoughness);
    // glTF strength interpolates between no occlusion and the authored map; multiplying by the
    // strength directly would incorrectly black out the environment when strength is zero.
    float occlusion = lerp(1.0f, occlusionMap.Sample(linearSampler, input.uv).r,
                           saturate(gObject.materialParams.z));

    float3 normalW = normalize(input.normalW);
    if ((gObject.flagsAndSlots.x & kFlagHasNormalMap) != 0u)
    {
        normalW = applyNormalMap(normalMap.Sample(linearSampler, input.uv).rgb, normalW,
                                 input.tangentW);
    }

    float3 toEye = normalize(gPass.eyePositionAndTime.xyz - input.positionW);
    float3 f0 = F0FromBaseColor(baseColor, metallic);

    DirLight light0;
    light0.strength = gPass.lightStrength0.xyz;
    light0.direction = gPass.lightDirection0.xyz;
    DirLight light1;
    light1.strength = gPass.lightStrength1.xyz;
    light1.direction = gPass.lightDirection1.xyz;
    DirLight light2;
    light2.strength = gPass.lightStrength2.xyz;
    light2.direction = gPass.lightDirection2.xyz;

    // Only light 0 owns a shadow map.
    float shadowFactor = CalcShadowFactor(input.shadowPos, shadowMap, shadowSampler,
                                          int(gPass.samplerSlots.w));
    float3 litColor =
        shadowFactor * ComputeDirectionalLight(light0, normalW, toEye, baseColor, f0, metallic,
                                               alpha);
    litColor += ComputeDirectionalLight(light1, normalW, toEye, baseColor, f0, metallic, alpha);
    litColor += ComputeDirectionalLight(light2, normalW, toEye, baseColor, f0, metallic, alpha);

    // Image-based lighting. N.V is clamped off zero because the DFG table's first column stands for
    // a half-texel inside the domain, not for the grazing limit itself.
    float nov = clamp(dot(normalW, toEye), 1e-4f, 1.0f);
    float3 reflectDir = reflect(-toEye, normalW);
    float3 irradiance = irradianceMap.Sample(iblSampler, normalW).rgb;
    float3 prefiltered =
        prefilteredEnvMap
            .SampleLevel(iblSampler, reflectDir, perceptualRoughness * (kSpecularMipCount - 1.0f))
            .rgb;
    float2 dfg = dfgLut.Sample(iblSampler, float2(nov, perceptualRoughness)).rg;
    // Occlusion attenuates the environment only. It describes how much of the surrounding hemisphere
    // reaches this point, which says nothing about a light whose visibility the shadow map already
    // answered -- applying it to both would darken a lit surface twice.
    litColor += occlusion * ImageBasedLight(irradiance, prefiltered, dfg, baseColor, f0, metallic);

    // Emissive is radiance the surface produces rather than reflects, so it takes no lighting term
    // and is added last; pre-exposure then applies to the whole sum exactly once.
    litColor += gObject.emissive.rgb * emissiveMap.Sample(linearSampler, input.uv).rgb;

    // Pre-exposed, still linear: the display transform is the only thing that leaves this space.
    return float4(litColor * gPass.preExposure.x, baseColorSample.a);
}
