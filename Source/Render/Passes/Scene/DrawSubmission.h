//----------------------------------------------------------------------------------------------------------------------
/// @file DrawSubmission.h
/// @brief Declares CPU draw preparation and three paced GPU submission slots.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Scene/DrawItem.h"
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
/// Fewest runs of one scene pipeline group drawn as a counted multi-draw. Below it a group keeps
/// one lone indirect draw per run, which Metal 4 draws natively; above it the backend's encode
/// kernel and indirect command buffer cost less than the native draws they replace (R5.7 item C).
inline constexpr uint32_t kCountedDrawMinRuns = 512;
/// Most records in one counted multi-draw: RojoRHI's Metal 4 maxDrawIndirectCount.
inline constexpr uint32_t kCountedDrawMaxRecords = 65'536;
/// The scene pass's pipeline group of an item: 0 opaque (one pipeline whatever the sidedness),
/// 1 masked single-sided, 2 masked double-sided, as the scene stage selects its pipelines; the
/// encoder asserts that a counted draw's runs bind one pipeline.
uint32_t sceneDrawGroup(const engine::DrawItem& item);
/// Per-view command list borrowing the Renderer-owned current slot.
struct DrawList {
    std::vector<DrawRun> runs; ///< CPU command order, one run per direct/indirect entry.
    /// Leading runs drawn as counted multi-draws, as run counts in order: each covers consecutive
    /// runs of one pipeline group with consecutive arguments. Runs after them draw one by one.
    std::vector<uint32_t> countedDraws;
    uint32_t firstEntry = 0;                        ///< First row in the view's contiguous range.
    uint32_t entryCount = 0;                        ///< Number of submitted instances.
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
/// Builds rows and arguments deterministically without touching a GPU. On indirect and batched
/// submission, when a scene pipeline group holds at least kCountedDrawMinRuns runs, those groups'
/// runs come first, in group order, and become counted draws of at most kCountedDrawMaxRecords
/// runs, split evenly; the other runs follow in their original order. Shadow runs never move.
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
