#pragma once

#include "../RojoRHI/Tests/RhiGpuTestSupport.h"

#include "Asset/Asset.h"
#include "Asset/GeometryGenerator.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <string>

namespace {

template <typename T>

//======================================================================================================================
std::string errorOf(const lmx::asset::AssetResult<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace
