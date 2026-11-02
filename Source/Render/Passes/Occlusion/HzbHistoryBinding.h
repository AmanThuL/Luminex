//----------------------------------------------------------------------------------------------------------------------
/// @file HzbHistoryBinding.h
/// @brief Retains graph-local pyramid imports and declaration-owned history parameters.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Occlusion/Occlusion.h"

#include <memory>
#include <optional>
#include <string_view>

namespace lmx::render {
class HzbStage;
class GpuVisibility;
struct HzbSource;
struct VisibilityDeclarationProjection;
class Renderer;

/// Borrows one graph and command list while retaining its two pyramid allocations.
class HzbGraphBinding : public std::enable_shared_from_this<HzbGraphBinding> {
public:
    /// Releases allocation leases after all graph callbacks using them have expired.
    ~HzbGraphBinding();
    HzbGraphBinding(const HzbGraphBinding&) = delete;
    HzbGraphBinding& operator=(const HzbGraphBinding&) = delete;

private:
    friend class HzbStage;
    friend class HzbHistoryBinding;
    HzbGraphBinding(HzbStage& owner, RenderGraph& graph, rojoRHI::CommandList& commands);
    GraphTexture importSlot(uint32_t slot);
    void advance(uint32_t slot, GraphTexture texture);
    GraphBuffer importPublication();
    void advancePublication(GraphBuffer buffer);
    RenderGraph& graph() const;
    rojoRHI::CommandList& commands() const;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Keeps one old-history observer and its complete declaration values together.
class HzbHistoryBinding : public std::enable_shared_from_this<HzbHistoryBinding> {
public:
    /// Returns the immutable source facts observed at declaration.
    const HzbSource& source() const;
    /// Returns the parameters this declaration supplies to actual recording.
    OcclusionParams recordingParams() const;
    /// Resolves the retained canonical sampled view for this pass.
    rojoRHI::ViewIndex sampledView(const PassResources& resources) const;
    /// Appends a real logical history reader; zero camera work remains a logical observer.
    void appendRead(RenderGraph& graph, std::string_view name, ComputePassDesc desc,
                    ExecuteFn execute, bool potentialCameraWork);
    ~HzbHistoryBinding();

private:
    friend class HzbStage;
    friend class GpuVisibility;
    friend class RenderGraph;
    friend class Renderer;
    HzbHistoryBinding(std::shared_ptr<HzbGraphBinding> graph, uint32_t slot,
                      const HzbSource& source);
    void bindDeclaration(OcclusionParams params,
                         std::shared_ptr<VisibilityDeclarationProjection> projection);
    std::optional<uint64_t> contentFrame() const;
    OcclusionParams prepareRecording() const;
    void publishRecording(const OcclusionParams& params) noexcept;
    rojoRHI::Texture allocationTexture() const;
    engine::TextureRef sampledTexture(const PassResources& resources) const;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace lmx::render
