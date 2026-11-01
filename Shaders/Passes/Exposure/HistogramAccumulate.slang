// Log-luminance histogram accumulation for auto-exposure metering (spec 9/10). One dispatch per
// scene pixel bins that pixel's *scene-referred* luminance -- the pre-exposed value the scene
// target holds, divided back out by the preExposure this frame actually applied -- into one of
// 256 log-spaced bins via an atomic add. The buffer is cleared to zero by a declared copy pass
// (fillBuffer) before this kernel runs; ExposureResolve.slang is the only reader of the result.
//
// This frame's applied preExposure is read from the persistent exposure buffer (gExposure) rather
// than a CPU uniform: it is the same buffer ScenePass.slang/Sky.slang read when auto-exposure is
// on, so the histogram's reconstruction agrees with what shading actually used by construction --
// Renderer.cpp declares this pass's read against the exact version those raster passes read too.
//
// The address root carries the histogram, applied exposure, parameters and sampled scene view.

import RojoRHI;
import ExposureRoots;

static const uint kHistogramBins = 256;
// Rec. 709 luma weights -- the same perceptual-luminance convention the display transform's
// tone map implicitly assumes.
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeHistogramAccumulate(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<HistogramRoot>();
    let params = rojoRHI::load<HistogramParams>(root.paramsAddr, 0);
    let sceneColor = rojoRHI::texture2D<float4>(root.sceneColor);
    if (tid.x >= params.width || tid.y >= params.height)
    {
        return;
    }

    float preExposure = rojoRHI::load<float>(root.exposureAddr, 0);
    float3 displayedColor = sceneColor.Load(int3(int2(tid.xy), 0)).rgb;
    float displayedLuminance = dot(displayedColor, kLuminanceWeights);
    // Scene color is pre-exposed (AGENTS.md); dividing by this frame's own preExposure recovers
    // the scene-referred value the metering formula needs.
    float sceneLuminance = displayedLuminance / max(preExposure, 1e-6);
    float logLuminance = log2(max(sceneLuminance, 1e-6));

    float range = max(params.logLuminanceMax - params.logLuminanceMin, 1e-6);
    float t = saturate((logLuminance - params.logLuminanceMin) / range);
    uint bin = min(uint(t * float(kHistogramBins)), kHistogramBins - 1u);

    rojoRHI::atomicAdd(root.histogramAddr, bin, 1u);
}
