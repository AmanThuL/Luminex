// P06 histogram.resolve's address-first frontend (spec section 6), over the same NoApiShared math
// the incumbent's HistResolve.slang uses. One thread, exactly like its counterpart.
//
// Binding surface: one root block at buffer 0 carrying the addresses of the histogram it reads and
// the exposure value it writes. No storage-buffer bindings exist in this interface at all.
import NoApiShared;

struct HistResolveRoot
{
    float4 percentiles;                  // x low, y high, z target grey, w compensation EV
    float4 evRange;                      // x evMin, y evMax, z logLuminanceMin, w logLuminanceMax
    ConstBufferPointer<uint> histogram;  // R4
    Ptr<float> exposure;                 // R5, this frame's resolved exposure
};

[[vk::binding(0, 0)]]
ConstantBuffer<HistResolveRoot> gRoot : register(b0); // → MSL [[buffer(0)]]

static const uint kHistogramBins = 256;

// FINDING (evidence section 6.8): the prototype's `dispatch` carries threadgroup *counts* only and
// its pipeline descriptor carries no shape, so the implementation pins 8x8x1 for every kernel and
// this shader cannot ask for the `[numthreads(1, 1, 1)]` its incumbent counterpart declares. The
// guard below is how a genuinely single-threaded kernel is expressed under that constraint: 64
// threads launch and 63 return immediately. Recorded rather than worked around, because it is a real
// consequence of the model's dispatch signature meeting Metal's requirement.
[shader("compute")]
[numthreads(8, 8, 1)]
void computeHistResolve(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x != 0u || tid.y != 0u || tid.z != 0u)
    {
        return;
    }

    uint totalCount = 0u;
    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        totalCount += gRoot.histogram[i];
    }

    if (totalCount == 0u)
    {
        gRoot.exposure[0] = 1.0;
        return;
    }

    float lowCount = gRoot.percentiles.x * 0.01 * float(totalCount);
    float highCount = gRoot.percentiles.y * 0.01 * float(totalCount);

    float weightedLogLuminance = 0.0;
    float weight = 0.0;
    float cumulative = 0.0;

    for (uint i = 0u; i < kHistogramBins; ++i)
    {
        float count = float(gRoot.histogram[i]);
        float binStart = cumulative;
        float binEnd = cumulative + count;
        float overlap = max(min(binEnd, highCount) - max(binStart, lowCount), 0.0);
        float binCenterLogLuminance =
            BinCenterLogLuminance(i, gRoot.evRange.z, gRoot.evRange.w, kHistogramBins);
        weightedLogLuminance += overlap * binCenterLogLuminance;
        weight += overlap;
        cumulative = binEnd;
    }

    float averageLogLuminance = weight > 0.0 ? weightedLogLuminance / weight : 0.0;
    float averageLuminance = exp2(averageLogLuminance);

    float exposure =
        (gRoot.percentiles.z / max(averageLuminance, 1e-6)) * exp2(gRoot.percentiles.w);
    exposure = clamp(exposure, exp2(gRoot.evRange.x), exp2(gRoot.evRange.y));

    gRoot.exposure[0] = exposure;
}
