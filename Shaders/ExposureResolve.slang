// Resolves one frame's histogram into an instantaneous target exposure (spec 9): trims the low
// and high percentile of pixel *count* from the binned distribution, averages the log-luminance
// of what remains weighted by how much of each bin's count survived the trim, and converts that
// back into a linear exposure multiplier around a target grey point. No adaptation or smoothing --
// the result is this frame's answer alone, exported for frame N+1 to apply (M6 deferral).
//
// One thread: 256 bins is cheap enough that a sequential scan costs nothing worth parallelising,
// and it keeps the CPU reference this is tested against a straight line-for-line port.
//
// Slots: buffers 0 histogram (read-only), 1 exposure result (write), 2 params.

struct ExposureResolveParams
{
    float lowPercentile;   // 0..100, fraction of pixel count trimmed from the shadow end.
    float highPercentile;  // 0..100, fraction of pixel count trimmed from the highlight end.
    float targetGrey;      // The scene-referred luminance exposure aims to map to unit output.
    float evMin;            // Clamp on the resolved exposure, in stops.
    float evMax;
    float compensationEv;  // Extra stops applied after metering, before the clamp.
    float logLuminanceMin; // Must match HistogramAccumulate.slang's binning range.
    float logLuminanceMax;
};

[[vk::binding(0, 0)]]
StructuredBuffer<uint> gHistogram : register(t0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gExposure : register(u1); // → MSL [[buffer(1)]]

[[vk::binding(2, 0)]]
ConstantBuffer<ExposureResolveParams> gParams : register(b2); // → MSL [[buffer(2)]]

static const uint kHistogramBins = 256;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeExposureResolve(uint3 tid: SV_DispatchThreadID)
{
    uint totalCount = 0u;
    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        totalCount += gHistogram[i];
    }

    // An empty frame (a fully clipped or fully black target) has nothing to meter; unit exposure
    // is the same answer the manual slider's EV 0 gives.
    if (totalCount == 0u)
    {
        gExposure[0] = 1.0;
        return;
    }

    float lowCount = gParams.lowPercentile * 0.01 * float(totalCount);
    float highCount = gParams.highPercentile * 0.01 * float(totalCount);

    float binWidth = (gParams.logLuminanceMax - gParams.logLuminanceMin) / float(kHistogramBins);
    float weightedLogLuminance = 0.0;
    float weight = 0.0;
    float cumulative = 0.0;

    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        float count = float(gHistogram[i]);
        float binStart = cumulative;
        float binEnd = cumulative + count;
        // The portion of this bin's pixels that survive the percentile trim.
        float overlap = max(min(binEnd, highCount) - max(binStart, lowCount), 0.0);
        float binCenterLogLuminance = gParams.logLuminanceMin + (float(i) + 0.5) * binWidth;
        weightedLogLuminance += overlap * binCenterLogLuminance;
        weight += overlap;
        cumulative = binEnd;
    }

    // weight is zero only when the trim window itself is empty (low >= high), which a Render
    // Settings clamp on the sliders keeps from happening in practice.
    float averageLogLuminance = weight > 0.0 ? weightedLogLuminance / weight : 0.0;
    float averageLuminance = exp2(averageLogLuminance);

    float exposure =
        (gParams.targetGrey / max(averageLuminance, 1e-6)) * exp2(gParams.compensationEv);
    exposure = clamp(exposure, exp2(gParams.evMin), exp2(gParams.evMax));

    gExposure[0] = exposure;
}
