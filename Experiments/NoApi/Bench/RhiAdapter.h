//----------------------------------------------------------------------------------------------------------------------
/// @file RhiAdapter.h
/// @brief Declares RhiAdapter for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the maintained-RHI adapter: the incumbent half of M5.1's Stage 3 comparison,
///        executing the frozen representative graph by hand-encoding production rhi::CommandList
///        calls (spec section 6, plan Stage 3 item 1).

#pragma once
#include "Bench/Metrics.h"
#include "Bench/Runner.h"
#include "Workload/RepresentativeGraph.h"

#include "RHI/RHI.h"
#include "Render/RenderGraph.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace lmx::noapi::bench {

/// Executes the M5.1 representative graph (spec section 6) over the production RHI: P03/P04 bind
/// the unmodified production ShadowPass/ScenePass pipelines and their complete binding surface;
/// P01/P02/P13 are plain copy commands; P05-P12 bind the distilled Experiments/NoApi/Shaders
/// kernels. See RhiAdapter.cpp's header comment for exactly which of this class's work runs before,
/// inside, and after the spec section 8 timed region.
///
/// The frame's schedule and barriers are derived exactly once, in setup(), by declaring the
/// manifest's R1-R10 (plus the tracked material-0 emissive texture) to a production
/// `lmx::render::RenderGraph` over this adapter's own real resources and calling `compileFrame()`
/// one time -- never per frame, and never inside runFrame() (spec section 8: "the graph planner
/// must NOT run per-frame or inside any timed region; the schedule is data from the manifest").
/// runFrame() replays the resulting fixed pass order and barrier list verbatim every frame, varying
/// only the per-frame data the manifest's draw/update policy assigns (camera pose, per-draw
/// material parameters already fixed by draw index, and R10's staging content).
class RhiAdapter final : public Adapter {
public:
    explicit RhiAdapter(bool enableValidation = true) : m_enableValidation(enableValidation) {}
    bool setup() override;
    void runFrame(uint32_t frameIndex, std::vector<uint8_t>& outReadback) override;
    void teardown() override;
    uint64_t lastFrameTimedRegionNs() const override { return m_lastFrameTimedRegionNs; }
    FrameBindingCounters lastFrameBindingCounters() const override { return m_lastFrameCounters; }
    AllocationSnapshot allocationSnapshot() const override;

    /// One pipeline's cold-compile wall time and whether it loaded a precompiled metallib (spec
    /// section 8: "Pipeline compile and cache ... descriptive evidence only"). Populated by
    /// createPipelines(), called once from setup() -- so every entry is a "cold" (fresh-process,
    /// first-creation) measurement; this adapter never recompiles a pipeline a second time.
    struct PipelineCompileRecord {
        std::string label;
        uint64_t coldNs = 0;
        bool loadedMetallib = false; ///< False means the runtime-MSL fallback compiled it.
    };
    const std::vector<PipelineCompileRecord>& pipelineCompileTimes() const {
        return m_pipelineCompileTimes;
    }

private:
    bool m_enableValidation = true;
    // One texture-subresource barrier the one-time compile derived, resolved to this adapter's own
    // live rhi::Texture.
    struct TextureBarrierOp {
        rhi::Texture* texture = nullptr;
        rhi::TextureSubresourceRange range;
        rhi::TextureUse from = rhi::TextureUse::ShaderRead;
        rhi::TextureUse to = rhi::TextureUse::ShaderRead;
    };
    // The buffer counterpart, on the same terms.
    struct BufferBarrierOp {
        rhi::Buffer* buffer = nullptr;
        rhi::BufferUse from = rhi::BufferUse::ShaderRead;
        rhi::BufferUse to = rhi::BufferUse::ShaderRead;
    };
    using BarrierOp = std::variant<TextureBarrierOp, BufferBarrierOp>;

    // One material's five ScenePass texture slots (spec section 6: "t0 base color sRGB, t1 normal,
    // t4 metallic-roughness, t5 occlusion, t6 emissive sRGB"), generated once at setup.
    struct MaterialTextures {
        std::unique_ptr<rhi::Texture> baseColor;
        std::unique_ptr<rhi::Texture> normal;
        std::unique_ptr<rhi::Texture> metallicRoughness;
        std::unique_ptr<rhi::Texture> occlusion;
        std::unique_ptr<rhi::Texture> emissive; // Unused for material 0; see m_material0Emissive.
    };

    // Creates every manifest resource, uploads every synthetic asset, compiles pipelines, and runs
    // the one-time graph compile that derives m_barriersBeforePass. Returns false and logs on the
    // first failure.
    bool createResources();
    bool createPipelines();
    // Creates P03's 1,024 per-draw ShadowObjectUniforms buffers once (spec RhiAdapter.cpp's header
    // comment on the uniform-ring capacity finding): the shadow map's light is fixed for the whole
    // run, so every draw's mvp is frame-invariant and these never need to be recreated.
    bool createShadowObjectUniformBuffers();
    bool compileSchedule();

    // One P0N encode function per representative-graph pass, called from runFrame() in schedule
    // order (spec section 6: no culling, so schedule order is P01..P13 declaration order).
    void encodeFillHistogram(rhi::CommandList& commands);
    void encodeUploadEmissive(rhi::CommandList& commands, uint32_t frameIndex);
    void encodeShadow(rhi::CommandList& commands);
    void encodeScene(rhi::CommandList& commands, const glm::mat4& viewProj,
                     const glm::mat4& shadowTransform);
    void encodeHistogramAccumulate(rhi::CommandList& commands);
    void encodeHistogramResolve(rhi::CommandList& commands);
    void encodeBloomThreshold(rhi::CommandList& commands);
    void encodeBloomDown(rhi::CommandList& commands, rhi::Texture& target,
                         const rhi::TextureSubresourceRange& srcRange, uint32_t srcExtent,
                         const rhi::TextureSubresourceRange& dstRange, uint32_t dstExtent);
    void encodeBloomUp(rhi::CommandList& commands, rhi::Texture& baseTexture,
                       const rhi::TextureSubresourceRange& baseRange, rhi::Texture& smallTexture,
                       const rhi::TextureSubresourceRange& smallRange, uint32_t smallExtent,
                       rhi::Texture& dstTexture, const rhi::TextureSubresourceRange& dstRange,
                       uint32_t dstExtent);
    void encodeComposite(rhi::CommandList& commands);
    void encodeReadback(rhi::CommandList& commands);

    // M5.1 Stage 4 instrumentation (spec section 9's binding-traffic dimension): every encode
    // function above calls these instead of rhi::CommandList's binding methods directly. Each
    // forwards the call unchanged and bumps exactly one counter in m_frameCounters, so counting
    // adds no work beyond a plain integer increment per call (spec's fairness rule) and no call
    // site has to duplicate the bookkeeping.
    void bindTextureCounted(rhi::CommandList& commands, uint32_t slot, rhi::Texture& texture,
                            const rhi::TextureViewDesc& view = {});
    void bindBufferCounted(rhi::CommandList& commands, uint32_t slot, rhi::Buffer& buffer);
    void bindSamplerCounted(rhi::CommandList& commands, uint32_t slot, rhi::Sampler& sampler);
    void bindStorageBufferCounted(rhi::CommandList& commands, uint32_t slot, rhi::Buffer& buffer,
                                  rhi::StorageAccess access);
    void bindStorageTextureCounted(rhi::CommandList& commands, uint32_t slot, rhi::Texture& texture,
                                   const rhi::TextureViewDesc& view, rhi::StorageAccess access);
    void setUniformsCounted(rhi::CommandList& commands, uint32_t slot, const void* data,
                            uint64_t size);

    // Recreates the R10 ring slot this frame writes with its deterministic staging content (spec
    // section 6: "material 0's emissive texture is re-uploaded every frame via P02"). See
    // RhiAdapter.cpp's header comment for why recreation, rather than an in-place write, is how
    // this adapter realises a per-frame CPU upload through the public RHI.
    void refreshEmissiveStaging(uint32_t frameIndex);

    std::unique_ptr<rhi::Device> m_device;

    // Shader libraries, held for the pipelines' lifetime.
    std::unique_ptr<rhi::ShaderLibrary> m_shadowLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_sceneLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_histAccumulateLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_histResolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomDownLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_bloomUpLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_compositeLibrary;

    std::unique_ptr<rhi::GraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_scenePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_histAccumulatePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_histResolvePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomThresholdPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomDownPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_bloomUpPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_compositePipeline;

    // R1-R10 (spec section 6's resource table), named after the manifest's own ids.
    std::unique_ptr<rhi::Texture> m_r1Shadow;
    std::unique_ptr<rhi::Texture> m_r2SceneColor;
    std::unique_ptr<rhi::Texture> m_r3SceneDepth;
    std::unique_ptr<rhi::Buffer> m_r4Histogram;
    std::unique_ptr<rhi::Buffer> m_r5Exposure;
    std::unique_ptr<rhi::Texture> m_r6BloomA;
    std::unique_ptr<rhi::Texture> m_r7BloomB;
    std::unique_ptr<rhi::Texture> m_r8Out;
    std::unique_ptr<rhi::Buffer> m_r9Readback;
    // UNSCORED DIAGNOSTIC (spec section 2: exploratory additions must be marked unscored). When
    // LMX_NOAPI_DIAG_SCENE is set, runFrame() additionally copies R2 whole into this buffer and
    // returns its bytes instead of R9's, which is what localizes a parity divergence to the scene
    // pass or to the post chain. Null and never touched otherwise, so the scored path is unchanged.
    std::unique_ptr<rhi::Buffer> m_diagSceneColor;
    bool m_diagScene = false;
    // R10's three 16 KiB slots, each recreated on the frame that writes it -- see
    // refreshEmissiveStaging's header comment.
    std::array<std::unique_ptr<rhi::Buffer>, workload::kEmissiveRingSlotCount> m_r10Ring;

    std::unique_ptr<rhi::Texture> m_material0Emissive; // The one tracked Materials texture.
    std::array<MaterialTextures, workload::kMaterialCount> m_materials;

    std::unique_ptr<rhi::Texture> m_irradiance;
    std::unique_ptr<rhi::Texture> m_prefilteredEnv;
    std::unique_ptr<rhi::Texture> m_dfgLut;

    std::unique_ptr<rhi::Buffer> m_quadVertexBuffer;
    std::unique_ptr<rhi::Buffer> m_quadIndexBuffer;

    // Per-draw b1 uniform blocks, delivered through freshly-created persistent buffers
    // (rhi::CommandList::bindBuffer) rather than the per-frame transient ring
    // (rhi::CommandList::setUniforms) -- see RhiAdapter.cpp's header comment for the uniform-ring
    // capacity finding this works around. P03's are frame-invariant and created once at setup;
    // P04's mvp changes every frame (the camera orbits) and is recreated every frame inside
    // encodeScene().
    std::array<std::unique_ptr<rhi::Buffer>, workload::kDrawCount> m_shadowObjectUniformBuffers;
    std::array<std::unique_ptr<rhi::Buffer>, workload::kDrawCount> m_sceneObjectUniformBuffers;

    std::unique_ptr<rhi::Sampler> m_linearSampler;
    std::unique_ptr<rhi::Sampler> m_shadowSampler;
    std::unique_ptr<rhi::Sampler> m_iblSampler;

    // The manifest's fixed pass count (spec section 6: "exactly thirteen, executed P01-P13"),
    // restated here as a local constant because RepresentativeGraph's pass table is a runtime
    // vector, not something an array bound can read directly.
    static constexpr size_t kPassCount = 13;

    // Barriers the one-time compile derived, indexed by schedule position (0 = P01 .. 12 = P13):
    // m_barriersBeforePass[i] is emitted, in order, immediately before pass i's own encode call.
    std::array<std::vector<BarrierOp>, kPassCount> m_barriersBeforePass{};

    // M5.1 Stage 4 instrumentation (spec sections 8-9). m_frameCounters is reset at the top of
    // every runFrame() and populated by the counted* wrappers above and by the barrier-emission
    // lambda inside runFrame(); m_creationCounters accumulates every
    // texture/buffer/sampler/pipeline this adapter has ever asked the RHI to create, in setup() and
    // in every frame's per-draw P04 buffer creation alike (this file's header comment's
    // uniform-ring finding), so its value at end of run is the true cumulative call count -- not a
    // live count, because the RHI's public surface offers no per-object destruction hook cheap
    // enough to call from every encode function without adding work of its own.
    uint64_t m_lastFrameTimedRegionNs = 0;
    FrameBindingCounters m_frameCounters{};
    FrameBindingCounters m_lastFrameCounters{};
    struct CreationCounters {
        uint64_t textureCreateCalls = 0;
        uint64_t bufferCreateCalls = 0;
        uint64_t samplerCreateCalls = 0;
        uint64_t pipelineCreateCalls = 0;
    };
    CreationCounters m_creationCounters{};
    std::vector<PipelineCompileRecord> m_pipelineCompileTimes;
};

} // namespace lmx::noapi::bench
