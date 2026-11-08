//----------------------------------------------------------------------------------------------------------------------
/// @file DrawItem.h
/// @brief Declares one frame-local draw: its instance row, mesh range and borrowed textures.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Gpu/GpuTexture.h"
#include "Engine/Material/AlphaMode.h"
#include "Engine/Scene/SceneTables.h"

#include <cstdint>

namespace lmx::engine {

/// Frame-local draw order and texture bindings; all borrowed buffers and textures outlive
/// execution.
struct DrawItem {
    /// Opaque full generational identity; zero for hand-built views.
    uint64_t instanceIdentity = 0;
    uint32_t instanceRow = 0; ///< Stable instance slot; independent of draw-list position.
    MeshRow mesh;             ///< Range in the scene geometry pool.
    const GpuTexture* diffuse = nullptr;           ///< Null selects the white fallback.
    const GpuTexture* normalMap = nullptr;         ///< Null selects the flat-normal fallback.
    const GpuTexture* metallicRoughness = nullptr; ///< Null selects the white fallback.
    const GpuTexture* occlusion = nullptr;         ///< Null selects the white fallback.
    const GpuTexture* emissiveMap = nullptr;       ///< Null selects the white fallback.
    AlphaMode alphaMode = AlphaMode::Opaque;       ///< Coverage pipeline selection.
    bool doubleSided = false;                      ///< Masked culling pipeline selection.
};

} // namespace lmx::engine
