// P06 histogram.resolve's binding frontend (spec section 6): trims the low/high percentile of pixel
// count from the histogram R4 accumulated, averages the surviving log-luminance, and writes this
// frame's exposure into R5 -- the cross-frame feedback edge P05 reads on the following frame. One
// thread, exactly like Shaders/ExposureResolve.slang: 256 bins is cheap enough that a sequential
// scan costs nothing worth parallelising. Math lives in NoApiShared.slang.
//
// Slots: buffers 0 histogram (read-only), 1 exposure (write), 2 params.
import NoApiShared;

struct ResolveParams
{
    float lowPercentile;
    float highPercentile;
    float targetGrey;
    float evMin;
    float evMax;
    float compensationEv;
    float logLuminanceMin;
    float logLuminanceMax;
};

[[vk::binding(0, 0)]]
StructuredBuffer<uint> gHistogram : register(t0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gExposure : register(u1); // → MSL [[buffer(1)]]

[[vk::binding(2, 0)]]
ConstantBuffer<ResolveParams> gParams : register(b2); // → MSL [[buffer(2)]]

static const uint kHistogramBins = 256;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeHistResolve(uint3 tid: SV_DispatchThreadID)
{
    uint totalCount = 0u;
    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        totalCount += gHistogram[i];
    }

    if (totalCount == 0u)
    {
        gExposure[0] = 1.0;
        return;
    }

    float lowCount = gParams.lowPercentile * 0.01 * float(totalCount);
    float highCount = gParams.highPercentile * 0.01 * float(totalCount);

    float weightedLogLuminance = 0.0;
    float weight = 0.0;
    float cumulative = 0.0;

    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        float count = float(gHistogram[i]);
        float binStart = cumulative;
        float binEnd = cumulative + count;
        float overlap = max(min(binEnd, highCount) - max(binStart, lowCount), 0.0);
        float binCenterLogLuminance =
            BinCenterLogLuminance(i, gParams.logLuminanceMin, gParams.logLuminanceMax, kHistogramBins);
        weightedLogLuminance += overlap * binCenterLogLuminance;
        weight += overlap;
        cumulative = binEnd;
    }

    float averageLogLuminance = weight > 0.0 ? weightedLogLuminance / weight : 0.0;
    float averageLuminance = exp2(averageLogLuminance);

    float exposure =
        (gParams.targetGrey / max(averageLuminance, 1e-6)) * exp2(gParams.compensationEv);
    exposure = clamp(exposure, exp2(gParams.evMin), exp2(gParams.evMax));

    gExposure[0] = exposure;
}
