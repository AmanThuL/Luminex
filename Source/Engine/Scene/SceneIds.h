//----------------------------------------------------------------------------------------------------------------------
/// @file SceneIds.h
/// @brief Declares scene-local generational identity handles.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx::engine {
/// Distinct scene-local instance identity; store zero is invalid.
struct InstanceId {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const InstanceId&) const = default;
};
static_assert(sizeof(InstanceId) == 8);

/// Distinct scene-local mesh identity; store zero is invalid.
struct MeshId {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const MeshId&) const = default;
};
static_assert(sizeof(MeshId) == 8);

/// Distinct scene-local material identity; store zero is invalid.
struct MaterialId {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const MaterialId&) const = default;
};
static_assert(sizeof(MaterialId) == 8);

/// Distinct scene-local texture identity; store zero is invalid.
struct TextureId {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const TextureId&) const = default;
};
static_assert(sizeof(TextureId) == 8);

/// Distinct scene-local local-light identity; store zero is invalid.
struct LightId {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const LightId&) const = default;
};
static_assert(sizeof(LightId) == 8);

} // namespace lmx::engine
