//----------------------------------------------------------------------------------------------------------------------
/// @file SceneEnvironment.h
/// @brief Declares the sky, image-based lighting, and light rig shared by every scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "Engine/Ibl.h"
#include "Engine/Scene.h"
#include "RHI/RHI.h"

#include <memory>
#include <string_view>

namespace lmx::engine {

/// Publishes `scene`'s sky geometry and radiance, the image-based lighting generated from
/// `environment`, and the catalog's shared three-light directional rig. Every built-in scene ends
/// its construction here, so they all light from the same rig and the same split-sum set.
///
/// `skyCubemap` is the uploaded radiance the sky pass samples and `environment` is that same
/// radiance in CPU form, which the IBL is generated from. Both are taken because a scene chooses
/// how its cubemap is stored -- a one-texel authored constant, or a converted HDRI -- while the
/// generator always needs the CPU copy. Passing radiance that disagrees between them lights the
/// scene from a sky it does not show; keeping them equal is the caller's contract.
///
/// `options` selects the reflection extent and optionally a lower-resolution copy of the same
/// environment for diffuse convolution; its borrowed source only needs to survive this call.
/// `analyticLights` false zeroes the rig's strengths, for a scene whose environment already
/// contains its key lights and would otherwise be lit twice. Ownership of `skyCubemap` moves into
/// `scene`. Returns `AssetErrorCode::UploadFailed` if any GPU resource cannot be created.
AssetResult<void> attachEnvironment(rhi::Device& device, Scene& scene,
                                    std::unique_ptr<rhi::Texture> skyCubemap,
                                    const ibl::CpuCubemap& environment, bool analyticLights,
                                    std::string_view label, ibl::GenerationOptions options = {});

/// Calls attachEnvironment with the authored neutral sky every scene without its own environment
/// shares: one sRGB texel of light overcast sky, decoded once so the sky pass and the generated
/// image-based lighting describe the same radiance, with the analytic rig on. `label` prefixes the
/// created GPU objects' debug labels.
AssetResult<void> attachNeutralEnvironment(rhi::Device& device, Scene& scene,
                                           std::string_view label);

} // namespace lmx::engine
