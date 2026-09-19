//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterStage.h
/// @brief Owns the paced froxel light grid and declares its count/scan/fill passes.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/LightClusters.h"
#include "Render/RenderGraph.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace lmx::render {

/// Counter storage words per paced slot: LightClusterCounters' six fields plus ABI padding.
inline constexpr uint32_t kLightClusterCounterWords = 8;

/// Everything one declaration needs. `view` and `inverseJitteredProjection` belong to the frame
/// that rasterizes the scene pass, so a fragment's own slice lookup agrees with these bounds
/// exactly. The stage declares nothing unless `clustered` is set and `liveLightCount` is nonzero.
struct LightClusterInputs {
    bool clustered = false;      ///< Whether the frame shades local lights from the grid.
    uint32_t liveLightCount = 0; ///< Rows with a live light; zero declares no pass at all.
    uint32_t rowCount = 0;       ///< Addressable light row slots, free ones included.
    GraphBuffer lights;          ///< The imported light-row table the kernels read.
    glm::mat4 view{1.0f};        ///< World-to-view transform, metres.
    glm::mat4 inverseJitteredProjection{1.0f};                  ///< Inverse scene projection.
    std::array<float, kClusterSliceBoundaryCount> sliceDepth{}; ///< clusterSliceDepths' table.
    uint32_t activeWidth = 0;        ///< Active render rectangle width in pixels.
    uint32_t activeHeight = 0;       ///< Active render rectangle height in pixels.
    uint64_t frameNumber = 0;        ///< Device frame the declaration belongs to.
    bool shaderReadsOutputs = false; ///< A later scene/debug raster pass reads both output buffers.
    bool captureLists = false; ///< Fixture/diagnostic: read the grid and list back at retirement.
};

/// The grid and index list versions a declaration produced; both are empty when nothing was
/// declared, which is what a caller checks before consuming them.
struct LightClusterOutputs {
    GraphBuffer grid;      ///< Per-froxel `(offset, count)` records, in flat froxel order.
    GraphBuffer indices;   ///< The flat row-index list the records address.
    bool declared = false; ///< Whether the four passes were declared at all.
};

/// One frame's readback: always the counters, plus the lists when the declaration asked for them.
struct RetiredLightClusters {
    uint64_t frameNumber = 0;        ///< The device frame the grid was built for.
    LightClusterCounters counters;   ///< Totals the scan reconciled on the GPU.
    std::vector<ClusterRecord> grid; ///< kClusterCount records, or empty without `captureLists`.
    std::vector<uint32_t> indices;   ///< `counters.assigned` entries, or empty likewise.
};

/// Owns three paced slots of grid, list, count and counter buffers plus the three kernels, and
/// declares `lmx.pass.light.reset|count|scan|fill` over them. The kernels reproduce
/// `buildLightClusters` bit for bit; see Shaders/Modules/LightCluster.slang.
class LightClusterStage {
public:
    /// Registers the scalar-packed kernel parameter layout without creating a GPU device.
    static void registerLayoutsForCapture();

    /// Loads the safe-math clustering kernels and allocates every paced slot.
    static rojoRHI::Result<std::unique_ptr<LightClusterStage>> create(rojoRHI::Device& device);

    /// Declares the four passes and retains the declaration's readback context. Returns an
    /// undeclared result when the frame is not clustered or has no live light.
    LightClusterOutputs declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                const LightClusterInputs& inputs);

    /// Fixture-only logical write limits; both must be nonzero and neither may exceed the frozen
    /// physical capacity. The defaults restore kMaxLightsPerCluster and kLightClusterIndexCapacity.
    void setCapacityOverride(uint32_t perCluster = kMaxLightsPerCluster,
                             uint32_t global = kLightClusterIndexCapacity);

    /// Reads back every declaration whose frame is at most `completedFrame`.
    void retireThrough(uint64_t completedFrame);

    /// Moves all newly retired records to the caller, in frame order.
    std::vector<RetiredLightClusters> takeRetired();

private:
    explicit LightClusterStage(rojoRHI::Device& device) : m_device(device) {}

    // One paced set of owned buffers. `used` is what says a later frame's import must seed the
    // previous access, exactly as GpuVisibility's slots do.
    struct Slot {
        std::unique_ptr<rojoRHI::Buffer> grid, indices, counts, counters;
        bool used = false;
        bool shaderRead = false;
    };
    struct Pending {
        uint64_t frameNumber = 0;
        bool captureLists = false;
        rojoRHI::Buffer *grid = nullptr, *indices = nullptr, *counters = nullptr;
    };

    RetiredLightClusters readback(const Pending& pending) const;

    rojoRHI::Device& m_device;
    std::array<std::unique_ptr<rojoRHI::ShaderLibrary>, 3> m_libraries;
    std::array<std::unique_ptr<rojoRHI::ComputePipeline>, 3> m_pipelines;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending;
    std::vector<RetiredLightClusters> m_retired;
    uint32_t m_perClusterCap = kMaxLightsPerCluster;
    uint32_t m_globalCapacity = kLightClusterIndexCapacity;
};

} // namespace lmx::render
