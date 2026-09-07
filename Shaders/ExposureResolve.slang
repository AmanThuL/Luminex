// Resolves one frame's histogram into an instantaneous target exposure (spec 9), then steps this
// frame's applied exposure toward it at a bounded rate (M6.2 spec 7). Metering trims the low and
// high percentile of pixel *count* from the binned distribution, averages the log-luminance of
// what remains weighted by how much of each bin's count survived the trim, and converts that back
// into a linear exposure multiplier around a target grey point.
//
// Adaptation is a bounded step in stops per second rather than an exponential lerp, so settle time
// follows from step size and rate exactly -- which is what a frozen stability tolerance needs. A
// rate of 0 disables it and reproduces spec 9's instantaneous answer bit for bit, because the
// log2/exp2 round trip is skipped rather than cancelled. `deltaSeconds` is the App's fixed 1/60 s
// per declared frame, never wall time. The one-frame lag stays: frame N+1 applies what frame N
// resolved here.
//
// The buffer this writes is the persistent {applied, previous} pair. `applied` is what the next
// scene pass multiplies by; `previous` takes what this frame's scene pass multiplied by, so a
// temporal resolve can correct a history recorded at one exposure into another.
//
// One thread: 256 bins is cheap enough that a sequential scan costs nothing worth parallelising,
// and it keeps the CPU reference this is tested against a straight line-for-line port.
//
// Slots: buffers 0 histogram (read-only), 1 exposure pair (read-write), 2 params.

struct ExposureResolveParams
{
    float lowPercentile;   // 0..100, lower bound of the retained population.
    float highPercentile;  // 0..100, upper bound of the retained population.
    float targetGrey;      // Output value the retained population's average maps to.
    float evMin;            // Clamp on the resolved exposure, in stops.
    float evMax;
    float compensationEv;  // Extra stops applied after metering, before the clamp.
    float logLuminanceMin; // Must match HistogramAccumulate.slang's binning range.
    float logLuminanceMax;
    float adaptUpStopsPerSecond;   // Bound on a rising step; 0 disables adaptation.
    float adaptDownStopsPerSecond; // Bound on a falling step; 0 disables adaptation.
    float deltaSeconds;            // The App's fixed frame step, never wall time.
    float pad;                     // Keeps the block a multiple of 16 bytes.
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
    // is the same answer the manual slider's EV 0 gives. Adaptation has no target to step toward
    // here, so this stays the instantaneous answer spec 9 defined -- but the pair still shifts, so
    // the discontinuity is one a history correction can see rather than one it cannot.
    if (totalCount == 0u)
    {
        gExposure[1] = gExposure[0];
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

    float targetExposure =
        (gParams.targetGrey / max(averageLuminance, 1e-6)) * exp2(gParams.compensationEv);

    // Which rate bounds the step is decided on the linear exposures rather than their logarithms:
    // log2 is monotonic, so the comparison is the same one and costs nothing.
    float applied = gExposure[0];
    float rate = targetExposure > applied ? gParams.adaptUpStopsPerSecond
                                          : gParams.adaptDownStopsPerSecond;
    float adapted = targetExposure;
    if (rate > 0.0)
    {
        float maxStep = rate * gParams.deltaSeconds;
        float evApplied = log2(max(applied, 1e-6));
        float evTarget = log2(max(targetExposure, 1e-6));
        adapted = exp2(evApplied + clamp(evTarget - evApplied, -maxStep, maxStep));
    }

    gExposure[1] = applied;
    gExposure[0] = clamp(adapted, exp2(gParams.evMin), exp2(gParams.evMax));
}
