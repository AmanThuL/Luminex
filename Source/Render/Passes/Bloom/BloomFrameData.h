//----------------------------------------------------------------------------------------------------------------------
/// @file BloomFrameData.h
/// @brief Declares the bloom frame data and the pure functions that build it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cstdint>

namespace lmx::render {

/// Mirrors Shaders/Passes/Bloom/BloomThreshold.slang's BloomThresholdParams.
struct BloomThresholdParams {
    float threshold;    ///< 0 -- scene-linear luminance above which a texel contributes.
    uint32_t srcWidth;  ///< 4 -- scene colour width, pixels.
    uint32_t srcHeight; ///< 8 -- scene colour height, pixels.
    uint32_t dstWidth;  ///< 12 -- bloom mip 0 width, pixels.
    uint32_t dstHeight; ///< 16 -- bloom mip 0 height, pixels.
};
static_assert(sizeof(BloomThresholdParams) == 20,
              "must match BloomThreshold.slang's BloomThresholdParams");

/// Mirrors Shaders/Passes/Bloom/BloomDownsample.slang's BloomDownsampleParams.
struct BloomDownsampleParams {
    uint32_t srcWidth;  ///< 0 -- the larger mip's width, pixels.
    uint32_t srcHeight; ///< 4 -- the larger mip's height, pixels.
    uint32_t dstWidth;  ///< 8 -- the smaller mip's width, pixels.
    uint32_t dstHeight; ///< 12 -- the smaller mip's height, pixels.
};
static_assert(sizeof(BloomDownsampleParams) == 16,
              "must match BloomDownsample.slang's BloomDownsampleParams");

/// Mirrors Shaders/Passes/Bloom/BloomUpsample.slang's BloomUpsampleParams.
struct BloomUpsampleParams {
    uint32_t smallWidth;  ///< 0 -- the smaller mip's width, pixels.
    uint32_t smallHeight; ///< 4 -- the smaller mip's height, pixels.
    uint32_t dstWidth;    ///< 8 -- the destination (larger) mip's width, pixels.
    uint32_t dstHeight;   ///< 12 -- the destination (larger) mip's height, pixels.
};
static_assert(sizeof(BloomUpsampleParams) == 16,
              "must match BloomUpsample.slang's BloomUpsampleParams");

/// Plain values the bloom threshold parameters are built from.
struct BloomThresholdInputs {
    float threshold;      ///< Scene-linear luminance threshold.
    uint32_t sceneWidth;  ///< Scene colour width, pixels.
    uint32_t sceneHeight; ///< Scene colour height, pixels.
    uint32_t bloomWidth;  ///< Bloom mip 0 width, pixels (half the scene's, rounded up).
    uint32_t bloomHeight; ///< Bloom mip 0 height, pixels (half the scene's, rounded up).
};

/// Builds the threshold parameters; every field is copied as given.
inline BloomThresholdParams buildBloomThresholdParams(const BloomThresholdInputs& inputs) {
    return {.threshold = inputs.threshold,
            .srcWidth = inputs.sceneWidth,
            .srcHeight = inputs.sceneHeight,
            .dstWidth = inputs.bloomWidth,
            .dstHeight = inputs.bloomHeight};
}

/// Builds the parameters of the downsample from mip `level - 1` to mip `level` (level >= 1) of a
/// chain whose mip 0 is bloomWidth x bloomHeight. Each mip is the base shifted right by its level,
/// never below 1.
inline BloomDownsampleParams buildBloomDownsampleParams(uint32_t bloomWidth, uint32_t bloomHeight,
                                                        uint32_t level) {
    return {.srcWidth = std::max(bloomWidth >> (level - 1), 1u),
            .srcHeight = std::max(bloomHeight >> (level - 1), 1u),
            .dstWidth = std::max(bloomWidth >> level, 1u),
            .dstHeight = std::max(bloomHeight >> level, 1u)};
}

/// Builds the parameters of the upsample that writes mip `level` from mip `level + 1` of a chain
/// whose mip 0 is bloomWidth x bloomHeight. Each mip is the base shifted right by its level, never
/// below 1.
inline BloomUpsampleParams buildBloomUpsampleParams(uint32_t bloomWidth, uint32_t bloomHeight,
                                                    uint32_t level) {
    return {.smallWidth = std::max(bloomWidth >> (level + 1), 1u),
            .smallHeight = std::max(bloomHeight >> (level + 1), 1u),
            .dstWidth = std::max(bloomWidth >> level, 1u),
            .dstHeight = std::max(bloomHeight >> level, 1u)};
}

} // namespace lmx::render
