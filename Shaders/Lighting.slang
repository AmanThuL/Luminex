// Linear-space Blinn-Phong lighting module. It owns no resources or entry points.
module Lighting;

// Constant-buffer float3 fields occupy 16-byte slots in the CPU mirror.
public struct DirLight
{
    public float3 strength;
    public float3 direction;
};

// Linear falloff between the inner and outer range.
public float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick Fresnel approximation; the multiply chain avoids pow() domain behavior.
public float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

// Diffuse plus specular reflectance for one light.
public float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye,
                         float3 diffuseAlbedo, float3 fresnelR0, float shininess)
{
    const float m = shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    // Normalize the specular lobe by (m + 8) / 8.
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(fresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;

    // Compress highlights smoothly into the renderable range.
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (diffuseAlbedo + specAlbedo) * lightStrength;
}

// Evaluate one directional light; lightVec points opposite ray travel.
public float3 ComputeDirectionalLight(DirLight L, float3 diffuseAlbedo, float3 fresnelR0,
                                      float shininess, float3 normal, float3 toEye)
{
    float3 lightVec = -L.direction;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.strength * ndotl;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, diffuseAlbedo, fresnelR0, shininess);
}
