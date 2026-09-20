//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayStage.cpp
/// @brief Implements display resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/DisplayStage.h"

#include "Core/Assert.h"
#include "Render/Renderer.h"

#include <array>
#include <span>
#include <utility>

namespace lmx::render {
namespace {

// Mirrors Shaders/Passes/Display/DisplayTransform.slang's DisplayParams.
struct DisplayParams {
    float bloomIntensity;
};
static_assert(sizeof(DisplayParams) == 4, "must match DisplayTransform.slang's DisplayParams");

// DisplayTransform.slang's own resource set: unrelated to the scene pass's slots above.
constexpr uint32_t kSceneColorTextureSlot = 0;
constexpr uint32_t kDisplayBloomTextureSlot = 1;
constexpr uint32_t kDisplayParamsSlot = 0;

} // namespace

//======================================================================================================================
rojoRHI::Result<void> DisplayStage::loadLibraries(rojoRHI::Device& device) {
    if (auto library = device.loadShaderLibrary("Shaders/DisplayTransform"); library) {
        m_displayLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> DisplayStage::createPipelines(rojoRHI::Device& device) {
    // A fullscreen triangle over an already-rasterised image: no depth to test against and no
    // face to cull, since the one primitive covers the target by construction.
    if (auto pipeline = device.createGraphicsPipeline({.library = m_displayLibrary.get(),
                                                       .vertexEntry = "vertexMain",
                                                       .fragmentEntry = "fragmentMain",
                                                       .colorFormat = kDisplayFormat,
                                                       .depthFormat = rojoRHI::Format::Unknown,
                                                       .cullMode = rojoRHI::CullMode::None,
                                                       .label = "lmx.render.displayPipeline"});
        pipeline) {
        m_displayPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    return {};
}

//======================================================================================================================
rojoRHI::Result<void> DisplayStage::createResources(rojoRHI::Device& device) {
    // A valid resource for DisplayTransform's bloom slot when bloom is off. The shader skips the
    // texture load in that mode, but the argument table must still contain a bound texture.
    {
        const std::array<uint16_t, 4> kZeroHalf4 = {0, 0, 0, 0};
        const rojoRHI::TextureMip mip{.data = kZeroHalf4.data(), .bytesPerRow = sizeof(kZeroHalf4)};
        if (auto texture = device.createTexture({.width = 1,
                                                 .height = 1,
                                                 .format = kSceneColorFormat,
                                                 .sampled = true,
                                                 .label = "lmx.render.blackBloomFallback"},
                                                std::span{&mip, 1});
            texture) {
            m_blackBloomFallback = std::move(*texture);
        } else {
            return std::unexpected(texture.error());
        }
    }

    return {};
}

//======================================================================================================================
void DisplayStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                           GraphTexture displayInput, GraphTexture bloomResult,
                           GraphTexture displayColor, bool bloomEnabled, float bloomIntensity) {
    PassDesc displayDesc;
    // Declaring the read is what orders this pass after the scene pass and puts the scene
    // target's transition to a shader read in front of it; nothing here places a barrier.
    displayDesc.textureReads.push_back(displayInput);
    // Bloom's read is declared only when the toggle is on: an undeclared bloomResult reaches no
    // sink through this pass, so dead-pass culling drops threshold/downsample/upsample together
    // when it is off (spec 10) -- the same pattern the exposure passes above use.
    if (bloomEnabled) {
        displayDesc.textureReads.push_back(TextureUseDesc(
            bloomResult, rojoRHI::TextureSubresourceRange{.baseMipLevel = 0, .mipLevelCount = 1}));
    }
    // The fullscreen triangle covers every pixel, so the clear only states an attachment load
    // action the RHI requires; no fragment reads what it wrote.
    displayDesc.color = ColorAttachment{
        .handle = displayColor, .load = LoadOp::Clear, .store = StoreOp::Store, .clearColor = {}};
    graph.addPass(
        "lmx.pass.display", std::move(displayDesc),
        [this, &commands, displayInput, bloomResult, bloomEnabled,
         bloomIntensity](const PassResources& resources) {
            const GraphResult<rojoRHI::Texture*> hdrTexture = resources.texture(displayInput);
            LMX_ASSERT(hdrTexture.has_value(), hdrTexture.error().message);

            commands.bindPipeline(*m_displayPipeline);
            commands.bindTexture(kSceneColorTextureSlot, **hdrTexture);
            // Disabled bloom binds a valid 1x1 resource and sets the exact zero that makes the
            // shader skip its texture load: scene color + 0 stays bit-identical to scene color
            // alone without addressing outside the fallback texture.
            if (bloomEnabled) {
                const GraphResult<rojoRHI::Texture*> bloomTexture = resources.texture(bloomResult);
                LMX_ASSERT(bloomTexture.has_value(), bloomTexture.error().message);
                commands.bindTexture(kDisplayBloomTextureSlot, **bloomTexture);
            } else {
                commands.bindTexture(kDisplayBloomTextureSlot, *m_blackBloomFallback);
            }
            const DisplayParams params{.bloomIntensity = bloomEnabled ? bloomIntensity : 0.0f};
            commands.bindFrameData(kDisplayParamsSlot, params);
            commands.draw(3);
        });
}

} // namespace lmx::render
