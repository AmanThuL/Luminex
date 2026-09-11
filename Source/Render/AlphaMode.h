//----------------------------------------------------------------------------------------------------------------------
/// @file AlphaMode.h
/// @brief Declares opaque and alpha-tested material coverage.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::render {

/// Coverage policy, independent of lighting and temporal reconstruction.
enum class AlphaMode {
    Opaque, ///< Texture and factor alpha do not discard surface coverage.
    Mask    ///< Discard when sampled base-color alpha times factor alpha is below the cutoff.
};

} // namespace lmx::render
