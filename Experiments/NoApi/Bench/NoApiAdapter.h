//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiAdapter.h
/// @brief Declares the prototype adapter: the address-first half of M5.1's Stage 3 comparison,
///        executing the frozen representative graph through `lmx::noapi` (spec section 6, plan
///        Stage 3 item 2).
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Bench/Metrics.h"
#include "Bench/Runner.h"
#include "Workload/RepresentativeGraph.h"

#include "NoApi/NoApi.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace lmx::noapi::bench {

/// Executes the M5.1 representative graph (spec section 6) over the address-first prototype: every
/// per-draw and per-pass input is a root block pushed into the frame ring and passed to the draw by
/// address, and every texture and sampler is a slot index into the one bindless table. There is no
/// buffer object, no per-draw descriptor, and no texture or sampler binding call anywhere in this
/// file. See NoApiAdapter.cpp's header comment for exactly which of this class's work runs before,
/// inside, and after the spec section 8 timed region.
///
/// The frame's schedule is the manifest's frozen P01-P13 order, and its barriers are derived
/// exactly once, in setup(), by `planBarriers()` walking the manifest's declared resource uses
/// (spec section 8: "the graph planner must NOT run per-frame or inside any timed region").
/// runFrame() replays the resulting fixed stage-pair list verbatim every frame, varying only the
/// per-frame data the manifest's draw/update policy assigns.
class NoApiAdapter final : public Adapter {
public:
    ~NoApiAdapter() override;

    bool setup() override;
    void runFrame(uint32_t frameIndex, std::vector<uint8_t>& outReadback) override;
    void teardown() override;
    uint64_t lastFrameTimedRegionNs() const override { return m_lastFrameTimedRegionNs; }
    FrameBindingCounters lastFrameBindingCounters() const override { return m_lastFrameCounters; }
    AllocationSnapshot allocationSnapshot() const override;

    /// @copydoc RhiAdapter::PipelineCompileRecord
    struct PipelineCompileRecord {
        std::string label;
        uint64_t coldNs = 0;
        bool loadedMetallib = false;
    };
    const std::vector<PipelineCompileRecord>& pipelineCompileTimes() const {
        return m_pipelineCompileTimes;
    }

private:
    // One derived stage dependency, emitted immediately before the pass it is indexed by. The whole
    // barrier vocabulary of this interface is a producer/consumer stage pair plus the cache
    // maintenance the caller declares, so a "barrier" here names no resource at all -- which is
    // what makes one entry able to stand for every hazard a pass boundary carries.
    struct StageBarrier {
        Stage producer = Stage::None;
        Stage consumer = Stage::None;
        Hazard hazards = Hazard::None;
        bool present = false;
    };

    // Creation split into the phases setup() runs in order; each returns false and logs on the
    // first failure.
    bool createMemory();
    bool createTextures();
    bool createSamplersAndTable();
    bool createPipelines();
    bool uploadStaticAssets();
    // Writes every root block whose contents are the same on all 32 frames into the persistent
    // root allocation, once, and records the stable GPU addresses runFrame() reuses. Called after
    // the bindless table exists, because the blocks carry its address.
    void writePersistentRoots();
    void planBarriers();

    // One P0N encode function per representative-graph pass, called from runFrame() in schedule
    // order.
    void encodeFillHistogram(CommandBuffer* commands);
    void encodeUploadEmissive(CommandBuffer* commands, uint32_t frameIndex);
    void encodeShadow(CommandBuffer* commands);
    void encodeScene(CommandBuffer* commands, const glm::mat4& viewProj,
                     const glm::mat4& shadowTransform);
    void encodeHistogramAccumulate(CommandBuffer* commands);
    void encodeHistogramResolve(CommandBuffer* commands);
    void encodeBloomThreshold(CommandBuffer* commands);
    // `root` is one of the persistent bloom root blocks; `destinationExtent` sizes the dispatch.
    void encodeBloomDown(CommandBuffer* commands, GpuAddress root, uint32_t destinationExtent);
    void encodeBloomUp(CommandBuffer* commands, GpuAddress root, uint32_t destinationExtent);
    void encodeComposite(CommandBuffer* commands);
    void encodeReadback(CommandBuffer* commands);

    // Emits the barrier planBarriers() derived for schedule position `passIndex`, if it derived
    // one.
    void emitBarrier(CommandBuffer* commands, size_t passIndex);

    // Writes frame `frameIndex`'s deterministic staging content into the R10 ring slot that frame
    // owns. A plain CPU write through the allocation's mapped pointer: this interface has no buffer
    // object to recreate and no upload command to record.
    void writeEmissiveStaging(uint32_t frameIndex);

    Device* m_device = nullptr;
    Queue* m_queue = nullptr;
    ResidencySet* m_residency = nullptr;
    BindlessTable* m_table = nullptr;
    FrameRing m_ring;

    // Every allocation this adapter owns. `m_staging` is released once setup's uploads have
    // retired, so it does not inflate the resident-bytes dimension Stage 4 measures.
    Allocation m_textureMemory{};  ///< Private placement memory every texture is placed into.
    Allocation m_staging{};        ///< Setup-only upload memory; freed before setup returns.
    Allocation m_geometry{};       ///< The shared quad's vertices and indices.
    Allocation m_storageBuffers{}; ///< R4 histogram and R5 exposure.
    Allocation m_emissiveRing{};   ///< R10's three 16 KiB slots.
    Allocation m_readback{};       ///< R9.
    /// Root blocks whose contents never change across the run: P03's 1,024 per-draw blocks and the
    /// eight P05-P12 kernel/composite blocks. Written once in setup() and referenced by address
    /// every frame, which is the address-first model's own answer to invariant per-draw data -- no
    /// ring slot, no rewrite, no lifetime rule, because nothing ever writes it again.
    Allocation m_persistentRoots{};
    /// UNSCORED DIAGNOSTIC (spec section 2: exploratory additions must be marked unscored). When
    /// LMX_NOAPI_DIAG_SCENE is set, runFrame() additionally copies R2 whole into this allocation
    /// and returns its bytes instead of R9's, which is what localizes a parity divergence to the
    /// scene pass or to the post chain. Empty and never touched otherwise.
    Allocation m_diagSceneColor{};
    bool m_diagScene = false;
    LinearAllocator m_stagingAllocator;

    GpuAddress m_histogramAddress = kNullAddress;
    GpuAddress m_exposureAddress = kNullAddress;
    GpuAddress m_quadVertexAddress = kNullAddress;
    GpuAddress m_quadIndexAddress = kNullAddress;

    // Stable addresses into m_persistentRoots. P03's blocks are one contiguous array, so draw i's
    // address is m_shadowRootBase + i * sizeof(ShadowObjectRoot) -- pointer arithmetic, which is
    // the whole per-draw "binding" that pass performs.
    GpuAddress m_shadowRootBase = kNullAddress;
    GpuAddress m_histAccumulateRoot = kNullAddress;
    GpuAddress m_histResolveRoot = kNullAddress;
    GpuAddress m_bloomThresholdRoot = kNullAddress;
    std::array<GpuAddress, 2> m_bloomDownRoot{}; ///< P08 then P09.
    std::array<GpuAddress, 2> m_bloomUpRoot{};   ///< P10 then P11.
    GpuAddress m_compositeRoot = kNullAddress;

    // Textures in the fixed creation order kTextureIndex* names; `m_textureLabels` owns the storage
    // the descriptors' label views point at.
    std::vector<Texture*> m_textures;
    std::deque<std::string> m_textureLabels;

    Sampler* m_linearSampler = nullptr;
    Sampler* m_shadowSampler = nullptr;
    Sampler* m_iblSampler = nullptr;
    DepthStencilState* m_depthState = nullptr;

    // Shader intermediate code, held for the pipelines' lifetime.
    std::vector<std::byte> m_shadowShader;
    std::vector<std::byte> m_sceneShader;
    std::vector<std::byte> m_histAccumulateShader;
    std::vector<std::byte> m_histResolveShader;
    std::vector<std::byte> m_bloomThresholdShader;
    std::vector<std::byte> m_bloomDownShader;
    std::vector<std::byte> m_bloomUpShader;
    std::vector<std::byte> m_compositeShader;

    Pipeline* m_shadowPipeline = nullptr;
    Pipeline* m_scenePipeline = nullptr;
    Pipeline* m_histAccumulatePipeline = nullptr;
    Pipeline* m_histResolvePipeline = nullptr;
    Pipeline* m_bloomThresholdPipeline = nullptr;
    Pipeline* m_bloomDownPipeline = nullptr;
    Pipeline* m_bloomUpPipeline = nullptr;
    Pipeline* m_compositePipeline = nullptr;

    // The manifest's fixed pass count (spec section 6: "exactly thirteen, executed P01-P13"),
    // restated as a local constant because the manifest's pass table is a runtime vector.
    static constexpr size_t kPassCount = 13;
    std::array<StageBarrier, kPassCount> m_barrierBeforePass{};

    bool m_torndown = false;

    // M5.1 Stage 4 instrumentation (spec sections 8-9): populated at the end of every runFrame()
    // from counters the prototype already maintains at their choke points (CommandBuffer.cpp's
    // `stats`, BindlessTable.cpp's write counters) -- reading them here is a handful of integer
    // copies, not a computation, so it adds no measurable work relative to what was already tracked
    // inside the timed region.
    uint64_t m_lastFrameTimedRegionNs = 0;
    FrameBindingCounters m_lastFrameCounters{};
    // Bindless table write traffic is cumulative on BindlessTable (Metal4Internal.h), so a
    // per-frame count is this frame's post-write total minus the total this adapter observed at the
    // previous sample -- taken once at setup() (after every one-time table write) and again after
    // every frame.
    uint64_t m_tableWriteCallsAtLastSample = 0;
    uint64_t m_tableWriteBytesAtLastSample = 0;
    std::vector<PipelineCompileRecord> m_pipelineCompileTimes;
};

} // namespace lmx::noapi::bench
