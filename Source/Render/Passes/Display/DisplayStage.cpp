//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayStage.cpp
/// @brief Implements display resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Display/DisplayStage.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Display/DisplayFrameData.h"
#include "Render/Passes/Display/DisplayRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Renderer/Renderer.h"

#include <array>
#include <span>
#include <utility>

namespace lmx::render {
//======================================================================================================================
lmx::Result<std::unique_ptr<DisplayStage>> DisplayStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<DisplayStage> self(new DisplayStage);

    {
        auto library = gpu.loadShaderLibrary("DisplayTransform");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_displayLibrary = std::move(*library);
    }

    // A fullscreen triangle over an already-rasterised image: no depth to test against and no
    // face to cull, since the one primitive covers the target by construction.
    {
        auto pipeline =
            createFullscreenPipeline(device, self->m_displayLibrary.get(), "fragmentMain",
                                     kDisplayFormat, "lmx.render.displayPipeline");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_displayPipeline = std::move(*pipeline);
    }

    // Keep a real sampled view in the root when bloom is disabled; the shader skips its load.
    {
        const std::array<uint16_t, 4> kZeroHalf4 = {0, 0, 0, 0};
        {
            auto texture =
                createTexel(device, kSceneColorFormat, rojoRHI::TextureType::Tex2D,
                            std::as_bytes(std::span{kZeroHalf4}), "lmx.render.blackBloomFallback");
            if (!texture) {
                return std::unexpected(texture.error());
            }
            self->m_blackBloomFallback = std::move(*texture);
        }
    }

    engine::registerUniformStruct(
        {"DisplayRoot",
         sizeof(DisplayRoot),
         {{"sceneColor", offsetof(DisplayRoot, sceneColor), "uint"},
          {"bloom", offsetof(DisplayRoot, bloom), "uint"},
          {"padding", offsetof(DisplayRoot, padding), "uint"},
          {"bloomIntensity", offsetof(DisplayRoot, bloomIntensity), "float"}}});
    return self;
}

//======================================================================================================================
void DisplayStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                           const DisplayInputs& inputs) {
    const auto displayInput = inputs.displayInput;
    const auto bloomResult = inputs.bloomResult;
    const auto displayColor = inputs.displayColor;
    const auto bloomEnabled = inputs.bloomEnabled;
    const auto bloomIntensity = inputs.bloomIntensity;
    PassDesc displayDesc;
    // Declaring the read is what orders this pass after the scene pass and puts the scene
    // target's transition to a shader read in front of it; nothing here places a barrier.
    displayDesc.textureReads.push_back(displayInput);
    // Bloom's read is declared only when the toggle is on: an undeclared bloomResult reaches no
    // sink through this pass, so dead-pass culling drops threshold/downsample/upsample together
    // when it is off (spec 10) -- the same pattern the exposure passes above use.
    if (bloomEnabled) {
        displayDesc.textureReads.push_back(TextureUseDesc(
            bloomResult, rojoRHI::SubresourceRange{
                             .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1}));
    }
    // The fullscreen triangle covers every pixel, so the clear only states an attachment load
    // action the RHI requires; no fragment reads what it wrote.
    displayDesc.color = ColorAttachment{
        .handle = displayColor, .load = LoadOp::Clear, .store = StoreOp::Store, .clearColor = {}};
    graph.addPass(
        "lmx.pass.display", std::move(displayDesc),
        [this, &commands, displayInput, bloomResult, bloomEnabled,
         bloomIntensity](const PassResources& resources) {
#ifndef NDEBUG
            const auto hdrTexture = lmx::render::texture(resources, displayInput);
            const auto bloomTexture = bloomEnabled ? lmx::render::texture(resources, bloomResult)
                                                   : m_blackBloomFallback.ref();
#endif
            const DisplayParams params = buildDisplayParams(bloomEnabled, bloomIntensity);
            const DisplayRoot root{
                .sceneColor = static_cast<uint32_t>(resources.view(displayInput)),
                .bloom = static_cast<uint32_t>(bloomEnabled ? resources.view(bloomResult)
                                                            : m_blackBloomFallback.view()),
                .bloomIntensity = params.bloomIntensity};
            commands.setPipeline(m_displayPipeline.get());
            engine::registerFrameRoot("DisplayRoot", commands.pushRoot(root));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 2> accesses{
                {{.texture = hdrTexture.texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::FragmentShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.texture = bloomTexture.texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::FragmentShader,
                  .access = rojoRHI::Access::ShaderRead}}};
            commands.declareAccess(
                std::span{accesses}.first(params.bloomIntensity != 0.0f ? 2u : 1u));
#endif
            commands.draw(3);
        });
}

} // namespace lmx::render
