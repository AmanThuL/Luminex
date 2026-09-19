//----------------------------------------------------------------------------------------------------------------------
/// @file LightCheckCapture.h
/// @brief Writes portable raw GPU and CPU light-list evidence for independent audit.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/LightClusterCheck.h"

#include <iosfwd>

namespace lmx::app {
/// Writes the eight-byte LMXLC01 format signature to a new binary evidence stream.
/// Returns false on an output failure; the caller owns path selection and stream lifetime.
bool writeLightCheckHeader(std::ostream& stream);

/// Appends one little-endian frame: frame id, extents, row count, capacities, grid and list
/// lengths, GPU/CPU counters, GPU/CPU records, then GPU/CPU indices. Requires exactly one full
/// grid on each side and defined-prefix list lengths equal to assigned counters. Returns false
/// for malformed input or output failure; no renderer result is changed by serialization.
bool writeLightCheckFrame(std::ostream& stream, const render::LightClusterCheckFrame& frame);
} // namespace lmx::app
