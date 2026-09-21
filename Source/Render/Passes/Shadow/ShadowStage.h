//----------------------------------------------------------------------------------------------------------------------
/// @file ShadowStage.h
/// @brief Declares shadow fitting and the concrete shadow draw stage.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Scene/DrawSubmission.h"
#include "Render/Renderer/SceneView.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace lmx::render {

/// The light's view-projection and the same matrix with the NDC -> texcoord map baked in, which is
/// what ScenePass.slang hands to CalcShadowFactor.
struct ShadowMatrices {
    glm::mat4 viewProj;        ///< World-to-light clip transform.
    glm::mat4 shadowTransform; ///< World-to-shadow-texture transform.
};

/// For a right-handed camera and Metal's [0,1] clip depth, put the light at -2r along its own
/// direction, look at the sphere's centre, and fit an
/// orthographic frustum to the sphere exactly (extents +/-r, near r, far 3r).
///
/// Depth is reversed, like the camera's: the near plane maps to 1 and the far plane to 0, so the
/// surface nearest the light holds the larger value. Everything downstream is built on that --
/// the shadow pass clears to 0 and keeps what compares Greater, and the comparison sampler is
/// GreaterEqual.
///
/// A free function because it is pure arithmetic on the scene's bounds -- unit-testable without a
/// device, which is where its coverage lives (Tests/RenderTests.cpp).
///
/// `lightDir` is the direction the rays travel and need not be normalised. A direction parallel to
/// world up is handled rather than producing NaNs because the editor can reach it.
ShadowMatrices fitShadowOrtho(const glm::vec4& boundingSphere, const glm::vec3& lightDir);

/// Borrowed resources and copied light transform for one shadow declaration.
/// The textures, sampler and SceneView referents must survive graph execution.
struct ShadowStageInputs {
    DrawList draws;                        ///< Prepared commands and paced buffers for this view.
    GraphBuffer drawRows;                  ///< Read-only visible-row import.
    GraphBuffer drawArguments;             ///< Indirect-argument import.
    std::vector<GraphBuffer> sceneBuffers; ///< Five read-only scene pool/table imports.
    GraphTexture shadowMap;                ///< D32Float attachment version to write.
    glm::mat4 lightViewProj;               ///< World-to-light clip transform, reversed depth.
    rojoRHI::Texture* whiteTexture;        ///< Non-null neutral diffuse fallback owned by Renderer.
    rojoRHI::Sampler* linearSampler;       ///< Non-null material sampler owned by Renderer.
};

/// Owns shadow pipelines; Renderer keeps the stage alive through graph execution.
class ShadowStage {
public:
    /// Creates all opaque and masked variants; propagates library or pipeline creation errors.
    static rojoRHI::Result<std::unique_ptr<ShadowStage>> create(rojoRHI::Device& device);

    /// Registers the shared shadow-pass capture layout idempotently, without a device.
    static void registerUniformLayoutsForCapture();

    /// Declares the depth pass and returns its written version. Copies frame values; borrows
    /// commands and view resources until execution on those commands completes.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const ShadowStageInputs& inputs);

private:
    ShadowStage() = default;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_shadowLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_maskShadowLibrary;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_shadowPipeline;
    std::array<std::unique_ptr<rojoRHI::GraphicsPipeline>, 2> m_maskShadowPipelines;
};

} // namespace lmx::render
