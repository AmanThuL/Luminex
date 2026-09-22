//----------------------------------------------------------------------------------------------------------------------
/// @file Formats.h
/// @brief Names the renderer scene and SDR display storage formats.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Renderer/DisplayDomain.h"
#include <rojoRHI/Format.h>

namespace lmx::render {

/// The two colour formats the frame runs through, named because three places have to agree on
/// each: the texture the renderer creates, the pipeline compiled to render into it, and the format
/// declared when the graph imports it. A literal in any one of those is a trap -- the graph checks
/// attachment roles against what it was told, not against the texture.
///
/// The scene renders in half float because that is what holds radiance above 1.0; the display
/// target is the 8-bit surface the viewport, the swapchain, and the screenshot all expect.
constexpr rojoRHI::Format kSceneColorFormat = rojoRHI::Format::RGBA16Float;
/// Eight-bit BGRA storage of kSdrDisplayDomain; UNORM stores its already-encoded sRGB bytes.
constexpr rojoRHI::Format kDisplayFormat = rojoRHI::Format::BGRA8Unorm;
static_assert(kDisplayFormat == rojoRHI::Format::BGRA8Unorm &&
                  kSdrDisplayDomain.bitsPerChannel == 8,
              "display storage must match the named domain's channel precision");

} // namespace lmx::render
