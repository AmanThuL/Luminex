// PCF/PCSS filtering module. Resources arrive as parameters, so importers own slot layout.
// Constants preserve the regression scene's filter calibration.
module Shadow;

static const float kEps = 1e-3;
static const float kPi = 3.141592653589793;
static const float kPi2 = 6.283185307179586;
static const int kNumSamples = 25;
static const int kNumRings = 10;

static const float kShadowDepthBias = 0.004;
static const int kPcfNumSamples = kNumSamples;
static const int kBlockerSearchNumSamples = kNumSamples;
static const float kNearPlane = 10.0;
static const float kLightWorldSize = 10.0;
static const float kLightFrustumWidth = 1.0;
static const float kLightSizeUv = kLightWorldSize / kLightFrustumWidth;

// Runtime filter selection avoids pipeline permutations.
public static const int kShadowFilterPcf = 0;
public static const int kShadowFilterPcss = 1;

// Slang lowers this mutable module value to per-thread entry-point context.
static float2 gPoissonDisk[kNumSamples];

float Rand_1to1(float x)
{
    return frac(sin(x) * 10000.0);
}

// Full precision keeps the sin-fract hash constants stable; half rounds them materially.
float Rand_2to1(float2 uv)
{
    const float a = 12.9898, b = 78.233, c = 43758.5453;
    float dt = dot(uv.xy, float2(a, b)), sn = fmod(dt, kPi);
    return frac(sin(sn) * c);
}

// Rotate an outward-biased spiral per pixel to trade banding for noise.
void PoissonDiskSamples(const in float2 randomSeed)
{
    float ANGLE_STEP = kPi2 * float(kNumRings) / float(kNumSamples);
    float INV_NUM_SAMPLES = 1.0 / float(kNumSamples);

    float angle = Rand_2to1(randomSeed) * kPi2;
    float radius = INV_NUM_SAMPLES;
    float radiusStep = radius;

    for (int i = 0; i < kNumSamples; i++)
    {
        gPoissonDisk[i] = float2(cos(angle), sin(angle)) * pow(radius, 0.75);
        radius += radiusStep;
        angle += ANGLE_STEP;
    }
}

// Average blocker depth in the PCSS search region around uv.
// Clamp point loads to prevent out-of-range zero depth from creating border blockers.
float FindBlocker(float2 uv, float zReceiver, Texture2D shadowMap)
{
    float searchWidth = kLightSizeUv * (zReceiver - kNearPlane) / zReceiver;

    int numBlockers = 0;
    float blockerSum = 0.0;

    PoissonDiskSamples(float2(Rand_1to1(uv.x), Rand_1to1(uv.y)));

    uint width, height, numMips;
    shadowMap.GetDimensions(0, width, height, numMips);
    float dx = 1.0 / (float)width;

    for (int i = 0; i < kBlockerSearchNumSamples; ++i)
    {
        float2 offset = gPoissonDisk[i] * searchWidth * dx;
        int2 texel = clamp(int2((uv + offset) * float2(width, height)), int2(0),
                           int2(int(width) - 1, int(height) - 1));
        float shadowMapDepth = shadowMap.Load(int3(texel, 0)).r;
        if (shadowMapDepth < zReceiver - kShadowDepthBias)
        {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }

    if (numBlockers < 1)
        return 0.0;
    return blockerSum / float(numBlockers);
}

// Hardware comparison sampling averages a rotated Poisson kernel around post-divide coordinates.
float PCF(float4 coords, float filterRadiusUV, Texture2D shadowMap, SamplerComparisonState samp)
{
    float currentDepth = coords.z;

    uint width, height, numMips;
    shadowMap.GetDimensions(0, width, height, numMips);

    float dx = 1.0f / (float)width; // Texel size
    float percentLit = 0.0f;

    PoissonDiskSamples(float2(Rand_1to1(coords.x), Rand_1to1(coords.y)));

    [unroll]
    for (int i = 0; i < kPcfNumSamples; ++i)
    {
        float2 offset = gPoissonDisk[i] * 5 * dx * filterRadiusUV;
        percentLit += shadowMap
                          .SampleCmpLevelZero(samp, coords.xy + offset,
                                              currentDepth - kShadowDepthBias)
                          .r;
    }

    return percentLit / float(kPcfNumSamples);
}

// PCSS derives penumbra width from average blocker depth, then applies PCF at that radius.
float PCSS(float4 coords, Texture2D shadowMap, SamplerComparisonState samp)
{
    float2 uv = coords.xy;
    float zReceiver = coords.z;

    float avgBlockerDepth = FindBlocker(uv, zReceiver, shadowMap);

    if (avgBlockerDepth < kEps)
        return 1.0;

    float penumbraSize = (zReceiver - avgBlockerDepth) / avgBlockerDepth * kLightSizeUv;

    return PCF(coords, penumbraSize, shadowMap, samp);
}

// Return visible light in [0,1]. shadowPosH is homogeneous after NDC-to-UV * lightViewProj;
// Metal's zero-to-one clip depth already matches the D32 shadow map.
public float CalcShadowFactor(float4 shadowPosH, Texture2D shadowMap, SamplerComparisonState samp,
                              int filter)
{
    shadowPosH.xyz /= shadowPosH.w;

    if (filter == kShadowFilterPcss)
    {
        return PCSS(shadowPosH, shadowMap, samp);
    }
    return PCF(shadowPosH, 5.0, shadowMap, samp);
}
