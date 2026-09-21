//----------------------------------------------------------------------------------------------------------------------
/// @file SceneIds.h
/// @brief Declares scene-local generational identity handles.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Containers/Handle.h"

namespace lmx::engine {
/// Distinct scene-local instance identity; store zero is invalid.
using InstanceId = Handle<struct InstanceTag>;
static_assert(sizeof(InstanceId) == 8);

/// Distinct scene-local mesh identity; store zero is invalid.
using MeshId = Handle<struct MeshTag>;
static_assert(sizeof(MeshId) == 8);

/// Distinct scene-local material identity; store zero is invalid.
using MaterialId = Handle<struct MaterialTag>;
static_assert(sizeof(MaterialId) == 8);

/// Distinct scene-local texture identity; store zero is invalid.
using TextureId = Handle<struct TextureTag>;
static_assert(sizeof(TextureId) == 8);

/// Distinct scene-local local-light identity; store zero is invalid.
using LightId = Handle<struct LightTag>;
static_assert(sizeof(LightId) == 8);

} // namespace lmx::engine
