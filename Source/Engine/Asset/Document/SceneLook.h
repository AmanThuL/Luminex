//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLook.h
/// @brief Defines the CPU-owned exposure, bloom, shadow and environment document settings.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace lmx::asset {

/// Document shadow filtering, translated explicitly at the Render boundary.
enum class ShadowFilter {
    PCF,  ///< Fixed-kernel percentage-closer filtering.
    PCSS, ///< Contact-hardening percentage-closer soft shadows.
};

/// Authored scene look; defaults match the original editor and scene-view look exactly.
struct SceneLook {
    /// Manual exposure and complete automatic metering configuration.
    struct Exposure {
        float ev = 0.0f;                      ///< Manual exposure in stops.
        bool autoEnabled = false;             ///< Enables histogram metering.
        float lowPercentile = 50.0f;          ///< Lower retained percentile, in percent.
        float highPercentile = 95.0f;         ///< Upper retained percentile, in percent.
        float targetGrey = 0.18f;             ///< Target scene-linear luminance.
        float evMin = -8.0f;                  ///< Minimum automatic exposure in stops.
        float evMax = 8.0f;                   ///< Maximum automatic exposure in stops.
        float compensationEv = 0.0f;          ///< Automatic exposure bias in stops.
        float adaptUpStopsPerSecond = 3.0f;   ///< Brightening speed.
        float adaptDownStopsPerSecond = 1.5f; ///< Darkening speed.
        /// Compares the complete authored values without tolerance.
        bool operator==(const Exposure&) const = default;
    };

    /// Bloom contribution to the display transform.
    struct Bloom {
        bool enabled = true;    ///< Enables bloom contribution.
        float threshold = 1.0f; ///< Scene-linear luminance threshold.
        float intensity = 0.2f; ///< Contribution weight.
        /// Compares the complete authored values without tolerance.
        bool operator==(const Bloom&) const = default;
    };

    /// A required HDRI file with its conversion settings and content identity.
    struct Hdri {
        /// Decoded filesystem path relative to Assets/; JSON reading/writing decodes/encodes once.
        std::string uri;
        std::string sha256;            ///< Lowercase SHA-256 of the referenced file.
        float yaw = 0.0f;              ///< Equirectangular yaw in radians.
        float scale = 0.25f;           ///< Scene-linear radiance multiplier.
        uint32_t faceSize = 128;       ///< Sky and reflection cubemap face extent.
        uint32_t diffuseFaceSize = 32; ///< Diffuse-convolution source face extent.
        /// Compares the complete authored values without tolerance.
        bool operator==(const Hdri&) const = default;
    };

    /// Neutral authored sky or a required HDRI used by sky and IBL together.
    struct Environment {
        std::array<uint8_t, 3> skySrgb8{149, 170, 196}; ///< Neutral sky's encoded sRGB bytes.
        std::optional<Hdri> hdri;                       ///< When present replaces the neutral sky.
        /// Compares the complete authored values without tolerance.
        bool operator==(const Environment&) const = default;
    };

    Exposure exposure;                             ///< Saved metering configuration.
    Bloom bloom;                                   ///< Saved bloom configuration.
    ShadowFilter shadowFilter = ShadowFilter::PCF; ///< Saved shadow filter.
    Environment environment;                       ///< Saved sky and IBL source.

    /// Compares the complete authored look without tolerance.
    bool operator==(const SceneLook&) const = default;
};

/// One authored look value outside the range every readable document satisfies.
struct SceneLookRangeError {
    std::string path;    ///< JSON pointer below the look object, such as /exposure/targetGrey.
    std::string message; ///< Requirement the value violates.
};

/// Checks the exposure and bloom ranges shared by the document reader and live edits:
/// 0 <= lowPercentile < highPercentile <= 100, targetGrey > 0, evMin <= evMax, nonnegative
/// adaptation speeds and nonnegative bloom threshold and intensity. Returns the first violation in
/// document order, or nothing. Finiteness is the caller's separate check.
std::optional<SceneLookRangeError> sceneLookRangeError(const SceneLook& look);

} // namespace lmx::asset
