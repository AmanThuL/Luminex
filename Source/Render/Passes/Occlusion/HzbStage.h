//----------------------------------------------------------------------------------------------------------------------
/// @file HzbStage.h
/// @brief Declares exact reversed-depth pyramid allocation and cross-frame ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Occlusion/HzbFrameData.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <memory>
#include <vector>

namespace lmx::render {
class HzbGraphBinding;
class HzbHistoryBinding;
class HzbStage;
struct RendererResizeAccess;
struct HzbResizeTestAccess;

/// Owns an unpublished resize candidate; abandoning it leaves its stage unchanged.
/// Its stage, creating context and device must outlive it and all resources it owns.
class PreparedHzbResize {
public:
    /// A prepared allocation has exactly one owner.
    PreparedHzbResize(const PreparedHzbResize&) = delete;
    /// A prepared allocation cannot acquire a second owner by assignment.
    PreparedHzbResize& operator=(const PreparedHzbResize&) = delete;
    /// Transfers the candidate, leaving other empty.
    PreparedHzbResize(PreparedHzbResize&& other) noexcept;
    /// Releases this candidate before transferring other's, leaving other empty.
    PreparedHzbResize& operator=(PreparedHzbResize&& other) noexcept;
    /// Releases an unpublished pair without changing the stage or a state table.
    ~PreparedHzbResize() noexcept;

private:
    friend class HzbStage;
    friend struct RendererResizeAccess;
    friend struct HzbResizeTestAccess;
    struct Impl;
    explicit PreparedHzbResize(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

/// Allocated pyramid dimensions; each level covers twice as many source texels per axis.
struct HzbLayout {
    uint32_t width = 0;      ///< Padded base width, independent of render scale.
    uint32_t height = 0;     ///< Padded base height, independent of render scale.
    uint32_t levelCount = 0; ///< First count placing both top active dimensions at most 16.
    uint64_t bytes = 0;      ///< Exact texel bytes for both pyramids, excluding driver padding.
};

/// Pads half the output extent so every floor-sized mip contains its ceil-sized active region.
HzbLayout hzbLayout(uint32_t outputWidth, uint32_t outputHeight);

/// Immutable facts of the declared frame that rasterized one pyramid's source depth.
struct HzbSource {
    uint64_t frameNumber = 0; ///< Authored source clock; actual recording has its own witness.
    glm::mat4 viewProjection{1.0f}; ///< Jittered world-to-clip matrix used by source rasterization.
    glm::vec3 cameraPosition{0.0f}; ///< Source world-space eye position, metres.
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f}; ///< Source normalized view direction.
    uint64_t coverageEpoch = 0;                 ///< Scene-wide depth coverage mutation epoch.
    uint64_t sceneGeneration = 0;               ///< Scene identity generation.
    uint32_t activeWidth = 0;                   ///< Rasterized source rectangle width.
    uint32_t activeHeight = 0;                  ///< Rasterized source rectangle height.
    uint32_t outputWidth = 0;                   ///< Source output allocation width.
    uint32_t outputHeight = 0;                  ///< Source output allocation height.
    uint32_t levelCount = 0;                    ///< Number of available pyramid levels.
    bool built = false;                         ///< A build was declared and rooted in the graph.
};

/// Owns two alternating pyramids and pipelines through all deferred graph callbacks.
/// The creating context/device must outlive this stage, its prepared values and graph callbacks.
/// Close borrowed graphs/callbacks and wait for device idle before replacing an existing pair or
/// destroying this stage. Retained bindings and GPU idle do not establish graph closure.
class HzbStage {
public:
    /// Loads reduction/publish pipelines; diagnostic copies use the shared readback path.
    static lmx::Result<std::unique_ptr<HzbStage>> create(engine::GpuContext& gpu,
                                                         bool cpuReadback = false);
    /// Prepares both pyramids from positive output dimensions without changing live state.
    /// A complete same-extent pair needs no new textures. Result errors retain their causal error;
    /// abandoning a successful candidate preserves all live state. Host allocation exceptions are
    /// outside that guarantee. The stage/context/device must outlive the returned candidate.
    lmx::Result<PreparedHzbResize> prepareResize(uint32_t outputWidth, uint32_t outputHeight) const;
    /// Consumes a current candidate belonging to this stage; misuse is fatal before publication.
    /// Replacement requires closed graphs/callbacks and the existing device-idle boundary.
    /// Initial allocation may occur inside a frame before its first graph. No-op consumes only
    /// the candidate. Replacement forgets exactly the old pair, preserving publication state.
    void commitResize(PreparedHzbResize&& prepared, TextureStateTable& states) noexcept;
    /// Allocates from output extent only; unchanged dimensions preserve identity and history.
    /// Pyramids it replaces lose their `states` entries, so a reused address carries nothing.
    lmx::Result<void> resize(uint32_t outputWidth, uint32_t outputHeight,
                             TextureStateTable& states);
    /// Returns the last declared source, or an unbuilt record before the first build.
    const HzbSource& previousSource() const { return m_sources[m_lastBuilt]; }
    /// Returns the last declared pyramid, or an allocated unbuilt texture initially.
    rojoRHI::Texture previousTexture() const { return m_allocation->textures[m_lastBuilt].rhi(); }
    /// Starts the canonical two-slot import bundle for this graph and current command list.
    std::shared_ptr<HzbGraphBinding> beginGraph(RenderGraph& graph, rojoRHI::CommandList& commands);
    /// Imports the last declared source through this graph's canonical slot bundle.
    std::shared_ptr<HzbHistoryBinding> importPrevious(HzbGraphBinding& graph);
    /// Declares a build through the same canonical two-slot bundle.
    GraphTexture declare(HzbGraphBinding& graph, GraphTexture depth, HzbSource source);
    /// Declares each reduction and a rooted publish read; alternates by build count.
    /// The graph and command list remain borrowed through recording. Source depth is sampled.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, GraphTexture depth,
                         HzbSource source);
    /// Returns allocated dimensions and combined pyramid texel bytes.
    const HzbLayout& layout() const { return m_layout; }

private:
    friend class HzbGraphBinding;
    friend class HzbHistoryBinding;
    friend class PreparedHzbResize;
    friend struct RendererResizeAccess;
    friend struct HzbResizeTestAccess;
    bool resizeCurrent(const PreparedHzbResize& prepared) const noexcept;
    // Borrows only replacement owners from a current token; a no-op returns two null pointers.
    std::array<const engine::GpuTexture*, 2>
    resizeTextures(const PreparedHzbResize& prepared) const;
    struct ContentWitness {
        const engine::GpuContext* owner = nullptr;
        const rojoRHI::CommandList* commands = nullptr;
        uint64_t actualFrame = 0;
        uint64_t generation = 0;
        uint64_t serial = 0;
        uint32_t recordedLevels = 0;
        HzbSource source;
        bool complete = false;
    };
    struct Allocation {
        std::array<engine::GpuTexture, 2> textures;
        std::array<ContentWitness, 2> contents;
        uint64_t generation = 0;
    };
    explicit HzbStage(engine::GpuContext& gpu) : m_gpu(gpu), m_device(gpu.device()) {}

    engine::GpuContext& m_gpu;
    rojoRHI::Device m_device;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
    uint32_t m_next = 0;
    uint32_t m_lastBuilt = 1;
    HzbLayout m_layout;
    std::shared_ptr<Allocation> m_allocation;
    std::array<HzbSource, 2> m_sources;
    std::array<uint64_t, 2> m_declaredSerials{};
    uint64_t m_declarationSerial = 0;
    uint64_t m_allocationGeneration = 0;
    struct GraphBorrow {
        const RenderGraph* graph = nullptr;
        const rojoRHI::CommandList* commands = nullptr;
        uint64_t frame = 0;
        std::weak_ptr<HzbGraphBinding> binding;
    };
    std::vector<GraphBorrow> m_graphBindings;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_reduceLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_reducePipeline;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_publishLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_publishPipeline;
    engine::GpuBuffer m_publishBuffer;
    bool m_published = false;
    SyncPoint m_publishPhysical{rojoRHI::Stage::None, rojoRHI::Access::None,
                                rojoRHI::Layout::Undefined};
};

} // namespace lmx::render
