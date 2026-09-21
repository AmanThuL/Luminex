//----------------------------------------------------------------------------------------------------------------------
/// @file StageSetup.cpp
/// @brief Creates labelled stage resources without changing caller policy or creation order.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/StageSetup.h"

#include <array>
#include <string>
#include <utility>

namespace lmx::render {

//======================================================================================================================
rojoRHI::GraphicsPipelineDesc fullscreenPipelineDesc(rojoRHI::ShaderLibrary* library,
                                                     std::string_view fragmentEntry,
                                                     rojoRHI::Format colorFormat,
                                                     std::string_view label) {
    return {.library = library,
            .vertexEntry = "vertexMain",
            .fragmentEntry = fragmentEntry,
            .colorFormat = colorFormat,
            .depthFormat = rojoRHI::Format::Unknown,
            .cullMode = rojoRHI::CullMode::None,
            .label = label};
}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
createTexel(rojoRHI::Device& device, rojoRHI::Format format, rojoRHI::TextureKind kind,
            std::span<const std::byte> bytes, std::string_view label) {
    const rojoRHI::TextureMip mip{.data = bytes.data(), .bytesPerRow = bytes.size()};
    const std::array<rojoRHI::TextureMip, 6> mips{mip, mip, mip, mip, mip, mip};
    const uint32_t faces = kind == rojoRHI::TextureKind::Cube ? 6u : 1u;
    return device.createTexture(
        {.width = 1, .height = 1, .format = format, .kind = kind, .sampled = true, .label = label},
        std::span{mips.data(), faces});
}

//======================================================================================================================
rojoRHI::Result<ComputePipelines> createComputePipelines(rojoRHI::Device& device,
                                                         std::span<const char* const> names,
                                                         std::string_view labelPrefix,
                                                         uint32_t (*threadsFor)(uint32_t)) {
    ComputePipelines result;
    result.libraries.reserve(names.size());
    result.pipelines.reserve(names.size());
    for (uint32_t index = 0; index < names.size(); ++index) {
        auto library = device.loadShaderLibrary("Shaders/" + std::string(names[index]));
        if (!library) {
            return std::unexpected(library.error());
        }
        result.libraries.push_back(std::move(*library));
        auto pipeline =
            device.createComputePipeline({.library = result.libraries.back().get(),
                                          .computeEntry = "computeMain",
                                          .threadsPerThreadgroup = {threadsFor(index), 1, 1},
                                          .label = std::string(labelPrefix) + names[index]});
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        result.pipelines.push_back(std::move(*pipeline));
    }
    return result;
}

} // namespace lmx::render
