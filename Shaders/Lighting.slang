// Physically based shading module: a metallic-roughness microfacet BRDF for analytic lights and
// the split-sum reconstruction for image-based lighting. Owns no resources or entry points.
//
// Techniques, so each formula below can be checked against its source:
//   D  -- GGX / Trowbridge-Reitz normal distribution (Walter et al., "Microfacet Models for
//         Refraction through Rough Surfaces", EGSR 2007).
//   V  -- height-correlated Smith visibility in the combined G / (4 N.V N.L) form (Heitz,
//         "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs", JCGT 2014).
//   F  -- Schlick's Fresnel approximation (Schlick, "An Inexpensive BRDF Model for Physically-Based
//         Rendering", Eurographics 1994).
//   IBL -- the split-sum approximation (Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013)
//         with multiple-scattering energy compensation (Fdez-Aguera, "A Multiple-Scattering
//         Microfacet Model for Real-Time Image-based Lighting", JCGT 2019).
//
// Every quantity here is linear radiance. Source/Engine/Ibl.h generates the three IBL inputs and
// documents the conventions this module consumes them under; the two files have to agree, so the
// visibility term below is deliberately the same one Ibl.cpp's DFG integration uses.
module Lighting;

static const float kPi = 3.141592653589793;

// Constant-buffer float3 fields occupy 16-byte slots in the CPU mirror.
public struct DirLight
{
    public float3 strength;
    public float3 direction;
};

// Dielectric reflectance at normal incidence: the 4% every non-metal is approximated with, and the
// end of the mix a metal's own base colour replaces.
public static const float kDielectricF0 = 0.04f;

// Perceptual-roughness floor. A perfectly smooth surface concentrates D into a delta the raster
// grid cannot resolve, so a mirror highlight aliases into single blown-out pixels that swim as the
// camera moves; clamping the perceptual value before it is squared bounds the lobe's peak while
// leaving anything a material actually authors untouched.
public static const float kMinRoughness = 0.045f;

//======================================================================================================================
// The GGX width. Perceptual roughness is squared because that is the parameterization glTF authors
// in and the one Source/Engine/Ibl.h prefilters and integrates its DFG table against.
public float AlphaFromRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

//======================================================================================================================
public float D_GGX(float noh, float alpha)
{
    float a2 = alpha * alpha;
    float d = noh * noh * (a2 - 1.0f) + 1.0f;
    return a2 / max(kPi * d * d, 1e-9f);
}

//======================================================================================================================
// Height-correlated Smith, already divided by the BRDF's 4 N.V N.L denominator.
public float V_SmithHeightCorrelated(float nov, float nol, float alpha)
{
    float a2 = alpha * alpha;
    float view = nov * sqrt(nol * nol * (1.0f - a2) + a2);
    float light = nol * sqrt(nov * nov * (1.0f - a2) + a2);
    return 0.5f / max(view + light, 1e-5f);
}

//======================================================================================================================
// The multiply chain avoids pow()'s domain behaviour at the endpoints.
public float3 F_Schlick(float3 f0, float voh)
{
    float f = 1.0f - voh;
    float f5 = f * f * f * f * f;
    return f0 + (1.0f - f0) * f5;
}

//======================================================================================================================
// Reflectance at normal incidence: dielectrics keep the 4% constant and a metal's base colour takes
// over as it becomes conductive, which is what lets the shader derive F0 rather than carry it.
public float3 F0FromBaseColor(float3 baseColor, float metallic)
{
    return lerp(float3(kDielectricF0), baseColor, metallic);
}

//======================================================================================================================
// One analytic directional light. `lightVec` points opposite ray travel, so a light overhead is
// sampled straight up. Diffuse takes the energy Fresnel did not reflect and a metal has no diffuse
// lobe at all, so the two terms sum to at most one by construction.
public float3 ComputeDirectionalLight(DirLight light, float3 normal, float3 toEye, float3 baseColor,
                                      float3 f0, float metallic, float alpha)
{
    float3 lightVec = -light.direction;
    float nol = saturate(dot(normal, lightVec));
    if (nol <= 0.0f)
    {
        return float3(0.0f);
    }

    // A shading normal perturbed by a normal map can face away from a view direction the geometry
    // still faces, which would drive the visibility term negative; the floor keeps it finite.
    float nov = max(dot(normal, toEye), 1e-4f);
    float3 halfVec = normalize(toEye + lightVec);
    float noh = saturate(dot(normal, halfVec));
    float voh = saturate(dot(toEye, halfVec));

    float3 fresnel = F_Schlick(f0, voh);
    float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * baseColor / kPi;
    float3 specular = D_GGX(noh, alpha) * V_SmithHeightCorrelated(nov, nol, alpha) * fresnel;

    return (diffuse + specular) * nol * light.strength;
}

//======================================================================================================================
// Image-based lighting from the three assets Source/Engine/Ibl.h generates.
//
//   `irradiance`  -- that file's cosine convolution, already divided by pi, so it multiplies an
//                    albedo directly rather than needing a 1/pi here.
//   `prefiltered` -- the GGX-prefiltered radiance already sampled at the reflection vector and the
//                    mip that file maps this roughness to.
//   `dfg`         -- the split-sum table's (scale, bias) at (N.V, perceptual roughness), which
//                    reconstruct a single-scattering specular response as F0 * scale + bias.
//
// Single scattering alone is not energy conserving: the microfacets that mask a path carry their
// energy out of the lobe and the table's scale + bias falls with roughness accordingly, reaching
// 0.32 at roughness 1 -- so a white furnace would return a third of its own radiance on a rough
// conductor. Fdez-Aguera's compensation redistributes exactly that missing fraction, weighted by
// the average Fresnel a further bounce would see, and hands whatever neither lobe took to diffuse.
// A white furnace then returns its own radiance at every roughness and every metallic value, which
// is the property Tests/GpuRendererTests.cpp's furnace case measures.
public float3 ImageBasedLight(float3 irradiance, float3 prefiltered, float2 dfg, float3 baseColor,
                              float3 f0, float metallic)
{
    // Single-scattering specular, and the energy the single-scattering lobe failed to carry.
    float3 singleScatter = f0 * dfg.x + dfg.y;
    float multiScatterEnergy = 1.0f - (dfg.x + dfg.y);

    // Average Fresnel over the hemisphere for Schlick's form; the 1/21 is the closed-form integral
    // of (1 - cos)^5 against the projected-solid-angle measure.
    float3 averageFresnel = f0 + (1.0f - f0) / 21.0f;
    float3 multiScatter = multiScatterEnergy * singleScatter * averageFresnel /
                          max(1.0f - averageFresnel * multiScatterEnergy, 1e-4f);

    // Whatever neither specular term reflected is what the diffuse lobe may still carry.
    float3 diffuseEnergy = 1.0f - (singleScatter + multiScatter);
    float3 diffuse = baseColor * (1.0f - metallic) * diffuseEnergy;

    return singleScatter * prefiltered + (multiScatter + diffuse) * irradiance;
}
