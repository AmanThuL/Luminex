//----------------------------------------------------------------------------------------------------------------------
/// @file DrawSubmission.h
/// @brief Declares CPU draw preparation and three paced GPU submission slots.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Render/Graph/ResourceUse.h"
#include "Render/Passes/Visibility/Visibility.h"
#include <array>
#include <memory>
#include <optional>
#include <rojoRHI/RHI.h>
#include <vector>

namespace lmx::render {
/// Borrowed scene geometry, row data and rendering settings.
struct SceneView;
/// One CPU-issued command; items in a run share pipeline, material and mesh.
struct DrawRun {
    uint32_t itemIndex = 0;     ///< Representative candidate supplying geometry and textures.
    uint32_t firstEntry = 0;    ///< Absolute visible-row list index.
    uint32_t instanceCount = 0; ///< Number of consecutive row indices consumed.
    uint32_t argumentIndex = 0; ///< Absolute argument index, independent of firstEntry.
};
/// Per-view command list borrowing the Renderer-owned current slot.
struct DrawList {
    std::vector<DrawRun> runs; ///< CPU command order, one run per direct/indirect entry.
    uint32_t firstEntry = 0;   ///< First row in the view's contiguous range.
    uint32_t entryCount = 0;   ///< Number of submitted instances.
    const engine::GpuBuffer* rows = nullptr;        ///< Current paced visible-row allocation.
    const engine::GpuBuffer* arguments = nullptr;   ///< Current paced indirect argument allocation.
    SubmissionMode mode = SubmissionMode::Indirect; ///< Command encoder choice.
};
/// Pure builder output shared by CPU tests and the production upload path.
struct PreparedSubmission {
    std::vector<uint32_t> rows; ///< Scene range followed by shadow range.
    std::vector<rojoRHI::DrawIndexedIndirectArgs> arguments; ///< One argument record per run.
    DrawList scene;                                          ///< Scene commands.
    DrawList shadow;                                         ///< Shadow commands.
};
/// Builds rows and arguments deterministically without touching a GPU.
PreparedSubmission buildDrawSubmission(const SceneView& view, const VisibilityResult& scene,
                                       const VisibilityResult& shadow, SubmissionMode mode);
/// Owns three paced destination/staging pairs and retains replaced owners through retirement.
class DrawSubmission {
public:
    /// Borrows the device until destruction, after the caller has retired all submitted work.
    explicit DrawSubmission(rojoRHI::Device& device);
    /// Copies CPU prefixes outside a render pass on the current paced graphics list. The actual
    /// beginFrame number selects the retired slot; allocation errors leave prepared data intact.
    lmx::Result<void> prepare(rojoRHI::CommandList& commands, uint64_t frameNumber,
                              const SceneView& view, const VisibilityResult& scene,
                              const VisibilityResult& shadow);
    /// Last prepared scene commands, valid through graph execution.
    const DrawList& scene() const { return m_prepared.scene; }
    /// Last prepared shadow commands, valid through graph execution.
    const DrawList& shadow() const { return m_prepared.shadow; }
    /// Last upload and allocation diagnostics.
    SubmissionStats stats() const { return m_stats; }
    /// Current canonical CPU command preparation, before GPU writes.
    const PreparedSubmission& prepared() const { return m_prepared; }
    /// Nominal terminal graph use; CPU-only slots retain a null previous-use import.
    std::optional<BufferUse> rowUse() const { return m_slots[m_lastFrame % 3].rowUse; }
    /// Nominal terminal graph use, not an observation of an actual indirect command.
    std::optional<BufferUse> argumentUse() const { return m_slots[m_lastFrame % 3].argumentUse; }
    /// Records nominal terminal graph reads, including empty lists, for import compatibility.
    void recordUses();

private:
    struct Slot {
        engine::GpuBuffer rows, arguments;
        engine::GpuBuffer rowUpload, argumentUpload;
        std::optional<BufferUse> rowUse, argumentUse;
    };
    struct Retiring {
        uint64_t releaseFrame;
        Slot slot;
    };
    rojoRHI::Device& m_device;
    std::array<Slot, 3> m_slots;
    std::vector<Retiring> m_retiring;
    PreparedSubmission m_prepared;
    SubmissionStats m_stats;
    uint64_t m_lastFrame = 0;
    uint32_t m_capacity = 0;
};
} // namespace lmx::render
