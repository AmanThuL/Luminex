//----------------------------------------------------------------------------------------------------------------------
/// @file SceneEnvironment.h
/// @brief Declares the sky, image-based lighting, and light rig shared by every scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Upload/IblUpload.h"
#include <rojoRHI/RHI.h>

#include <memory>
#include <string_view>

namespace lmx::engine {

/// Publishes `scene`'s sky geometry and radiance, the image-based lighting generated from
/// `environment`, without authoring directional lights. Every built-in scene ends
/// its environment construction here, sharing the same split-sum generation.
///
/// `skyCubemap` is the uploaded radiance the sky pass samples and `environment` is that same
/// radiance in CPU form, which the IBL is generated from. Both are taken because a scene chooses
/// how its cubemap is stored -- a one-texel authored constant, or a converted HDRI -- while the
/// generator always needs the CPU copy. Passing radiance that disagrees between them lights the
/// scene from a sky it does not show; keeping them equal is the caller's contract.
///
/// `options` selects the reflection extent and optionally a lower-resolution copy of the same
/// environment for diffuse convolution; its borrowed source only needs to survive this call.
/// Ownership of skyCubemap moves into scene. Returns UploadFailed on GPU creation failure.
asset::AssetResult<void> attachEnvironment(rojoRHI::Device& device, Scene& scene,
                                           std::unique_ptr<rojoRHI::Texture> skyCubemap,
                                           const asset::ibl::CpuCubemap& environment,
                                           std::string_view label,
                                           ibl::GenerationOptions options = {});

/// Calls attachEnvironment with the authored neutral sky every scene without its own environment
/// shares: one sRGB texel of light overcast sky, decoded once so the sky pass and the generated
/// image-based lighting describe the same radiance, with directional lighting owned by document
/// nodes. `label` prefixes the created GPU objects' debug labels.
asset::AssetResult<void> attachNeutralEnvironment(rojoRHI::Device& device, Scene& scene,
                                                  std::string_view label);

} // namespace lmx::engine
