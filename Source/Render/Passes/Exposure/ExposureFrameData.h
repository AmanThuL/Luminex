//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureFrameData.h
/// @brief Declares the exposure frame data and the pure functions that build it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx::render {

/// Lowest log2 luminance the histogram covers. Wide enough for near-black shadow detail without the
/// metering formula knowing a scene's range in advance; HistogramAccumulate.slang and
/// ExposureResolve.slang must agree on both bounds.
constexpr float kExposureLogLuminanceMin = -12.0f;
/// Highest log2 luminance the histogram covers, a strongly overexposed highlight.
constexpr float kExposureLogLuminanceMax = 4.0f;
/// The seconds one declared frame advances the scene by, which is what exposure adaptation steps
/// against. The App's clock is a fixed step per frame (asset::kAnimationBakeRate); this restates
/// the number because Render cannot depend on Asset.
constexpr float kExposureFrameSeconds = 1.0f / 60.0f;

/// Mirrors Shaders/Passes/Exposure/HistogramAccumulate.slang's HistogramParams. Every field is a
/// scalar, so HLSL cbuffer packing (which Slang's Metal path still follows) leaves them contiguous
/// -- no vector field ever forces a gap here.
struct HistogramParams {
    float logLuminanceMin; ///< 0 -- log2 luminance of the first bin.
    float logLuminanceMax; ///< 4 -- log2 luminance of the last bin.
    uint32_t width;        ///< 8 -- metered rectangle width, pixels.
    uint32_t height;       ///< 12 -- metered rectangle height, pixels.
};
static_assert(sizeof(HistogramParams) == 16,
              "must match HistogramAccumulate.slang's HistogramParams");

/// Mirrors Shaders/Passes/Exposure/ExposureSeed.slang's ExposureSeedParams.
struct ExposureSeedParams {
    float exposure; ///< 0 -- the multiplier written into the exposure buffer.
};
static_assert(sizeof(ExposureSeedParams) == 4,
              "must match ExposureSeed.slang's ExposureSeedParams");

/// Mirrors Shaders/Passes/Exposure/ExposureResolve.slang's ExposureResolveParams.
struct ExposureResolveParams {
    float lowPercentile;   ///< 0 -- histogram percentile metering starts at, 0 to 1.
    float highPercentile;  ///< 4 -- histogram percentile metering stops at, 0 to 1.
    float targetGrey;      ///< 8 -- the scene-linear grey the metered value maps to.
    float evMin;           ///< 12 -- lower clamp of the resolved exposure, stops.
    float evMax;           ///< 16 -- upper clamp of the resolved exposure, stops.
    float compensationEv;  ///< 20 -- exposure compensation, stops.
    float logLuminanceMin; ///< 24 -- the histogram's lower bound, log2 luminance.
    float logLuminanceMax; ///< 28 -- the histogram's upper bound, log2 luminance.
    float adaptUp;         ///< 32 -- adaptation speed towards a brighter exposure, stops/second.
    float adaptDown;       ///< 36 -- adaptation speed towards a darker exposure, stops/second.
    float deltaSeconds;    ///< 40 -- the time one frame advances adaptation by.
    float pad;             ///< 44 -- rounds the block to 48 bytes; always 0.
};
static_assert(sizeof(ExposureResolveParams) == 48,
              "must match ExposureResolve.slang's ExposureResolveParams");

/// Plain values the exposure resolve parameters are built from, all from the scene view.
struct ExposureResolveInputs {
    float lowPercentile;           ///< Histogram percentile metering starts at.
    float highPercentile;          ///< Histogram percentile metering stops at.
    float targetGrey;              ///< Scene-linear target grey.
    float evMin;                   ///< Lower clamp, stops.
    float evMax;                   ///< Upper clamp, stops.
    float compensationEv;          ///< Exposure compensation, stops.
    float adaptUpStopsPerSecond;   ///< Adaptation speed towards brighter, stops/second.
    float adaptDownStopsPerSecond; ///< Adaptation speed towards darker, stops/second.
};

/// Builds the histogram parameters: the fixed log-luminance range over a width x height rectangle
/// of render-extent pixels.
inline HistogramParams buildHistogramParams(uint32_t width, uint32_t height) {
    return {.logLuminanceMin = kExposureLogLuminanceMin,
            .logLuminanceMax = kExposureLogLuminanceMax,
            .width = width,
            .height = height};
}

/// Builds the seed parameters. The exposure is exp2(exposureEv), the multiplier the manual EV in
/// stops stands for.
ExposureSeedParams buildExposureSeedParams(float exposureEv);

/// Builds the resolve parameters: the view's metering and adaptation settings, the histogram's
/// fixed range, kExposureFrameSeconds as the step and a zero pad. No value is clamped here.
inline ExposureResolveParams buildExposureResolveParams(const ExposureResolveInputs& inputs) {
    return {.lowPercentile = inputs.lowPercentile,
            .highPercentile = inputs.highPercentile,
            .targetGrey = inputs.targetGrey,
            .evMin = inputs.evMin,
            .evMax = inputs.evMax,
            .compensationEv = inputs.compensationEv,
            .logLuminanceMin = kExposureLogLuminanceMin,
            .logLuminanceMax = kExposureLogLuminanceMax,
            .adaptUp = inputs.adaptUpStopsPerSecond,
            .adaptDown = inputs.adaptDownStopsPerSecond,
            .deltaSeconds = kExposureFrameSeconds,
            .pad = 0.0f};
}

} // namespace lmx::render
